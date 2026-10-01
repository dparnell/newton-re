#!/usr/bin/env python3
"""NSOF (Newton Streamed Object Format), for a desktop's side of the dock.

Purpose
    The dock protocol carries NewtonScript objects as NSOF: a version byte
    (2), then the object as tagged data.  This reads and writes the forms
    a desktop meets - integers, true/nil, characters, strings, symbols,
    binaries, arrays and frames - with precedents (a back-reference to an
    object already in the stream) on reading.  It is written from the
    format as the reconstruction's src/stores/ObjectStreamer.cpp reads and
    writes it (Newton Formats, "Newton Streamed Object Format").

Usage
    from nsof import encode, decode, Symbol
    data = encode({Symbol("name"): "Internal", Symbol("signature"): 1234})
    obj = decode(data)

Inputs / outputs
    Python values: int, bool/None (true/nil), str (a string), Symbol (a
    symbol), bytes (a binary of class 'binary), Binary(klass, data), list
    (a plain array), Array(klass, items) (an array with a class),
    Immediate(ref) (an immediate written as it is), dict with Symbol keys
    (a frame; insertion order is the slot order).  A character decodes as
    a one-character Char.
"""

import struct

VERSION = 2
IMMEDIATE, CHARACTER, UNICHAR, BINARY, ARRAY, PLAIN_ARRAY, FRAME, SYMBOL, STRING, PRECEDENT, NIL, SMALL_RECT, LARGE_BINARY = range(13)


class Symbol(str):
    """A NewtonScript symbol (compared case-insensitively on the Newton)."""
    def __repr__(self):
        return "'" + str.__str__(self)


class Char(str):
    pass


class Immediate:
    """An immediate Ref written as it is - e.g. a function's class, the
    immediate 0x32 (kFuncClass)."""
    def __init__(self, ref):
        self.ref = ref


class Array:
    """An array with a class of its own (a function's 'literals)."""
    def __init__(self, klass, items):
        self.klass = klass
        self.items = list(items)


class Binary:
    def __init__(self, klass, data):
        self.klass = klass
        self.data = bytes(data)

    def __repr__(self):
        return "<binary %s, %d bytes>" % (self.klass, len(self.data))


def _xlong(n):
    return bytes([n]) if 0 <= n < 255 else b"\xff" + struct.pack(">i", n)


def _encode(obj, out):
    if obj is None or obj is False:
        out.append(NIL)
    elif obj is True:
        out.append(IMMEDIATE)
        out += _xlong(0x1a)
    elif isinstance(obj, int):
        out.append(IMMEDIATE)
        out += _xlong((obj << 2) & 0xFFFFFFFF if obj >= 0 else ((obj << 2) & 0xFFFFFFFF) - (1 << 32))
    elif isinstance(obj, Symbol):
        name = str.__str__(obj).encode("mac_roman")
        out.append(SYMBOL)
        out += _xlong(len(name)) + name
    elif isinstance(obj, Char):
        c = ord(obj)
        if c < 256:
            out += bytes([CHARACTER, c])
        else:
            out += bytes([UNICHAR]) + struct.pack(">H", c)
    elif isinstance(obj, str):
        data = (obj + "\0").encode("utf-16-be")
        out.append(STRING)
        out += _xlong(len(data)) + data
    elif isinstance(obj, (bytes, bytearray)):
        _encode(Binary(Symbol("binary"), obj), out)
    elif isinstance(obj, Binary):
        out.append(BINARY)
        out += _xlong(len(obj.data))
        _encode(obj.klass, out)
        out += obj.data
    elif isinstance(obj, Immediate):
        out.append(IMMEDIATE)
        out += _xlong(obj.ref)
    elif isinstance(obj, Array):
        out.append(ARRAY)
        out += _xlong(len(obj.items))
        _encode(obj.klass, out)
        for item in obj.items:
            _encode(item, out)
    elif isinstance(obj, (list, tuple)):
        out.append(PLAIN_ARRAY)
        out += _xlong(len(obj))
        for item in obj:
            _encode(item, out)
    elif isinstance(obj, dict):
        out.append(FRAME)
        out += _xlong(len(obj))
        for key in obj:
            _encode(Symbol(key), out)
        for value in obj.values():
            _encode(value, out)
    else:
        raise TypeError("cannot stream %r" % (obj,))


def encode(obj):
    out = bytearray([VERSION])
    _encode(obj, out)
    return bytes(out)


class _Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.precedents = []

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b

    def take(self, n):
        chunk = self.data[self.pos:self.pos + n]
        self.pos += n
        return chunk

    def xlong(self):
        b = self.byte()
        if b < 255:
            return b
        return struct.unpack(">i", self.take(4))[0]

    def read(self):
        tag = self.byte()
        if tag == IMMEDIATE:
            ref = self.xlong() & 0xFFFFFFFF
            if ref & 3 == 0:
                return (ref >> 2) - (1 << 30) if ref & 0x80000000 else ref >> 2
            if ref == 0x1a:
                return True
            if ref == 2:
                return None
            if ref & 0xF == 6:
                return Char(chr(ref >> 4))
            return ("magic", ref)
        if tag == CHARACTER:
            return Char(chr(self.byte()))
        if tag == UNICHAR:
            return Char(chr(struct.unpack(">H", self.take(2))[0]))
        if tag == NIL:
            return None
        if tag == PRECEDENT:
            return self.precedents[self.xlong()]
        if tag == SYMBOL:
            n = self.xlong()
            obj = Symbol(self.take(n).decode("mac_roman"))
            self.precedents.append(obj)
            return obj
        if tag == STRING:
            index = len(self.precedents)
            self.precedents.append(None)
            n = self.xlong()
            obj = self.take(n).decode("utf-16-be").rstrip("\0")
            self.precedents[index] = obj
            return obj
        if tag in (BINARY, LARGE_BINARY):
            index = len(self.precedents)
            self.precedents.append(None)
            if tag == LARGE_BINARY:
                raise ValueError("large binaries are not read here")
            n = self.xlong()
            klass = self.read()
            obj = Binary(klass, self.take(n))
            self.precedents[index] = obj
            return obj
        if tag in (ARRAY, PLAIN_ARRAY):
            obj = []
            self.precedents.append(obj)
            n = self.xlong()
            if tag == ARRAY:
                self.read()             # its class
            for _ in range(n):
                obj.append(self.read())
            return obj
        if tag == FRAME:
            obj = {}
            self.precedents.append(obj)
            n = self.xlong()
            keys = [self.read() for _ in range(n)]
            for key in keys:
                obj[key] = self.read()
            return obj
        if tag == SMALL_RECT:
            top, left, bottom, right = self.take(4)
            obj = {Symbol("top"): top, Symbol("left"): left, Symbol("bottom"): bottom, Symbol("right"): right}
            self.precedents.append(obj)
            return obj
        raise ValueError("unknown NSOF tag %d at %d" % (tag, self.pos - 1))


def decode(data, many=False):
    """The object (or, with many, every object one after another) in NSOF
    data; the version byte comes first in each."""
    reader = _Reader(bytes(data))
    objects = []
    while reader.pos < len(reader.data):
        if reader.byte() != VERSION:
            raise ValueError("not NSOF version 2")
        objects.append(reader.read())
        if not many:
            return objects[0]
    return objects
