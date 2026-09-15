"""Demangler for the cfront / ARM C++ name mangling used by the Newton toolchain.

Newton OS was built with Apple's ARM C++ compiler, whose mangling follows the
"ARM Annotated C++ Reference Manual" (cfront) scheme:

    <name>__<class><qualifiers>F<args>          member function
    <name>__F<args>                             free function
    __ct__<class>F<args> / __dt__<class>F<args> constructor / destructor
    __<op>__<class>F<args>                      operator (see OPERATORS)
    __op<type>__<class>F<args>                  conversion operator
    <name>__<class>                             static data member

`<class>` is `<len><identifier>` or `Q<n><len><id>...` for nested scopes, and
`C` before the `F` marks a const member function.  Types are prefix-encoded:

    v c s i l x f d r e b w      builtins (void ... wchar_t; `e` is "...")
    U S C V P R                  unsigned/signed/const/volatile/pointer/reference
    A<n>_<type>                  array of n
    F<args>_<ret>                function type (used after P for pointers)
    M<class>F<args>_<ret>        pointer to member function
    <len><identifier>            class / typedef name
    T<n>  N<count><n>            back-reference to parameter n (1-based)

Back-reference numbering
------------------------
Parameters are numbered per parameter list.  A nested list (inside `F...`)
starts with a copy of the enclosing list's parameters seen so far, appends its
own parameters to that copy, and the copy is discarded when the nested list
ends; the function type itself is then appended to the enclosing list.  This
was established empirically from the ROM symbols and cross-checked against
header declarations (e.g. `TMonitor::Init` in headers/OS600/UserMonitor.h):

    Init__8TMonitorFPFPvUlT1_vUlPvP12TEnvironmentUcT2T5
        inner list: [void*, ulong], T1 -> void*
        outer list: [fnptr, ulong, void*, TEnvironment*, uchar], T2 -> ulong, T5 -> uchar
    GetPicBits__FlP7PicPlayPCPFT1T2P8GrafPort_v
        inner list inherits [long, PicPlay*], so T1 -> long, T2 -> PicPlay*

The only pointer-to-member symbols in the ROMs
(`ReadWrite__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc`) are consistent
only if the member class is pushed as the first entry of the nested list and the
nested entries are *kept* afterwards; that is what `M` does here.

GNU libiberty's ARM mode (used by tools/mpdumper) records nothing inside nested
lists, which is why it fails on the examples above.  Its output is otherwise
used as the reference for the test-suite (tests/test_demangle.py).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional, Sequence, Union

__all__ = ["demangle", "Demangled", "DemangleError", "Type", "type_to_str", "type_to_json"]


class DemangleError(ValueError):
    pass


# --------------------------------------------------------------------------
# Type representation
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Named:
    name: str


@dataclass(frozen=True)
class Pointer:
    target: "Type"


@dataclass(frozen=True)
class Reference:
    target: "Type"


@dataclass(frozen=True)
class Qualified:
    target: "Type"
    const: bool = False
    volatile: bool = False


@dataclass(frozen=True)
class Array:
    element: "Type"
    count: int


@dataclass(frozen=True)
class Function:
    params: tuple
    returns: Optional["Type"]      # None for the top-level function (not encoded)


@dataclass(frozen=True)
class MemberPointer:
    cls: str
    target: "Type"                 # normally a Function


Type = Union[Named, Pointer, Reference, Qualified, Array, Function, MemberPointer]


def _qual_str(t: Qualified) -> str:
    parts = []
    if t.const:
        parts.append("const")
    if t.volatile:
        parts.append("volatile")
    return " ".join(parts)


def _render(t: Type, decl: str) -> str:
    """Render type `t` around the declarator string `decl` (C declaration style)."""
    if isinstance(t, Named):
        return t.name + (" " + decl if decl else "")
    if isinstance(t, Qualified):
        inner = t.target
        q = _qual_str(t)
        if isinstance(inner, (Pointer, Reference, MemberPointer)):
            # `CPc` = char *const
            return _render(inner, q + (" " + decl if decl else ""))
        # `Cc` = char const, `C5TRect` = TRect const
        return _render_named_qual(inner, q, decl)
    if isinstance(t, Pointer):
        return _render(t.target, _wrap_ptr("*" + decl, t.target))
    if isinstance(t, Reference):
        return _render(t.target, _wrap_ptr("&" + decl, t.target))
    if isinstance(t, MemberPointer):
        return _render(t.target, _wrap_ptr(t.cls + "::*" + decl, t.target))
    if isinstance(t, Array):
        return _render(t.element, f"{decl}[{t.count}]")
    if isinstance(t, Function):
        params = ", ".join(type_to_str(p) for p in t.params) or "void"
        sig = f"{decl}({params})"
        if t.returns is None:
            return sig
        return _render(t.returns, sig)
    raise TypeError(t)


def _render_named_qual(inner: Type, q: str, decl: str) -> str:
    # libiberty style: "unsigned char const *", "TRect const &"
    if isinstance(inner, Named):
        return inner.name + " " + q + (" " + decl if decl else "")
    # qualified array / function (unusual) - fall back to prefix form
    return q + " " + _render(inner, decl)


def _wrap_ptr(decl: str, target: Type) -> str:
    """Pointers/references to functions or arrays need parentheses."""
    if isinstance(target, (Function, Array)):
        return "(" + decl + ")"
    return decl


def type_to_str(t: Type) -> str:
    return _render(t, "")


def type_to_json(t: Type) -> dict:
    """Structured form of a type for symbols.json (consumed by the Ghidra script)."""
    if isinstance(t, Named):
        return {"k": "named", "name": t.name}
    if isinstance(t, Pointer):
        return {"k": "ptr", "t": type_to_json(t.target)}
    if isinstance(t, Reference):
        return {"k": "ref", "t": type_to_json(t.target)}
    if isinstance(t, Qualified):
        return {"k": "qual", "const": t.const, "volatile": t.volatile, "t": type_to_json(t.target)}
    if isinstance(t, Array):
        return {"k": "array", "n": t.count, "t": type_to_json(t.element)}
    if isinstance(t, Function):
        return {"k": "func", "params": [type_to_json(p) for p in t.params],
                "ret": type_to_json(t.returns) if t.returns is not None else None}
    if isinstance(t, MemberPointer):
        return {"k": "memptr", "cls": t.cls, "t": type_to_json(t.target)}
    raise TypeError(t)


# --------------------------------------------------------------------------
# Result
# --------------------------------------------------------------------------

@dataclass
class Demangled:
    mangled: str
    scope: List[str]                        # enclosing classes, outermost first
    name: str                               # e.g. "Init", "TFoo", "~TFoo", "operator="
    kind: str                               # function|ctor|dtor|operator|conversion|data
    params: Optional[List[Type]] = None     # None for data symbols
    is_const: bool = False
    is_static: bool = False
    conversion_type: Optional[Type] = None

    @property
    def qualified_name(self) -> str:
        return "::".join(self.scope + [self.name])

    @property
    def class_name(self) -> Optional[str]:
        return self.scope[-1] if self.scope else None

    @property
    def signature(self) -> str:
        """Full libiberty-style text, e.g. 'TFoo::Init(unsigned long) const'."""
        if self.params is None:
            return self.qualified_name
        params = ", ".join(type_to_str(p) for p in self.params) or "void"
        s = f"{self.qualified_name}({params})"
        if self.is_const:
            s += " const"
        if self.is_static:
            s += " static"
        return s

    def param_strings(self) -> List[str]:
        return [type_to_str(p) for p in (self.params or [])]

    def to_json(self) -> dict:
        return {
            "signature": self.signature,
            "scope": self.scope,
            "name": self.name,
            "kind": self.kind,
            "params": [type_to_json(p) for p in self.params] if self.params is not None else None,
            "const": self.is_const,
            "static": self.is_static,
        }


# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------

BUILTINS = {
    "v": "void", "c": "char", "s": "short", "i": "int", "l": "long",
    "x": "long long", "f": "float", "d": "double", "r": "long double",
    "e": "...", "b": "bool", "w": "wchar_t",
}

# cfront operator codes (libiberty `optable`, restricted to the codes cfront emits)
OPERATORS = {
    "nw": " new", "dl": " delete", "vn": " new []", "vd": " delete []",
    "as": "=", "ne": "!=", "eq": "==", "ge": ">=", "gt": ">", "le": "<=", "lt": "<",
    "pl": "+", "apl": "+=", "mi": "-", "ami": "-=", "ml": "*", "amu": "*=",
    "md": "%", "amd": "%=", "dv": "/", "adv": "/=", "aa": "&&", "oo": "||",
    "nt": "!", "pp": "++", "mm": "--", "or": "|", "aor": "|=", "er": "^",
    "aer": "^=", "ad": "&", "aad": "&=", "co": "~", "cl": "()", "ls": "<<",
    "als": "<<=", "rs": ">>", "ars": ">>=", "rf": "->", "vc": "[]", "cm": ",",
    "cn": "?:", "mx": ">?", "mn": "<?", "rm": "->*",
}

class _Parser:
    def __init__(self, text: str):
        self.s = text
        self.i = 0

    # -- low level --------------------------------------------------------
    def peek(self, n: int = 1) -> str:
        return self.s[self.i:self.i + n]

    def at_end(self) -> bool:
        return self.i >= len(self.s)

    def take(self) -> str:
        if self.at_end():
            raise DemangleError("unexpected end of name")
        c = self.s[self.i]
        self.i += 1
        return c

    def expect(self, c: str) -> None:
        if self.peek(len(c)) != c:
            raise DemangleError(f"expected {c!r} at {self.i} in {self.s!r}")
        self.i += len(c)

    def digits(self) -> int:
        j = self.i
        while j < len(self.s) and self.s[j].isdigit():
            j += 1
        if j == self.i:
            raise DemangleError(f"expected count at {self.i} in {self.s!r}")
        n = int(self.s[self.i:j])
        self.i = j
        return n

    def one_digit(self) -> int:
        c = self.take()
        if not c.isdigit():
            raise DemangleError(f"expected digit at {self.i - 1} in {self.s!r}")
        return int(c)

    # -- names --------------------------------------------------------------
    def class_name(self) -> str:
        """<len><identifier> or Q<n><len><id>..."""
        if self.peek() == "Q":
            self.i += 1
            n = self.one_digit()
            return "::".join(self.sized_identifier() for _ in range(n))
        return self.sized_identifier()

    def sized_identifier(self) -> str:
        n = self.digits()
        ident = self.s[self.i:self.i + n]
        if len(ident) != n:
            raise DemangleError(f"identifier overruns end of {self.s!r}")
        self.i += n
        return ident

    # -- types --------------------------------------------------------------
    def type(self, table: List[Type]) -> Type:
        c = self.take()
        if c == "P":
            return Pointer(self.type(table))
        if c == "R":
            return Reference(self.type(table))
        if c in "CV":
            const, volatile = c == "C", c == "V"
            while self.peek() in ("C", "V"):
                q = self.take()
                const |= q == "C"
                volatile |= q == "V"
            return Qualified(self.type(table), const, volatile)
        if c in "US":
            return self._signed(c, self.type(table))
        if c == "A":
            n = self.digits()
            self.expect("_")
            return Array(self.type(table), n)
        if c == "F":
            return self.function_type(table, prefix=table)
        if c == "M":
            cls = self.class_name()
            self.expect("F")
            # Pointer to member: the class acts as the first (this) entry of the
            # nested list and the nested entries persist (see module docstring).
            table.append(Named(cls))
            fn = self.function_type(table, prefix=table, persist=True)
            return MemberPointer(cls, fn)
        if c in BUILTINS:
            return Named(BUILTINS[c])
        if c.isdigit() or c == "Q":
            self.i -= 1
            return Named(self.class_name())
        raise DemangleError(f"unknown type code {c!r} at {self.i - 1} in {self.s!r}")

    def _signed(self, code: str, t: Type) -> Type:
        """Apply U/S to a builtin integer, looking through cv-qualifiers (`UVl`)."""
        word = "unsigned" if code == "U" else "signed"
        if isinstance(t, Qualified):
            # `UVl`: qualifiers inside the sign modifier render prefix-style
            inner = self._signed(code, t.target)
            return Named(_qual_str(t) + " " + inner.name)
        if isinstance(t, Named) and t.name in ("char", "short", "int", "long", "long long"):
            return Named(word + " " + t.name)
        raise DemangleError(f"{word} applied to non-integer type in {self.s!r}")

    def function_type(self, outer: List[Type], prefix: Sequence[Type],
                      persist: bool = False) -> Function:
        """Parse `<args>_<ret>` for a nested function type (the F is consumed)."""
        table = outer if persist else list(prefix)
        params = self.args(table, terminated=True)
        self.expect("_")
        ret = self.type(table)
        if params == [Named("void")]:
            params = []
        return Function(tuple(params), ret)

    def args(self, table: List[Type], terminated: bool) -> List[Type]:
        """Parse a parameter list, appending each parameter to `table`.

        `terminated` lists end at '_' (nested function types); the top-level
        list runs to the end of the string.
        """
        params: List[Type] = []
        while not self.at_end() and not (terminated and self.peek() == "_"):
            c = self.peek()
            if c in "TN":
                self.i += 1
                count = 1 if c == "T" else self.one_digit()
                index = self._ref_index(len(table))
                if not 1 <= index <= len(table):
                    raise DemangleError(
                        f"back-reference {c}{index} out of range at {self.i} in {self.s!r}")
                for _ in range(count):
                    params.append(table[index - 1])
                    table.append(table[index - 1])
            else:
                t = self.type(table)
                params.append(t)
                table.append(t)
        return params

    def _ref_index(self, ntypes: int) -> int:
        # libiberty rule: once ten or more parameters exist an index may have
        # two digits; otherwise it is always a single digit.
        if ntypes >= 10:
            j = self.i
            while j < len(self.s) and self.s[j].isdigit():
                j += 1
            n = int(self.s[self.i:j])
            if n <= ntypes:
                self.i = j
                return n
        return self.one_digit()


def _split_name(mangled: str):
    """Return (basename, rest) where rest starts after the '__' separator.

    Special names start with '__' themselves (__ct, __dt, __op<type>, __<op>),
    so the separator is searched for after the first two characters.  Plain
    names may also contain '__' (e.g. `Decision_GU_or_O___FP13CrossInfoType`),
    so the first '__' whose remainder can start a scope or parameter list wins.
    """
    start = 2 if mangled.startswith("__") else 1
    sep = mangled.find("__", start)
    while sep >= 0:
        rest = mangled[sep + 2:]
        if rest[:1].isdigit() or rest[:1] in ("Q", "F") or rest[:2] in ("CF", "SF"):
            return mangled[:sep], rest
        sep = mangled.find("__", sep + 1)
    return None


def demangle(mangled: str) -> Optional[Demangled]:
    """Demangle a symbol name; returns None if it is not a C++ mangled name."""
    parts = _split_name(mangled)
    if parts is None:
        return None
    base, rest = parts
    if not base or not rest:
        return None
    try:
        return _demangle(mangled, base, rest)
    except DemangleError:
        return None


def _demangle(mangled: str, base: str, rest: str) -> Optional[Demangled]:
    p = _Parser(rest)

    # scope: <class> or Q<n>... - present unless the function is free (F follows)
    scope: List[str] = []
    if p.peek() and (p.peek().isdigit() or p.peek() == "Q"):
        scope = p.class_name().split("::")

    kind = "function"
    name = base
    conversion: Optional[Type] = None
    if base.startswith("__"):
        code = base[2:]
        if code == "ct":
            kind, name = "ctor", scope[-1] if scope else None
        elif code == "dt":
            kind, name = "dtor", ("~" + scope[-1]) if scope else None
        elif code.startswith("op") and len(code) > 2:
            kind = "conversion"
            tp = _Parser(code[2:])
            conversion = tp.type([])
            if not tp.at_end():
                raise DemangleError("trailing data in conversion operator type")
            name = "operator " + type_to_str(conversion)
        elif code in OPERATORS:
            kind, name = "operator", "operator" + OPERATORS[code]
        # otherwise an ordinary identifier that happens to start with '__'
        if name is None:
            raise DemangleError("constructor/destructor without a class")

    if p.at_end():
        if not scope or kind != "function":
            raise DemangleError("expected parameter list")
        return Demangled(mangled, scope, name, "data")

    is_const = is_static = False
    if p.peek(2) == "CF":
        is_const = True
        p.i += 1
    elif p.peek(2) == "SF":      # static member function
        is_static = True
        p.i += 1
    p.expect("F")
    params = p.args([], terminated=False)
    if not p.at_end():
        raise DemangleError("trailing data after parameter list")
    if len(params) == 1 and params[0] == Named("void"):
        params = []
    return Demangled(mangled, scope, name, kind, params, is_const, is_static, conversion)
