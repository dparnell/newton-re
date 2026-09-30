#!/usr/bin/env python3
"""
notyet.py - the NOT YET markers in src/ that name something src/ already has.

Purpose
    A reconstruction note saying "NOT YET RECONSTRUCTED: Foo" goes stale when
    Foo is reconstructed somewhere else and nobody comes back to the note.
    This lists every NOT YET marker in the source, pulls out the names it
    mentions, and reports the ones whose name src/ already defines - a
    function cited with `// ROM 0x... <mangled>` (the name the mangled symbol
    starts with, and Class::Name), or a C++ function or class defined in
    src/.  Each is a marker to look at: either the note is stale, or it names
    something done in one place and still missing in this one (a call not
    made yet), which the note should then say.

Inputs
    src/ (by default the repository's; or the path given).  No ROM, no
    Ghidra: it reads only the reconstructed source.

Output
    Text on stdout, one block per marker that names something defined:
        path:line: the marker's text (the comment it is in, joined)
            Name - cited at path:line [, ...]
    and a count at the end.  --all lists every marker, stale-looking or not;
    --json writes the same as a list of objects; --area limits it to one
    top-level area of src/ (views, stores, ...).

Invocation
    python tools/newton-rom/analysis/notyet.py [src] [--all] [--json] [--area views]

How a name is recognised
    In the marker's comment (the NOT YET line and the lines of the same
    comment block after it, up to a blank line or the end of the comment),
    words that look like C++ names: Class::Method, CamelCase names of five
    characters or more that contain a capital after the first letter
    (TCursor, EntryChanged, GetWordsHints), and names followed by "(".
    Common English words and the markers' own vocabulary are left out.  A
    name counts as defined when a `// ROM` citation's mangled symbol names
    it (Name__... or Name__<n>Class..., or a plain name), or when a line of
    src/ starts a definition of it (`Class::Name(` at the start of a line,
    `class Name`/`struct Name`, a function `Name(` at the start of a line).
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))

CITATION = re.compile(r'//\s*ROM\s+0x[0-9a-fA-F]+\s+(\S+)')
MANGLED = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*?)__(?:(\d+)([A-Za-z_][A-Za-z0-9_]*))?')
DEF_METHOD = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)\s*\(')
DEF_CLASS = re.compile(r'^\s*(?:class|struct|PROTOCOL)\s+([A-Za-z_][A-Za-z0-9_]*)\b')
DEF_FUNC = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\(')
NAME = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)?)\b(\s*\()?')

# words that look like names in the markers' prose but are not
IGNORED = set('''
    NOT YET RECONSTRUCTED DEVIATION ROM QUIRK BUG Newton NewtonScript
    TRUE FALSE NULL nil
'''.split())


def source_files(src):
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = [d for d in dirnames if d not in ('ddk',)]
        for f in filenames:
            if f.endswith(('.cpp', '.h', '.c', '.ns')):
                yield os.path.join(dirpath, f)


def is_candidate(word, called):
    if word in IGNORED:
        return False
    if '::' in word:
        return True
    if called and len(word) >= 4 and re.search(r'[A-Z]', word):
        return True
    if len(word) < 5:
        return False
    # a capital after the first character: CamelCase or TClass
    return re.search(r'[A-Z]', word[1:]) is not None and re.search(r'[a-z]', word) is not None


def collect_definitions(src):
    defined = {}	# name -> [(path, line)]
    classes_of = {}	# a bare method name -> the classes defining it
    own = {}		# a file's stem -> the classes whose methods it defines

    def add(name, where):
        defined.setdefault(name, []).append(where)

    def method(cls, name, where):
        add(cls + '::' + name, where)
        add(name, where)
        add(cls, where)
        classes_of.setdefault(name, set()).add(cls)
        own.setdefault(os.path.splitext(where[0])[0], set()).add(cls)

    for path in source_files(src):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        try:
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            for m in CITATION.finditer(line):
                sym = m.group(1)
                mm = MANGLED.match(sym)
                if mm:
                    name = mm.group(1)
                    if mm.group(3):
                        cls = mm.group(3)[:int(mm.group(2))]
                        method(cls, name, (rel, n))
                    else:
                        add(name, (rel, n))
                else:
                    plain = re.match(r'[A-Za-z_][A-Za-z0-9_]*', sym)
                    if plain:
                        add(plain.group(0), (rel, n))
            m = DEF_METHOD.match(line)
            if m:
                method(m.group(1), m.group(2), (rel, n))
                continue
            m = DEF_CLASS.match(line)
            if m and not line.rstrip().endswith(';'):
                add(m.group(1), (rel, n))
                continue
            m = DEF_FUNC.match(line)
            if m and path.endswith(('.cpp', '.c')):
                add(m.group(1), (rel, n))
                classes_of.setdefault(m.group(1), set()).add('')
    return defined, classes_of, own


def comment_block(lines, i):
    """The NOT YET line and the rest of its comment."""
    text = [lines[i].strip()]
    line = lines[i]
    if '//' in line:
        j = i + 1
        while j < len(lines) and lines[j].strip().startswith('//') and 'NOT YET' not in lines[j]:
            text.append(lines[j].strip())
            j += 1
    elif '*/' not in line[line.find('NOT YET'):]:
        j = i + 1
        while j < len(lines) and lines[j].strip() and j < i + 8:
            text.append(lines[j].strip())
            if '*/' in lines[j]:
                break
            j += 1
    joined = ' '.join(t.lstrip('/').strip() for t in text)
    return joined[joined.find('NOT YET'):] if 'NOT YET' in joined else joined


def markers(src, area=None):
    for path in source_files(src):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        if area and not rel.startswith('src/' + area + '/'):
            continue
        lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        for i, line in enumerate(lines):
            if 'NOT YET' in line:
                yield rel, i + 1, comment_block(lines, i)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('src', nargs='?', default=os.path.join(ROOT, 'src'))
    ap.add_argument('--all', action='store_true', help='every marker, not only those naming something defined')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--area', help='only src/<area>/')
    args = ap.parse_args()

    defined, classes_of, own = collect_definitions(args.src)
    results = []
    total = 0
    for rel, n, text in markers(args.src, args.area):
        total += 1
        found = []
        seen = set()
        for m in NAME.finditer(text):
            word, called = m.group(1), m.group(2) is not None
            if word in seen or not is_candidate(word, called):
                continue
            seen.add(word)
            key = word
            if '::' not in word and word in classes_of and '' not in classes_of[word]:
                # a method name: this file's own class's, else only when few
                # classes have one (GetValue, RealDoCommand are everywhere)
                mine = own.get(os.path.splitext(rel)[0], set()) & classes_of[word]
                if mine:
                    key = sorted(mine)[0] + '::' + word
                elif len(classes_of[word]) > 2:
                    continue
            where = [w for w in defined.get(key, []) if not (w[0] == rel and abs(w[1] - n) <= 3)]
            if where:
                found.append((key, where[:3]))
        if found or args.all:
            results.append({'path': rel, 'line': n, 'text': text,
                            'names': [{'name': w, 'at': ['%s:%d' % a for a in where]} for w, where in found]})

    if args.json:
        json.dump(results, sys.stdout, indent=1)
        print()
        return
    for r in results:
        print('%s:%d: %s' % (r['path'], r['line'], r['text'][:300]))
        for nm in r['names']:
            print('    %s - defined at %s' % (nm['name'], ', '.join(nm['at'])))
    print('%d marker(s) of %d name something src/ defines' % (sum(1 for r in results if r['names']), total))


if __name__ == '__main__':
    main()
