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
    """Turn the demangler's JSON types into Ghidra data types."""

    def __init__(self, program, classes: Dict[str, object], stats: Stats):
        from ghidra.program.model.data import CategoryPath
        self.program = program
        self.dtm = program.getDataTypeManager()
        self.classes = classes            # class name -> GhidraClass
        self.stats = stats
        self.types_cat = CategoryPath("/Newton/types")
        self.func_cat = CategoryPath("/Newton/functions")
        self._struct_cache: Dict[str, object] = {}
        self._funcdef_cache: Dict[str, object] = {}
        self.pointer_size = program.getDefaultPointerSize()

    def builtin(self, name: str):
        import ghidra.program.model.data as gd
        cls = getattr(gd, BUILTIN_TYPES[name])
        return cls.dataType

    def class_struct(self, name: str):
        """Structure standing for a C++ class (empty until members are recovered)."""
        from ghidra.program.model.data import StructureDataType
        from ghidra.program.model.listing import VariableUtilities
        if name in self._struct_cache:
            return self._struct_cache[name]
        if name in self.classes:
            dt = VariableUtilities.findOrCreateClassStruct(self.classes[name], self.dtm)
        else:
            dt = self.dtm.getDataType(self.types_cat, name)
            if dt is None:
                dt = self.dtm.addDataType(StructureDataType(self.types_cat, name, 0), None)
                self.stats.bump("opaque types created")
        self._struct_cache[name] = dt
        return dt

    def convert(self, t: dict):
        """Return (DataType, by_value_unknown) - flag set when a sizeless struct is passed by value."""
        from ghidra.program.model.data import ArrayDataType, PointerDataType
        k = t["k"]
        if k == "named":
            if t["name"] in BUILTIN_TYPES:
                return self.builtin(t["name"]), False
            return self.class_struct(t["name"]), True
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
        ret, _ = self.convert(t["ret"]) if t["ret"] else (self.builtin("void"), False)
        fd.setReturnType(ret)
        args = []
        for i, p in enumerate(t["params"]):
            if p == {"k": "named", "name": "..."}:
                fd.setVarArgs(True)
                continue
            dt, _ = self.convert(p)
            args.append(ParameterDefinitionImpl(f"param_{i + 1}", dt, None))
        fd.setArguments(args)
        fd = self.dtm.addDataType(fd, None)
        self._funcdef_cache[name] = fd
        self.stats.bump("function pointer types created")
        return fd


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


def create_functions(program, entries, monitor, log: Log) -> None:
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SourceType

    space = program.getAddressFactory().getDefaultAddressSpace()
    entry_set = AddressSet()
    for a in entries:
        entry_set.add(space.getAddress(a))
    log(f"disassembling from {len(entries)} entry points ...")
    DisassembleCommand(entry_set, None, True).applyTo(program, monitor)
    log("creating functions ...")
    CreateFunctionCmd(entry_set, SourceType.IMPORTED).applyTo(program, monitor)


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
                jt: dict, monitor, log: Log, stats: Stats) -> None:
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.exception import DuplicateNameException, InvalidInputException

    st = program.getSymbolTable()
    fm = program.getFunctionManager()
    mem = program.getMemory()
    space = program.getAddressFactory().getDefaultAddressSpace()
    global_ns = program.getGlobalNamespace()
    mapper = TypeMapper(program, classes, stats)
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
                _apply_signature(program, func, d, mapper, log, stats)
                func.setComment(d["signature"] + "\n" + primary["name"])
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


def _apply_signature(program, func, d: dict, mapper: TypeMapper, log: Log, stats: Stats) -> None:
    """Set parameters from the demangled type list when every type is representable."""
    from ghidra.program.model.listing import Function, ParameterImpl
    from ghidra.program.model.data import PointerDataType
    from ghidra.program.model.symbol import SourceType

    params = []
    by_value_unknown = False
    varargs = False
    if d["scope"] and d["kind"] != "data" and not d["static"]:
        this_dt = PointerDataType(mapper.class_struct(d["scope"][-1]), mapper.pointer_size, mapper.dtm)
        params.append(ParameterImpl("this", this_dt, program, SourceType.IMPORTED))
    for i, p in enumerate(d["params"] or []):
        if p == {"k": "named", "name": "..."}:
            varargs = True
            continue
        try:
            dt, unknown = mapper.convert(p)
        except Exception as e:  # noqa: BLE001 - keep going, this is best effort
            log(f"  {func.getName()}: cannot map parameter {i + 1}: {e}")
            stats.bump("signatures skipped (type error)")
            return
        by_value_unknown |= unknown
        params.append(ParameterImpl(f"param_{i + 1}", dt, program, SourceType.IMPORTED))
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
# Driver
# --------------------------------------------------------------------------

def apply(program, layout: dict, symbols: dict, rom: bytes, monitor, log: Log = print) -> Stats:
    """Run the whole import against an open program inside a transaction."""
    stats = Stats()
    jt = symbols["jumptable"]
    rom_size = sum(r["size"] for r in layout["regions"] if r["kind"] == "rom")

    setup_memory(program, layout, rom, monitor, log)
    create_jump_table_block(program, jt, rom, monitor, log)

    classes = create_classes(program, symbols["symbols"], log, stats)
    log(f"classes: {stats.get('classes created', 0)}")

    entries, names_at = choose_function_entries(symbols["symbols"], jt, rom, rom_size, stats)
    create_functions(program, entries, monitor, log)
    apply_names(program, names_at, entries, classes, jt, monitor, log, stats)
    create_thunks(program, jt, names_at, monitor, log, stats)

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
    return layout, symbols, rom
