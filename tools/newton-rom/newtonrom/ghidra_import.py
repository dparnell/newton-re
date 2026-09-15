"""Apply a Newton ROM layout and symbol table to a Ghidra program.

This module only imports Ghidra classes inside functions, so the rest of the
package stays usable without Ghidra.  It is driven by two files produced by the
extraction tools:

    layout.json   (extract_rom.py)  memory regions, REx entries
    symbols.json  (dump_symbols.py) demangled symbols + decoded jump table

and expects the program to have been created by importing rom.bin with the
Binary loader at address 0 (language ARM:BE:32:v4, compiler APCS).

What it does, in order (see README.md for the reasoning):

1. Memory map - splits the imported block into ROM_RO / ROM_RWINIT / REX,
   creates RAM_RW (initialised from the ROM copy) and RAM_ZI, and creates the
   JT block: the jump table as the CPU sees it at 0x01A00000 (an image of the
   ROM pages it aliases, see newtonrom/jumptable.py).
2. Classes - a Ghidra class namespace for every C++ class named in a symbol.
3. Functions - disassembles and creates a function at every C++ function
   symbol, every jump-table target, and every plain symbol that looks like ARM
   code; names it (in its class namespace) and applies the demangled parameter
   list where it can be represented.
4. Thunks - every jump-table slot becomes a thunk of its target function, so
   callers show the real name.
5. Labels - all remaining symbols become labels (data, constants, RAM).
"""

from __future__ import annotations

import json
from collections import defaultdict
from typing import Callable, Dict, List, Optional

from . import jumptable

BUILTIN_TYPES = {
    "void": "VoidDataType", "char": "CharDataType", "signed char": "SignedCharDataType",
    "unsigned char": "UnsignedCharDataType", "short": "ShortDataType",
    "unsigned short": "UnsignedShortDataType", "int": "IntegerDataType",
    "unsigned int": "UnsignedIntegerDataType", "long": "LongDataType",
    "unsigned long": "UnsignedLongDataType", "long long": "LongLongDataType",
    "unsigned long long": "UnsignedLongLongDataType", "float": "FloatDataType",
    "double": "DoubleDataType", "long double": "LongDoubleDataType",
    "bool": "BooleanDataType", "wchar_t": "WideCharDataType",
}

Log = Callable[[str], None]


class Stats(dict):
    def bump(self, key: str, n: int = 1) -> None:
        self[key] = self.get(key, 0) + n


# --------------------------------------------------------------------------
# Memory
# --------------------------------------------------------------------------

def setup_memory(program, layout: dict, rom: bytes, monitor, log: Log) -> None:
    mem = program.getMemory()
    space = program.getAddressFactory().getDefaultAddressSpace()
    addr = space.getAddress

    rom_regions = sorted((r for r in layout["regions"] if r["kind"] == "rom"), key=lambda r: r["address"])
    first = mem.getBlock(addr(rom_regions[0]["address"]))
    if first is None:
        raise RuntimeError("no memory block at the ROM base; import rom.bin with the Binary loader at 0")
    for r in rom_regions[1:]:
        start = addr(r["address"])
        block = mem.getBlock(start)
        if block is not None and block.getStart() != start:
            mem.split(block, start)
    for r in rom_regions:
        block = mem.getBlock(addr(r["address"]))
        block.setName(r["name"])
        _set_perms(block, r)
        block.setComment(r["comment"])
        # Anything left over (e.g. loader padding) after the last region keeps its name.
    for r in layout["regions"]:
        if r["kind"] == "ram_init":
            block = mem.createInitializedBlock(r["name"], addr(r["address"]), r["size"], 0, monitor, False)
            data = rom[r["rom_offset"]:r["rom_offset"] + r["size"]]
            mem.setBytes(addr(r["address"]), data)
            _set_perms(block, r)
            block.setComment(r["comment"])
        elif r["kind"] == "ram_zero":
            block = mem.createInitializedBlock(r["name"], addr(r["address"]), r["size"], 0, monitor, False)
            _set_perms(block, r)
            block.setComment(r["comment"])
    log("memory map: " + ", ".join(f"{b.getName()}@{b.getStart()}" for b in mem.getBlocks()))


def _set_perms(block, r: dict) -> None:
    block.setRead(bool(r.get("read", True)))
    block.setWrite(bool(r.get("write", False)))
    block.setExecute(bool(r.get("execute", False)))
    block.setVolatile(False)


def create_jump_table_block(program, jt: dict, rom: bytes, monitor, log: Log) -> None:
    """Create the JT block at the virtual base, mirroring the ROM pages it aliases."""
    mem = program.getMemory()
    space = program.getAddressFactory().getDefaultAddressSpace()
    count, base, rom_addr = jt["count"], jt["virtual_base"], jt["rom_address"]
    pages = jumptable.page_count(count)
    block = mem.createInitializedBlock("JT", space.getAddress(base), pages * jumptable.PAGE_SIZE,
                                       0, monitor, False)
    for p in range(pages):
        src = jumptable.virtual_page_source(p, rom_addr)
        mem.setBytes(space.getAddress(base + p * jumptable.PAGE_SIZE), rom[src:src + jumptable.PAGE_SIZE])
    block.setRead(True)
    block.setWrite(False)
    block.setExecute(True)
    block.setComment(
        f"Patchable jump table as mapped by the MMU: {count} B instructions, 32 per 4 KB page. "
        f"Virtual page p is an alias of ROM page {rom_addr:#x} + (p/32)*0x1000 and owns the "
        f"128-byte slice at offset (p%32)*0x80; the other bytes of each page are unused aliases.")
    log(f"jump table block: {pages} pages at {base:#x} ({count} slots)")


# --------------------------------------------------------------------------
# Names, namespaces and types
# --------------------------------------------------------------------------

class TypeMapper:
    """Turn JSON types (from the demangler or the DDK headers) into Ghidra data types.

    Named types resolve in this order: C builtin, DDK typedef, DDK record,
    DDK enum, then a class struct (for a class named by some symbol) or an
    opaque zero-length struct in /Newton/types.
    """

    def __init__(self, program, classes: Dict[str, object], stats: Stats, header=None):
        from ghidra.program.model.data import CategoryPath
        self.program = program
        self.dtm = program.getDataTypeManager()
        self.classes = classes            # class name -> GhidraClass
        self.stats = stats
        self.header = header              # newtonrom.headers.HeaderTypes or None
        self.types_cat = CategoryPath("/Newton/types")
        self.func_cat = CategoryPath("/Newton/functions")
        self.ddk_cat = CategoryPath("/Newton/DDK")
        self._struct_cache: Dict[str, object] = {}
        self._typedef_cache: Dict[str, object] = {}
        self._enum_cache: Dict[str, object] = {}
        self._funcdef_cache: Dict[str, object] = {}
        self.pointer_size = program.getDefaultPointerSize()

    def builtin(self, name: str):
        import ghidra.program.model.data as gd
        cls = getattr(gd, BUILTIN_TYPES[name])
        return cls.dataType

    def has_record(self, name: str) -> bool:
        return self.header is not None and name in self.header.records

    def class_struct(self, name: str):
        """Structure for a class/struct name: DDK-defined if known, else a placeholder."""
        from ghidra.program.model.data import StructureDataType, UnionDataType
        from ghidra.program.model.listing import VariableUtilities
        if name in self._struct_cache:
            return self._struct_cache[name]
        rec = self.header.records.get(name) if self.header else None
        if name in self.classes:
            dt = VariableUtilities.findOrCreateClassStruct(self.classes[name], self.dtm)
            if rec is not None and dt.getLength() != rec["size"]:
                dt.replaceWith(StructureDataType(name, rec["size"]))
        elif rec is not None:
            dt = self.dtm.getDataType(self.ddk_cat, name)
            if dt is None:
                shell = (UnionDataType(self.ddk_cat, name, self.dtm) if rec["kind"] == "union"
                         else StructureDataType(self.ddk_cat, name, rec["size"], self.dtm))
                dt = self.dtm.addDataType(shell, None)
        else:
            dt = self.dtm.getDataType(self.types_cat, name)
            if dt is None:
                dt = self.dtm.addDataType(StructureDataType(self.types_cat, name, 0), None)
                self.stats.bump("opaque types created")
        self._struct_cache[name] = dt
        return dt

    def typedef(self, name: str):
        from ghidra.program.model.data import TypedefDataType
        if name in self._typedef_cache:
            return self._typedef_cache[name]
        dt = self.dtm.getDataType(self.ddk_cat, name)
        if dt is None:
            target, _ = self.convert(self.header.typedefs[name])
            dt = self.dtm.addDataType(TypedefDataType(self.ddk_cat, name, target, self.dtm), None)
            self.stats.bump("typedefs created")
        self._typedef_cache[name] = dt
        return dt

    def enum(self, name: str):
        from ghidra.program.model.data import EnumDataType
        if name in self._enum_cache:
            return self._enum_cache[name]
        dt = self.dtm.getDataType(self.ddk_cat, name)
        if dt is None:
            e = self.header.enums[name]
            dt = EnumDataType(self.ddk_cat, name, max(e["size"], 1), self.dtm)
            for vname, value in e["values"]:
                try:
                    dt.add(vname, value)
                except Exception:  # noqa: BLE001 - duplicate names/values in odd enums
                    pass
            dt = self.dtm.addDataType(dt, None)
            self.stats.bump("enums created")
        self._enum_cache[name] = dt
        return dt

    def named(self, name: str):
        """(DataType, sizeless) for a named type."""
        if name in BUILTIN_TYPES:
            return self.builtin(name), False
        if self.header is not None:
            if name in self.header.typedefs:
                return self.typedef(name), False
            if name in self.header.records:
                return self.class_struct(name), False
            if name in self.header.enums:
                return self.enum(name), False
        return self.class_struct(name), True

    def convert(self, t: dict):
        """Return (DataType, sizeless) - flag set when an unknown struct is used by value."""
        from ghidra.program.model.data import ArrayDataType, PointerDataType
        k = t["k"]
        if k == "named":
            return self.named(t["name"])
        if k in ("ptr", "ref"):
            inner, _ = self.convert(t["t"])
            return PointerDataType(inner, self.pointer_size, self.dtm), False
        if k == "qual":
            return self.convert(t["t"])
        if k == "array":
            inner, unk = self.convert(t["t"])
            n = max(t["n"], 1)
            if inner.getLength() <= 0:
                return inner, True
            return ArrayDataType(inner, n, inner.getLength(), self.dtm), unk
        if k == "func":
            return self.function_definition(t), False
        if k == "memptr":
            # cfront pointer-to-member-function: represented as a pointer to the
            # function definition (the real layout is a small struct).
            fn = self.function_definition(t["t"]) if t["t"]["k"] == "func" else self.builtin("void")
            return PointerDataType(fn, self.pointer_size, self.dtm), False
        raise ValueError(f"unknown type kind {k}")

    def function_definition(self, t: dict):
        """A FunctionDefinition data type for a `func` JSON type (pointer targets)."""
        from ghidra.program.model.data import FunctionDefinitionDataType, ParameterDefinitionImpl
        name = "fn_" + _type_signature(t)
        if name in self._funcdef_cache:
            return self._funcdef_cache[name]
        existing = self.dtm.getDataType(self.func_cat, name)
        if existing is not None:
            self._funcdef_cache[name] = existing
            return existing
        fd = FunctionDefinitionDataType(self.func_cat, name, self.dtm)
        ret, _ = self.convert(t["ret"]) if t.get("ret") else (self.builtin("void"), False)
        fd.setReturnType(ret)
        args = []
        for i, p in enumerate(t["params"]):
            if p == {"k": "named", "name": "..."}:
                fd.setVarArgs(True)
                continue
            dt, _ = self.convert(p)
            args.append(ParameterDefinitionImpl(f"param_{i + 1}", dt, None))
        fd.setArguments(args)
        if t.get("variadic"):
            fd.setVarArgs(True)
        fd = self.dtm.addDataType(fd, None)
        self._funcdef_cache[name] = fd
        self.stats.bump("function pointer types created")
        return fd


def create_header_types(program, mapper: TypeMapper, log: Log, stats: Stats) -> None:
    """Create every DDK record/enum/typedef, then fill in struct members."""
    from ghidra.program.model.data import PointerDataType, StructureDataType, UnionDataType, VoidDataType

    header = mapper.header
    # 1. shells with the right sizes, so members of struct type get correct lengths
    for name in header.records:
        mapper.class_struct(name)
    for name in header.enums:
        mapper.enum(name)
    for name in header.typedefs:
        try:
            mapper.typedef(name)
        except Exception as e:  # noqa: BLE001
            log(f"  typedef {name}: {e}")
    # 2. members
    void_ptr = PointerDataType(VoidDataType.dataType, mapper.pointer_size, mapper.dtm)
    for name, rec in header.records.items():
        existing = mapper.class_struct(name)
        origin = f"{rec['file']}:{rec['line']}"
        if rec["kind"] == "union":
            built = UnionDataType(existing.getCategoryPath(), name, mapper.dtm)
            for f in rec["fields"]:
                dt, _ = mapper.convert(f["type"])
                if dt.getLength() > 0:
                    built.add(dt, f["name"], None)
            existing.replaceWith(built)
            existing.setDescription(f"DDK {origin}")
            stats.bump("DDK unions filled")
            continue
        built = StructureDataType(existing.getCategoryPath(), name, rec["size"], mapper.dtm)
        notes = "; ".join(rec.get("warnings", []))
        if rec["introduces_vptr"]:
            built.replaceAtOffset(0, void_ptr, mapper.pointer_size, "__vptr", "vtable pointer")
        for b in rec["bases"]:
            base = header.records.get(b["name"])
            if base is None or b.get("offset") is None or (base["size"] <= 1 and not base["fields"]):
                continue
            bdt = mapper.class_struct(b["name"])
            if bdt.getLength() > 0:
                _place(built, b["offset"], bdt, "_base_" + b["name"], "base class", log, name)
        for f in rec["fields"]:
            dt, sizeless = mapper.convert(f["type"])
            if "bit_width" in f:
                _place_bitfield(built, f, dt, log, name)
                continue
            if sizeless or dt.getLength() <= 0:
                log(f"  {name}.{f['name']}: sizeless type, left undefined")
                continue
            _place(built, f["offset"], dt, f["name"], None, log, name)
        existing.replaceWith(built)
        existing.setDescription(f"DDK {origin}" + (f" ({notes})" if notes else ""))
        stats.bump("DDK structs filled")
    log(f"DDK types: {stats.get('DDK structs filled', 0)} structs, {stats.get('enums created', 0)} enums, "
        f"{stats.get('typedefs created', 0)} typedefs")


def _place(struct, offset: int, dt, name: str, comment, log: Log, owner: str) -> None:
    try:
        struct.replaceAtOffset(offset, dt, dt.getLength(), name, comment)
    except Exception as e:  # noqa: BLE001
        log(f"  {owner}.{name} at {offset}: {e}")


def _place_bitfield(struct, f: dict, dt, log: Log, owner: str) -> None:
    """clang reports big-endian bit offsets from the MSB of the record; Ghidra wants
    the offset from the LSB of the container.  Unverified against the ROM (see README)."""
    width = max(dt.getLength(), 1)
    bits = width * 8
    byte_offset = (f["bit_offset"] // bits) * width
    lsb_offset = bits - (f["bit_offset"] % bits) - f["bit_width"]
    try:
        struct.insertBitFieldAt(byte_offset, width, lsb_offset, dt, f["bit_width"], f["name"], None)
    except Exception as e:  # noqa: BLE001
        log(f"  {owner}.{f['name']} bitfield: {e}")


def _type_signature(t: dict) -> str:
    """Compact, filesystem-safe rendering of a JSON type for data type names."""
    k = t["k"]
    if k == "named":
        return t["name"].replace(" ", "_")
    if k == "ptr":
        return _type_signature(t["t"]) + "_p"
    if k == "ref":
        return _type_signature(t["t"]) + "_r"
    if k == "qual":
        return ("c" if t["const"] else "") + ("v" if t["volatile"] else "") + _type_signature(t["t"])
    if k == "array":
        return f"{_type_signature(t['t'])}_a{t['n']}"
    if k == "func":
        ret = _type_signature(t["ret"]) if t["ret"] else "void"
        return ret + "__" + "_".join(_type_signature(p) for p in t["params"])
    if k == "memptr":
        return t["cls"] + "_m" + _type_signature(t["t"])
    raise ValueError(k)


def create_classes(program, symbols: List[dict], log: Log, stats: Stats) -> Dict[str, object]:
    """Create a GhidraClass for every class scope mentioned by a C++ symbol."""
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.exception import DuplicateNameException, InvalidInputException

    st = program.getSymbolTable()
    global_ns = program.getGlobalNamespace()
    classes: Dict[str, object] = {}
    scopes = set()
    for s in symbols:
        d = s["demangled"]
        if d:
            for i in range(1, len(d["scope"]) + 1):
                scopes.add(tuple(d["scope"][:i]))
    for scope in sorted(scopes, key=len):
        parent = classes[scope[:-1]] if len(scope) > 1 else global_ns
        name = scope[-1]
        ns = st.getNamespace(name, parent)
        if ns is None:
            try:
                ns = st.createClass(parent, name, SourceType.IMPORTED)
                stats.bump("classes created")
            except (DuplicateNameException, InvalidInputException) as e:
                log(f"cannot create class {'::'.join(scope)}: {e}")
                continue
        classes[scope] = ns
    # flat lookup by innermost name for the type mapper (collisions are rare)
    flat = {scope[-1]: ns for scope, ns in classes.items()}
    flat.update({"::".join(scope): ns for scope, ns in classes.items()})
    return flat


# --------------------------------------------------------------------------
# Functions and labels
# --------------------------------------------------------------------------

def _looks_like_arm_code(rom: bytes, address: int, rom_size: int) -> bool:
    """Cheap test used only for plain (non-C++, non-jump-table) symbols."""
    if address & 3 or address + 4 > rom_size:
        return False
    w = int.from_bytes(rom[address:address + 4], "big")
    cond = w >> 28
    if cond == 0xF or w == 0:
        return False
    op = (w >> 25) & 7
    # data processing / load-store / branch / block transfer with the
    # "always" condition - what a function normally starts with
    return cond == 0xE and op in (0, 1, 2, 3, 4, 5)


def choose_function_entries(symbols: List[dict], jt: dict, rom: bytes, rom_size: int, stats: Stats):
    """Decide which symbol addresses get a function.

    Returns (entries, names_at) where names_at maps address -> list of symbol
    records, for every symbol whether or not it becomes a function.
    """
    names_at: Dict[int, List[dict]] = defaultdict(list)
    for s in symbols:
        names_at[s["address"]].append(s)
    targets = {t for _, t in jt["entries"]}
    entries = set(targets)
    stats.bump("functions: jump table targets", len(targets))
    for a, recs in names_at.items():
        if a in entries or a >= rom_size:
            continue
        if any(r["demangled"] and r["demangled"]["kind"] != "data" for r in recs):
            entries.add(a)
            stats.bump("functions: C++ symbols")
        elif _looks_like_arm_code(rom, a, rom_size):
            entries.add(a)
            stats.bump("functions: plain symbols that look like code")
    return entries, names_at


def create_functions(program, entries, monitor, log: Log, stats: Stats) -> None:
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SourceType

    space = program.getAddressFactory().getDefaultAddressSpace()
    entry_set = AddressSet()
    for a in entries:
        entry_set.add(space.getAddress(a))
    log(f"disassembling from {len(entries)} entry points ...")
    cmd = DisassembleCommand(entry_set, None, True)
    cmd.applyTo(program, monitor)
    fix_virtual_calls(program, monitor, log, stats, cmd.getDisassembledAddressSet())
    log("creating functions ...")
    CreateFunctionCmd(entry_set, SourceType.IMPORTED).applyTo(program, monitor)


def fix_virtual_calls(program, monitor, log: Log, stats: Stats, fresh=None) -> None:
    """Keep disassembling past virtual calls.

    A virtual call is `ldr rN,[this] / mov lr,pc / add pc,rN,#slot*4` (the
    vtable is an array of B instructions, see verify_types.py).  Ghidra treats
    `add pc,...` as a terminal jump and stops disassembling, truncating every
    function after its first virtual call.  Mark those instructions as
    call-with-return and continue at the next instruction, repeating until no
    new sites appear (newly disassembled code contains more virtual calls).
    """
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.listing import FlowOverride

    listing = program.getListing()
    rounds = 0
    while True:
        starts = AddressSet()
        prev = None
        # only the code disassembled since the last round needs scanning
        scan = listing.getInstructions(fresh, True) if fresh is not None else listing.getInstructions(True)
        for ins in scan:
            if monitor.isCancelled():
                raise RuntimeError("cancelled")
            text = str(ins)
            if (text.startswith("add pc,") and prev is not None and str(prev) == "mov lr,pc"
                    and prev.getAddress().add(4) == ins.getAddress()
                    and ins.getFlowOverride() != FlowOverride.CALL_RETURN):
                ins.setFlowOverride(FlowOverride.CALL_RETURN)
                stats.bump("virtual call sites marked call-return")
                nxt = ins.getAddress().add(4)
                if listing.getInstructionAt(nxt) is None:
                    starts.add(nxt)
            prev = ins
        rounds += 1
        if starts.isEmpty() or rounds > 50:
            break
        cmd = DisassembleCommand(starts, None, True)
        cmd.applyTo(program, monitor)
        fresh = cmd.getDisassembledAddressSet()
    log(f"virtual calls: {stats.get('virtual call sites marked call-return', 0)} sites, {rounds} rounds")


def ghidra_name(d: Optional[dict], raw: str) -> str:
    """Symbol name for Ghidra: demangled name, made valid (no spaces etc.)."""
    if not d:
        return raw
    name = d["name"]
    if name.startswith("operator "):
        # "operator new", "operator char *" -> operator_new, operator_char_ptr
        rest = name[len("operator "):].replace("*", " ptr").replace("&", " ref")
        name = "operator_" + "_".join(rest.split())
    return name


def _pick_primary(recs: List[dict]) -> dict:
    """Prefer a demangled C++ name, then a global, then the first symbol."""
    return sorted(recs, key=lambda r: (r["demangled"] is None, not r["global"], r["name"]))[0]


def apply_names(program, names_at: Dict[int, List[dict]], entries, classes: Dict[str, object],
                jt: dict, mapper: TypeMapper, monitor, log: Log, stats: Stats) -> None:
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.exception import DuplicateNameException, InvalidInputException

    st = program.getSymbolTable()
    fm = program.getFunctionManager()
    mem = program.getMemory()
    space = program.getAddressFactory().getDefaultAddressSpace()
    global_ns = program.getGlobalNamespace()
    header = mapper.header
    slot_addrs = {v for v, _ in jt["entries"]}

    def namespace_for(d: Optional[dict]):
        if d and d["scope"]:
            return classes.get("::".join(d["scope"])) or classes.get(d["scope"][-1]) or global_ns
        return global_ns

    total = len(names_at)
    for i, (a, recs) in enumerate(sorted(names_at.items())):
        if monitor.isCancelled():
            raise RuntimeError("cancelled")
        if i % 5000 == 0:
            log(f"  naming {i}/{total}")
        if a in slot_addrs:
            continue                       # thunks get their names from the target
        addr = space.getAddress(a)
        if not mem.contains(addr):
            stats.bump("symbols outside memory (skipped)")
            continue
        primary = _pick_primary(recs)
        d = primary["demangled"]
        name = ghidra_name(d, primary["name"])
        ns = namespace_for(d)
        func = fm.getFunctionAt(addr)
        if func is not None:
            try:
                func.setName(name, SourceType.IMPORTED)
                if ns != global_ns:
                    func.setParentNamespace(ns)
            except (DuplicateNameException, InvalidInputException) as e:
                log(f"  {a:#x} {name}: {e}")
            if d:
                proto = None
                if header is not None and d["kind"] != "data":
                    if d["scope"]:
                        proto = header.find_method(d["scope"][-1], d["name"], d["params"], d["static"])
                    else:
                        proto = header.find_function(d["name"], d["params"])
                _apply_signature(program, func, d, mapper, log, stats, proto)
                func.setComment(d["signature"] + "\n" + primary["name"])
            elif header is not None:
                proto = header.find_function(primary["name"])
                if proto is not None:
                    # C-linkage function declared in the DDK: the header is the whole prototype
                    cd = {"scope": [], "kind": "function", "static": True,
                          "params": [p["type"] for p in proto["params"]]}
                    if proto.get("variadic"):
                        cd["params"].append({"k": "named", "name": "..."})
                    _apply_signature(program, func, cd, mapper, log, stats, proto)
                    func.setComment(f"{proto['file']}:{proto['line']}")
            stats.bump("functions named")
        else:
            try:
                sym = st.createLabel(addr, name, ns, SourceType.IMPORTED)
                sym.setPrimary()
                stats.bump("labels created")
            except (DuplicateNameException, InvalidInputException) as e:
                log(f"  {a:#x} {name}: {e}")
        # secondary names at the same address become extra labels
        for r in recs:
            if r is primary:
                continue
            rd = r["demangled"]
            alt = ghidra_name(rd, r["name"])
            if alt == name and namespace_for(rd) == ns:
                continue
            try:
                st.createLabel(addr, alt, namespace_for(rd), SourceType.IMPORTED)
                stats.bump("alias labels")
            except (DuplicateNameException, InvalidInputException):
                pass


def _apply_signature(program, func, d: dict, mapper: TypeMapper, log: Log, stats: Stats,
                     proto: Optional[dict] = None) -> None:
    """Set parameters from the demangled type list when every type is representable.

    `proto` is the matching DDK declaration, if any; it supplies parameter names
    and the return type, which the mangled name does not encode.
    """
    from ghidra.program.model.listing import Function, ParameterImpl
    from ghidra.program.model.data import PointerDataType
    from ghidra.program.model.symbol import SourceType

    params = []
    by_value_unknown = False
    varargs = False
    names = [p["name"] for p in proto["params"]] if proto else []
    # the header's (typedef'd) spellings are equivalent and read better than the
    # mangled underlying types: ULong/Boolean instead of ulong/uchar
    header_types = [p["type"] for p in proto["params"]] if proto else []
    used = {"this"}
    if d["scope"] and d["kind"] != "data" and not d["static"]:
        this_dt = PointerDataType(mapper.class_struct(d["scope"][-1]), mapper.pointer_size, mapper.dtm)
        params.append(ParameterImpl("this", this_dt, program, SourceType.IMPORTED))
    for i, p in enumerate(d["params"] or []):
        if p == {"k": "named", "name": "..."}:
            varargs = True
            continue
        try:
            dt, unknown = mapper.convert(header_types[i] if i < len(header_types) else p)
        except Exception as e:  # noqa: BLE001 - keep going, this is best effort
            log(f"  {func.getName()}: cannot map parameter {i + 1}: {e}")
            stats.bump("signatures skipped (type error)")
            return
        by_value_unknown |= unknown
        name = names[i] if i < len(names) and names[i] and names[i] not in used else f"param_{i + 1}"
        used.add(name)
        params.append(ParameterImpl(name, dt, program, SourceType.IMPORTED))
    if by_value_unknown:
        # A class passed by value has an unknown size; a wrong size would shift
        # every later parameter, so leave the prototype for analysis to infer.
        stats.bump("signatures skipped (by-value class)")
        return
    from java.util import ArrayList
    jparams = ArrayList()
    for p in params:
        jparams.add(p)
    try:
        func.replaceParameters(jparams, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                               True, SourceType.IMPORTED)
        if varargs:
            func.setVarArgs(True)
        stats.bump("signatures applied")
    except Exception as e:  # noqa: BLE001
        log(f"  {func.getName()}: replaceParameters failed: {e}")
        stats.bump("signatures skipped (ghidra error)")
        return
    if proto is not None:
        try:
            ret = proto.get("ret")
            if ret is not None and d["kind"] not in ("ctor", "dtor"):
                dt, _ = mapper.convert(ret)
                func.setReturnType(dt, SourceType.IMPORTED)
            stats.bump("prototypes from DDK headers")
        except Exception as e:  # noqa: BLE001
            log(f"  {func.getName()}: return type: {e}")


def create_thunks(program, jt: dict, names_at: Dict[int, List[dict]], monitor, log: Log, stats: Stats) -> None:
    """Make every jump-table slot a thunk of its target function."""
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SourceType

    fm = program.getFunctionManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    slots = AddressSet()
    for v, _ in jt["entries"]:
        slots.add(space.getAddress(v))
    CreateFunctionCmd(slots, SourceType.IMPORTED).applyTo(program, monitor)
    for v, t in jt["entries"]:
        slot = fm.getFunctionAt(space.getAddress(v))
        target = fm.getFunctionAt(space.getAddress(t))
        if slot is None or target is None:
            stats.bump("thunks skipped")
            continue
        if target.getName().startswith("FUN_") and v in names_at:
            # a target without a symbol of its own: name it after its slot
            target.setName(names_at[v][0]["name"], SourceType.IMPORTED)
        slot.setThunkedFunction(target)
        stats.bump("thunks created")
    log(f"thunks: {stats.get('thunks created', 0)} created, {stats.get('thunks skipped', 0)} skipped")


# --------------------------------------------------------------------------
# ROM facts recovered by verify_types.py (class sizes, vtables)
# --------------------------------------------------------------------------

def apply_romfacts(program, facts: dict, mapper: TypeMapper, classes: Dict[str, object],
                   monitor, log: Log, stats: Stats) -> None:
    """Apply romfacts.json: true class sizes, vtable labels/types, recovered names."""
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.data import CategoryPath, PointerDataType, StructureDataType, UnsignedIntegerDataType
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.exception import DuplicateNameException, InvalidInputException

    st = program.getSymbolTable()
    fm = program.getFunctionManager()
    mem = program.getMemory()
    space = program.getAddressFactory().getDefaultAddressSpace()
    addr = space.getAddress

    # 1. sizes: the constructor's allocation is the truth
    for cls, size in facts.get("sizes", {}).items():
        if cls not in classes and not mapper.has_record(cls):
            continue
        dt = mapper.class_struct(cls)
        cur = dt.getLength()
        if cur == size or size <= 0:
            continue
        if dt.getNumDefinedComponents() == 0 or cur <= 1:
            dt.replaceWith(StructureDataType(cls, size))
            stats.bump("class sizes set from ROM")
        elif size > cur:
            dt.growStructure(size - cur)
            dt.setDescription((dt.getDescription() or "") + f" [ROM allocates {size:#x}, header {cur:#x}]")
            stats.bump("class sizes grown to ROM size")
        else:
            dt.setDescription((dt.getDescription() or "") + f" [ROM allocates {size:#x}, header {cur:#x}]")
            stats.bump("class sizes smaller in ROM (kept header)")

    # 2. vtables: label, entry thunks, a struct type naming the slots
    vt_cat = CategoryPath("/Newton/vtables")
    all_vt = sorted({a for addrs in facts.get("vtables", {}).values() for a in addrs})
    next_vt = {a: (all_vt[i + 1] if i + 1 < len(all_vt) else None) for i, a in enumerate(all_vt)}
    ro_end = mem.getBlock("ROM_RO").getEnd().getOffset()
    slot_dt = UnsignedIntegerDataType.dataType
    entry_set = AddressSet()
    entry_targets = {}
    for cls, addrs in facts.get("vtables", {}).items():
        ns = classes.get(cls)
        for n, vt in enumerate(sorted(addrs)):
            label = "vtable" if n == 0 else f"vtable_{n + 1}"
            try:
                st.createLabel(addr(vt), label, ns or program.getGlobalNamespace(), SourceType.IMPORTED)
            except (DuplicateNameException, InvalidInputException):
                pass
            # entries run until the next known vtable or the first non-branch word
            limit = next_vt.get(vt) or ro_end
            names = []
            a = vt
            while a < limit and len(names) < 256:
                w = mem.getInt(addr(a)) & 0xFFFFFFFF
                if w >> 24 != 0xEA:
                    break
                imm = w & 0xFFFFFF
                imm -= 0x1000000 if imm & 0x800000 else 0
                target = a + 8 + imm * 4
                fn = fm.getFunctionAt(addr(target))
                if fn is not None and fn.isThunk():
                    fn = fn.getThunkedFunction(True)
                names.append(fn.getName() if fn is not None else f"slot_{len(names)}")
                entry_set.add(addr(a))
                entry_targets[a] = target
                a += 4
            if not names:
                continue
            vt_name = f"{cls}_vtbl" if n == 0 else f"{cls}_vtbl_{n + 1}"
            vtbl = mapper.dtm.getDataType(vt_cat, vt_name)
            if vtbl is None:
                vtbl = StructureDataType(vt_cat, vt_name, 0, mapper.dtm)
                used = set()
                for i, name in enumerate(names):
                    field = name if name not in used else f"{name}_{i}"
                    used.add(field)
                    vtbl.add(slot_dt, 4, field, f"slot {i}: B {name}")
                vtbl.setDescription(f"vtable of {cls} at {vt:#x} ({len(names)} entries, B instructions)")
                vtbl = mapper.dtm.addDataType(vtbl, None)
            if n == 0 and (cls in classes or mapper.has_record(cls)):
                cdt = mapper.class_struct(cls)
                if cdt.getLength() >= 4:
                    cdt.replaceAtOffset(0, PointerDataType(vtbl, mapper.pointer_size, mapper.dtm),
                                        mapper.pointer_size, "__vptr", "vtable pointer")
            stats.bump("vtables labelled")
    if not entry_set.isEmpty():
        CreateFunctionCmd(entry_set, SourceType.IMPORTED).applyTo(program, monitor)
        for a, target in entry_targets.items():
            slot = fm.getFunctionAt(addr(a))
            tgt = fm.getFunctionAt(addr(target))
            if slot is not None and tgt is not None and slot != tgt:
                slot.setThunkedFunction(tgt)
                stats.bump("vtable entry thunks")

    # 3. functions named through vtable slots (no symbol of their own)
    for a_text, qualified in facts.get("functions", {}).items():
        a = int(a_text, 0)
        fn = fm.getFunctionAt(addr(a))
        if fn is None:
            continue
        cls, _, name = qualified.rpartition("::")
        try:
            fn.setName(name, SourceType.IMPORTED)
            if cls in classes:
                fn.setParentNamespace(classes[cls])
            stats.bump("functions named from vtables")
        except (DuplicateNameException, InvalidInputException):
            pass
    log(f"ROM facts: {stats.get('class sizes set from ROM', 0)} sizes set, "
        f"{stats.get('vtables labelled', 0)} vtables, {stats.get('vtable entry thunks', 0)} entry thunks")


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------

def apply(program, layout: dict, symbols: dict, rom: bytes, monitor, log: Log = print,
          types: Optional[dict] = None, romfacts: Optional[dict] = None) -> Stats:
    """Run the whole import against an open program inside a transaction.

    `types` is the optional types.json from parse_headers.py; with it, DDK
    structs/enums/typedefs are created and header prototypes are applied.
    `romfacts` is the optional romfacts.json from verify_types.py (class
    sizes and vtables observed in the ROM).
    """
    stats = Stats()
    jt = symbols["jumptable"]
    rom_size = sum(r["size"] for r in layout["regions"] if r["kind"] == "rom")

    setup_memory(program, layout, rom, monitor, log)
    create_jump_table_block(program, jt, rom, monitor, log)

    classes = create_classes(program, symbols["symbols"], log, stats)
    log(f"classes: {stats.get('classes created', 0)}")
    header = None
    if types is not None:
        from .headers import HeaderTypes
        header = HeaderTypes(types)
    mapper = TypeMapper(program, classes, stats, header)
    if header is not None:
        create_header_types(program, mapper, log, stats)

    entries, names_at = choose_function_entries(symbols["symbols"], jt, rom, rom_size, stats)
    create_functions(program, entries, monitor, log, stats)
    apply_names(program, names_at, entries, classes, jt, mapper, monitor, log, stats)
    create_thunks(program, jt, names_at, monitor, log, stats)
    if romfacts is not None:
        apply_romfacts(program, romfacts, mapper, classes, monitor, log, stats)

    for key in sorted(stats):
        log(f"  {key}: {stats[key]}")
    return stats


def load_inputs(build_dir: str):
    import os
    with open(os.path.join(build_dir, "layout.json")) as f:
        layout = json.load(f)
    with open(os.path.join(build_dir, "symbols.json")) as f:
        symbols = json.load(f)
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    types = romfacts = None
    types_path = os.path.join(build_dir, "types.json")
    if os.path.exists(types_path):
        with open(types_path) as f:
            types = json.load(f)
    facts_path = os.path.join(build_dir, "romfacts.json")
    if os.path.exists(facts_path):
        with open(facts_path) as f:
            romfacts = json.load(f)
    return layout, symbols, rom, types, romfacts
