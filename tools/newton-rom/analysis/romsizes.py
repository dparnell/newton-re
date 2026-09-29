#!/usr/bin/env python3
"""Find ROM byte counts used as the size of something pointer-sized on the host.

Usage:
    python tools/newton-rom/analysis/romsizes.py [src] [--all] [--exclude DIR ...] [--wide-only]

The reconstruction keeps the ROM's struct layouts, but on the host a ULong,
a Ref, a pointer and a TRegister are pointer-sized (8 bytes) where the ROM's
are 4, so a struct holding one is bigger on the host than the ROM's byte
count says.  A ROM size copied into the source as a literal - the size of a
message sent or a reply answered, of a block allocated, copied or cleared -
then truncates the struct on the host: the sound server's 0x20-byte reply
stopped short of the node id, and the Sound Recorder never learnt how much
it had recorded (docs/work-log.md, 2026-09-30).

This scans the C++ sources for calls that take a size - message traffic
(Send, SendRPC, Receive, Reply, ReplyRPC, SetReply, Call), allocation
(NewPtr, NewNamedPtr, NewHandle, malloc, calloc, realloc, ...), copying
(memcpy, memmove, memset, BlockMove, CopyToShared, CopyFromShared) - whose
arguments include a hexadecimal literal, and for each one:

  - names the struct types of the other arguments where it can find the
    variable's declaration in the same file (`&reply`, `reply`, `&x->f`
    are looked up; a `new T` or `(T*)` cast is read directly);
  - says whether those structs hold anything pointer-sized on the host
    (their own members, their bases', and structs held by value - read
    from every struct and class definition under src/): WIDE, NARROW or
    UNKNOWN.

It also lists named constants with hexadecimal values whose names end in
Size or Bytes (a ROM size kept as a constant, which the same bug can hide
behind), with --all.

A hit is a place to look, not a bug: a size that is the ROM's size of the
*ROM's* data (bytes read from the ROM image, a big-endian persistent
format, a hardware register block) is right as it is.  A WIDE hit is the
likely bug.  Fix one with sizeof or offsetof (a named constant when the
ROM value means something) and a DEVIATION comment.

--lp64 takes the host to be Linux-like (LP64), where `long` - and so
NewtonErr, Fixed, AEEventClass and AEEventID - is 64 bits as well; on
Windows (LLP64) only the pointer-sized types grow.

Output: one line per hit, `file:line: WIDE|NARROW|UNKNOWN call(literal) [types]`,
then a count.  --wide-only prints only the WIDE ones; --exclude leaves out
directories (repeatable, relative to src).
"""

import argparse
import os
import re
import sys

CALLS = ['SendRPC', 'Send', 'Receive', 'ReplyRPC', 'Reply', 'SetReply', 'Call', 'SendForReply',
         'NewPtr', 'NewPtrClear', 'NewNamedPtr', 'NewHandle', 'NewHandleClear', 'NewNamedHandle',
         'ReallocPtr', 'SetPtrSize', 'SetHandleSize', 'malloc', 'calloc', 'realloc',
         'memcpy', 'memmove', 'memset', 'bzero', 'BlockMove', 'CopyToShared', 'CopyFromShared',
         'CopyTo', 'CopyFrom', 'Copy']
CALL_RE = re.compile(r'(?<![\w.])(?:[\w:]+(?:\.|->))?(' + '|'.join(CALLS) + r')\s*\(')
HEX_RE = re.compile(r'\b0x[0-9a-fA-F]+\b')
# pointer-sized on the host (src/ddk/NewtonTypes.h: ULong is uintptr_t, Long
# intptr_t, and TObjectId and ArrayIndex are made of them)
WIDE_TYPES = {'ULong', 'Long', 'Ref', 'RefStruct', 'RefVar', 'TRegister', 'uintptr_t', 'intptr_t',
              'size_t', 'ptrdiff_t', 'Ptr', 'Handle', 'VAddr', 'PAddr', 'TObjectId', 'ObjectId',
              'ArrayIndex', 'HostPtr', 'TTimeout'}
# the same size on every host - except `long` and the types made of it
# (NewtonErr, Fixed, AEEventClass, AEEventID), which are 32 bits on Windows
# (LLP64) and 64 on Linux (LP64): --lp64 counts those as wide
LONG_TYPES = {'long', 'NewtonErr', 'OSErr', 'Fixed', 'Fract', 'AEEventClass', 'AEEventID', 'FastInt'}
NARROW_SCALARS = {'char', 'short', 'int', 'unsigned', 'signed', 'UChar', 'UShort', 'UByte',
                  'Byte', 'SByte', 'Short', 'Long32', 'ULong32', 'Boolean', 'bool', 'float',
                  'double', 'UniChar', 'uint8_t', 'uint16_t', 'uint32_t', 'int8_t', 'int16_t',
                  'int32_t', 'uint64_t', 'int64_t', 'Coordinate', 'SemFlags', 'Point', 'Rect'} | LONG_TYPES
LP64 = False


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def read(path):
    raw = open(path, 'rb').read().decode('utf-8', 'replace')
    return raw.replace('\r\n', '\n').replace('\r', '\n')


def match_brace(text, i):
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return j
    return -1


def match_paren(text, i):
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return j
    return -1


def split_args(s):
    args, depth, cur = [], 0, ''
    for c in s:
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            args.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        args.append(cur.strip())
    return args


STRUCT_RE = re.compile(r'\b(struct|class|union)\s+(\w+)\s*(?:final\s*)?(:[^{;]*)?\{')


def collect_structs(root):
    """name -> {'bases': [...], 'members': [type strings]}"""
    structs = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            if not f.endswith(('.h', '.cpp')):
                continue
            text = strip_comments(read(os.path.join(dirpath, f)))
            for m in STRUCT_RE.finditer(text):
                name = m.group(2)
                start = m.end() - 1
                end = match_brace(text, start)
                if end < 0:
                    continue
                body = text[start + 1:end]
                # drop nested function bodies
                body = re.sub(r'\{[^{}]*\}', ';', body)
                body = re.sub(r'\{[^{}]*\}', ';', body)
                bases = []
                if m.group(3):
                    bases = [b.split()[-1] for b in m.group(3)[1:].split(',') if b.split()]
                members = []
                for decl in body.split(';'):
                    decl = decl.strip()
                    if not decl or '(' in decl or decl.startswith(('public', 'private', 'protected',
                                                                    'typedef', 'using', 'friend',
                                                                    'enum', 'static', 'template')):
                        decl = re.sub(r'^(public|private|protected)\s*:\s*', '', decl)
                        if not decl or '(' in decl or decl.startswith(('typedef', 'using', 'friend',
                                                                        'enum', 'static', 'template')):
                            continue
                    members.append(decl)
                if name not in structs or len(members) > len(structs[name]['members']):
                    structs[name] = {'bases': bases, 'members': members}
    return structs


def is_wide(name, structs, seen=None):
    """True: holds something pointer-sized; False: does not; None: unknown."""
    if seen is None:
        seen = set()
    if name in WIDE_TYPES or (LP64 and name in LONG_TYPES):
        return True
    if name in NARROW_SCALARS:
        return False
    if name not in structs or name in seen:
        return None
    seen.add(name)
    s = structs[name]
    verdict = False
    if any(True for b in s['bases']):
        for b in s['bases']:
            w = is_wide(b, structs, seen)
            if w:
                return True
            if w is None and b not in ('SingleObject',):
                verdict = None if verdict is False else verdict
    for decl in s['members']:
        if '*' in decl or '&' in decl:
            return True
        words = re.findall(r'[A-Za-z_]\w*', decl.split('[')[0].split(':')[0])
        words = [w for w in words if w not in ('const', 'volatile', 'mutable', 'unsigned', 'signed',
                                               'struct', 'class', 'union')]
        if not words:
            continue
        if LP64 and re.search(r'\blong\b', decl) and not re.search(r'\blong\s+long\b', decl):
            return True
        t = words[0]
        w = is_wide(t, structs, seen)
        if w:
            return True
        if w is None and verdict is False:
            verdict = None
    return verdict


def declared_type(text, upto, var):
    """The type a variable was last declared with before `upto`."""
    pat = re.compile(r'(?:^|[;{(,\s])((?:const\s+)?(?:struct\s+|class\s+)?[A-Za-z_][\w:]*)\s*[*&]*\s*\b'
                     + re.escape(var) + r'\s*(?:[;=,)\[]|$)', re.M)
    best = None
    for m in pat.finditer(text, 0, upto):
        t = m.group(1).split()[-1].split('::')[-1]
        if t in ('return', 'else', 'delete', 'new', 'case', 'goto', 'sizeof', 'const'):
            continue
        best = t
    return best


def member_type(structs, owner, member, depth=0):
    """The type of owner's member (looked for in its bases too)."""
    s = structs.get(owner)
    if s is None or depth > 6:
        return None
    for decl in s['members']:
        names = re.findall(r'[A-Za-z_]\w*', decl.split('[')[0].split('=')[0])
        if member in names[1:]:
            words = [w for w in names if w not in ('const', 'volatile', 'mutable', 'unsigned', 'signed',
                                                   'struct', 'class', 'union')]
            return words[0] if words else None
    for b in s['bases']:
        t = member_type(structs, b, member, depth + 1)
        if t:
            return t
    return None


def arg_types(text, pos, args, structs):
    types = []
    for a in args:
        if HEX_RE.fullmatch(a.strip()):
            continue
        m = re.search(r'\bnew\s+([A-Za-z_]\w*)', a) or re.search(r'\(\s*([A-Za-z_]\w*)\s*\*\s*\)', a)
        if m:
            types.append(m.group(1))
            continue
        m = re.fullmatch(r'&?\s*([A-Za-z_]\w*)(?:\s*(?:\.|->)\s*([A-Za-z_]\w*))*', a.strip())
        if m:
            names = re.findall(r'[A-Za-z_]\w*', a)
            t = declared_type(text, pos, names[0])
            # a member path followed through the structs (x->fY.fZ: the type
            # of fZ), and the outermost object's type as well when the path
            # starts from a value (&reply.fEvent sends the whole reply)
            outer = t if (t and len(names) > 1 and '->' not in a) else None
            for member in names[1:]:
                if t is None:
                    break
                t = member_type(structs, t, member)
            if t:
                types.append(t)
            # (the outer object counts only when the member is itself a
            #  struct that begins it - a copy of a scalar or array member is
            #  that member's size)
            if outer and (t is None or t in structs):
                types.append(outer)
    return types


def scan(root, excludes, want_all, wide_only):
    structs = collect_structs(root)
    hits = []
    for dirpath, dirs, files in os.walk(root):
        rel = os.path.relpath(dirpath, root).replace('\\', '/')
        if any(rel == e or rel.startswith(e + '/') for e in excludes):
            continue
        for f in sorted(files):
            if not f.endswith(('.cpp', '.h', '.c')):
                continue
            path = os.path.join(dirpath, f)
            text = strip_comments(read(path))
            for m in CALL_RE.finditer(text):
                open_at = m.end() - 1
                close = match_paren(text, open_at)
                if close < 0:
                    continue
                inner = text[open_at + 1:close]
                args = split_args(inner)
                # (a size: a hex literal, or one scaling a count, up to 0x4000 -
                #  bigger ones are timeouts, message types and sample rates)
                lits = [a for a in args
                        if (HEX_RE.fullmatch(a.strip())
                            or re.fullmatch(r'0x[0-9a-fA-F]+\s*\*\s*[\w()]+|[\w()]+\s*\*\s*0x[0-9a-fA-F]+', a.strip()))
                        and int(HEX_RE.search(a).group(0), 16) <= 0x4000]
                # (or a variable given a hex size a few lines up: `size = 0x20;`
                #  then Send(..., size) - how the sound server's replies hid)
                if not lits:
                    before = text[max(0, m.start() - 3000):m.start()]
                    for a in args:
                        a = a.strip()
                        if re.fullmatch(r'[A-Za-z_]\w*', a) and re.search(r'(?i)size|bytes|length', a):
                            assigned = re.findall(r'\b' + re.escape(a) + r'\s*=\s*(0x[0-9a-fA-F]+)\s*;', before)
                            if assigned and int(assigned[-1], 16) <= 0x4000:
                                lits.append(a + ' = ' + assigned[-1])
                if not lits:
                    continue
                if m.group(1) == 'memset' and len(args) == 3 and args[2].strip() not in lits:
                    continue		# (memset's value, not its size)
                types = arg_types(text, m.start(), args, structs)
                verdicts = [is_wide(t, structs) for t in types]
                if any(v is True for v in verdicts):
                    verdict = 'WIDE'
                elif types and all(v is False for v in verdicts):
                    verdict = 'NARROW'
                else:
                    verdict = 'UNKNOWN'
                line = text.count('\n', 0, m.start()) + 1
                shown = ' '.join(inner.split())[:90]
                via = [l for l in lits if ' = ' in l]
                hits.append((verdict, os.path.relpath(path, os.path.dirname(root)).replace('\\', '/'), line,
                             m.group(0)[:-1].strip() + '(' + shown + ')' + (('  {' + ', '.join(via) + '}') if via else ''),
                             sorted(set(types))))
            if want_all:
                for c in re.finditer(r'(?:#define\s+|const\s+\w+\s+)(k\w*(?:Size|Bytes))\s*=?\s*\(?\s*(0x[0-9a-fA-F]+)', text):
                    line = text.count('\n', 0, c.start()) + 1
                    hits.append(('CONST', os.path.relpath(path, os.path.dirname(root)).replace('\\', '/'), line,
                                 c.group(1) + ' = ' + c.group(2), []))
    order = {'WIDE': 0, 'UNKNOWN': 1, 'NARROW': 2, 'CONST': 3}
    hits.sort(key=lambda h: (order[h[0]], h[1], h[2]))
    shown = 0
    for verdict, path, line, call, types in hits:
        if wide_only and verdict != 'WIDE':
            continue
        if verdict == 'NARROW' and not want_all:
            continue
        shown += 1
        print('%s:%d: %s %s%s' % (path, line, verdict, call, ('  [' + ', '.join(types) + ']') if types else ''))
    counts = {k: sum(1 for h in hits if h[0] == k) for k in order}
    print('%d wide, %d unknown, %d narrow%s' % (counts['WIDE'], counts['UNKNOWN'], counts['NARROW'],
                                                  (', %d size constants' % counts['CONST']) if want_all else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', nargs='?', default=os.path.join(os.path.dirname(__file__), '..', '..', '..', 'src'))
    ap.add_argument('--all', action='store_true', help='also the NARROW hits and the size constants')
    ap.add_argument('--wide-only', action='store_true', help='only the WIDE hits')
    ap.add_argument('--lp64', action='store_true',
                    help='as on Linux: long (and NewtonErr, Fixed, AEEventClass...) is 64 bits too')
    ap.add_argument('--exclude', action='append', default=['ddk', 'thirdparty'],
                    help='a directory under src to leave out (ddk and thirdparty by default)')
    a = ap.parse_args()
    global LP64
    LP64 = a.lp64
    scan(os.path.abspath(a.src), a.exclude, a.all, a.wide_only)
    return 0


if __name__ == '__main__':
    sys.exit(main())
