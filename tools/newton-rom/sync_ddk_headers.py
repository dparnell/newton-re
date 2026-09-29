#!/usr/bin/env python3
"""Produce the compilable copy of the DDK headers used by the reconstruction (src/ddk).

Usage:
    python sync_ddk_headers.py headers src/ddk

Copies every header into one flat directory (the DDK's includes are flat),
converting Mac Roman text and CR line endings to UTF-8/LF, and applies the
small list of PATCHES below - things the 1990s ARM compiler accepted but a
modern C++ compiler rejects - plus one general tidy-up (a name after #endif
becomes a comment).  Each patch is exact-match and must apply, so a
change in the originals is noticed.  The output is committed; re-run this
after touching headers/ or PATCHES.
"""

from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from newtonrom.headers import prepare, write_text  # noqa: E402

CLIBRARY = ["limits.h", "New.h", "setjmp.h", "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h"]

# Headers the reconstruction replaces with its own (same name, same public
# interface): OS600/Protocols.h describes TClassInfo as a table of ARM branch
# instructions and self-relative offsets that ProtocolGen's glue dispatches
# through; src/protocols/Protocols.h re-expresses it with virtual functions
# and function pointers (its comment says how).
REPLACED = {
    "Protocols.h": "ARM-specific protocol glue; src/protocols/Protocols.h re-expresses it",
    # BufferSegment.h is the external interface only (a private constructor, no
    # virtuals, "to prevent external code from knowing the size of the object");
    # src/utility/BufferSegment.h has the ROM's classes CMinBuffer, CBuffer and
    # CBufferSegment with their virtuals and layout
    "BufferSegment.h": "external interface only; src/utility/BufferSegment.h has the ROM's classes",
    # BufferList.h likewise (the constructor private); src/utility/BufferList.h
    # has the ROM's CBufferList with its fields
    "BufferList.h": "external interface only; src/utility/BufferList.h has the ROM's class",
    # Endpoint.h: TEndpoint is a protocol whose methods src/protocols/Protocols.h
    # makes virtual; src/comms/Endpoint.h declares them so (and the classes
    # an endpoint works with, which the DDK leaves out)
    "Endpoint.h": "a protocol's methods made virtual; src/comms/Endpoint.h",
}

PATCHES = {
    # SerialOptions.h: a block of NewtonScript character constants ($\u0000 and
    # friends) behind #ifdef FRAM.  They are not C++, and a C++ compiler lexes
    # even a skipped conditional block far enough to reject the universal
    # character names as control characters - so nothing that includes this
    # header will compile, and HALOptions.h (the configuration server's) does.
    # The block is commented out rather than deleted, so it is still there to
    # read.
    "SerialOptions.h": [
        ('#define\tunicodeNUL\t\t\t\t\t$\\u0000',
         "/*\tNewtonScript character constants; a C++ lexer will not have them\r" + '#define\tunicodeNUL\t\t\t\t\t$\\u0000'),
        ('#define\tunicodeUS\t\t\t\t\t$\\u001F',
         '#define\tunicodeUS\t\t\t\t\t$\\u001F' + "\r*/"),
    ],
    # CMService.h: a service's Start and DoneStarting are the protocol's
    # methods, which src/protocols/Protocols.h makes virtual (VIRTUAL ...
    # ENDVIRTUAL, the DDK's own "hasNoProtocols" way) so an implementation can
    # supply them; the header declares them plainly, as ProtocolGen took them
    "CMService.h": [
        ("\t\t\tNewtonErr\tStart(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);",
         "\t\t\tVIRTUAL NewtonErr\tStart(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo) ENDVIRTUAL;"),
        ("\t\t\tNewtonErr\tDoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);",
         "\t\t\tVIRTUAL NewtonErr\tDoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo) ENDVIRTUAL;"),
    ],
    # UserSemaphore.h: GetRefCon takes void**, fSem is a ULong* -> needs a cast in C++
    "UserSemaphore.h": [
        ("TULockingSemaphore(TObjectId id = 0) : TUSemaphoreGroup(id) { GetRefCon(&fSem); }",
         "TULockingSemaphore(TObjectId id = 0) : TUSemaphoreGroup(id) { GetRefCon((void**)&fSem); }"),
    ],
    # NewtonTypes.h: a virtual address is a 32-bit word on the MessagePad; a host build
    # (host_compat.h defines hostVAddrIsPointerSized) keeps whole host pointers in it,
    # since task stacks and the buffers behind shared memory are host memory there
    "NewtonTypes.h": [
        ("typedef ULong\tVAddr;",
         "#ifdef hostVAddrIsPointerSized\ntypedef uintptr_t\tVAddr;\n#else\ntypedef ULong\tVAddr;\n#endif"),
        # ULong/Long are the ARM's 32-bit word, which also carries pointers (refcons, task
        # arguments); a host build makes them pointer-sized, as an LP64 Linux does anyway
        ("typedef long\t\t\tLong;\t\t\t/* In ANSI C long is signed long */\ntypedef signed long\t\tSLong;\ntypedef unsigned long\tULong;\n\ntypedef signed long\t\tFastInt;",
         "#ifdef hostLongIsPointerSized\ntypedef intptr_t\t\tLong;\ntypedef intptr_t\t\tSLong;\ntypedef uintptr_t\t\tULong;\ntypedef intptr_t\t\tFastInt;\n#else\ntypedef long\t\t\tLong;\t\t\t/* In ANSI C long is signed long */\ntypedef signed long\t\tSLong;\ntypedef unsigned long\tULong;\n\ntypedef signed long\t\tFastInt;\n#endif"),
    ],
    # UserTasks.h: the include is spelt in the wrong case for a case-sensitive file system;
    # TUTaskWorld's spawned task starts at a member function in the ROM (its address is
    # passed as the TaskProcPtr), which C++ forbids - a static trampoline stands in
    "UserTasks.h": [
        ('#include "sharedTypes.h"', '#include "SharedTypes.h"'),
        ("\t\tvoid\t\t\tTaskEntry(ULong, TObjectId taskId);\t// low level entry for spawned task (only in base class)",
         "\t\tvoid\t\t\tTaskEntry(ULong, TObjectId taskId);\t// low level entry for spawned task (only in base class)\n"
         "\t\tstatic void\t\tTaskEntryProc(void* theObject, ULong size, TObjectId taskId);\t// the TaskProcPtr that calls TaskEntry (reconstruction)"),
    ],
    # UserPorts.h: the ROM's Sleep() and TUTaskWorld::StartTask use TUPort's private
    # Send*Goo like SleepTill does, but only SleepTill is a friend in the DDK's header
    # UserDomain.h: the include is spelt in the wrong case
    "UserDomain.h": [
        ('#include "sharedTypes.h"', '#include "SharedTypes.h"'),
    ],
    # DynamicArray.h: pointer arithmetic through a long truncates 64-bit host pointers
    "DynamicArray.h": [
        ("{ return (void*)((long)fArrayBlock + (fElementSize * index)); }",
         "{ return (void*)((char*)fArrayBlock + (fElementSize * index)); }"),
    ],
    # NewtQD.h: APCS aligns every structure to a word, so the ROM's Region, Picture
    # and Polygon have their Rect at offset 4 (a rectangular region's rgnSize is 12,
    # its rows start at 12; NewRgn 0x003150b0 zeroes the Rect at +4); a host
    # compiler packs the shorts, so the two bytes of padding are spelt out
    "NewtQD.h": [
        ("\t\tStructSizeType\trgnSize;\n\t\tRect\trgnBBox;\n\t\t} Region;",
         "\t\tStructSizeType\trgnSize;\n\t\tshort\t\t\tfiller;\t\t/* APCS word alignment of the Rect (sync_ddk_headers.py) */\n\t\tRect\trgnBBox;\n\t\t} Region;"),
        ("\t\tStructSizeType\tpicSize;\n\t\tRect\tpicFrame;\n\t\t} Picture;",
         "\t\tStructSizeType\tpicSize;\n\t\tshort\t\t\tfiller;\t\t/* APCS word alignment of the Rect (sync_ddk_headers.py) */\n\t\tRect\tpicFrame;\n\t\t} Picture;"),
        ("\t\tStructSizeType\tpolySize;\n\t\tRect\tpolyBBox;\n\t\tPoint\tpolyPoints[1];\n\t\t} Polygon;",
         "\t\tStructSizeType\tpolySize;\n\t\tshort\t\t\tfiller;\t\t/* APCS word alignment of the Rect (sync_ddk_headers.py) */\n\t\tRect\tpolyBBox;\n\t\tPoint\tpolyPoints[1];\n\t\t} Polygon;"),
        # paths are the ARM's 32-bit words, in a picture as in memory; a host's long
        # may be wider (an LP64 Linux), so they are spelt Long32 (host_compat.h)
        ("\t\tlong\tvectors;\n\t\tlong\tcontrolBits[1];",
         "\t\tLong32\tvectors;\t\t/* Long32: the ARM's word (sync_ddk_headers.py) */\n\t\tLong32\tcontrolBits[1];"),
        ("\t\tlong\tcontours;\n\t\tpath\tcontour[1];",
         "\t\tLong32\tcontours;\t\t/* Long32: the ARM's word (sync_ddk_headers.py) */\n\t\tpath\tcontour[1];"),
        ("\t\tlong index;\n\t\tlong ep;\n\t\tlong* bits;",
         "\t\tlong index;\n\t\tlong ep;\n\t\tLong32* bits;\t\t/* Long32: the contour's words (sync_ddk_headers.py) */"),
        # a text object is a handle (NewText 0x0035bfc4) passed as the ARM's word;
        # Long is that word, pointer-sized on a host
        ("\ttypedef void (*TextObjProc) (/*TextObjectRef*/ long , Fixed , Fixed );",
         "\ttypedef void (*TextObjProc) (/*TextObjectRef*/ Long , Fixed , Fixed );\t/* Long: a handle (sync_ddk_headers.py) */"),
    ],
    # ConfigQD.h: the MP2100 ROM is built with QD_Gray - its PixelMap has the
    # grayTable field (0x1c bytes: a GrafPort is 0x54 bytes with portRect at
    # 0x1c, clipRgn at 0x28, grafProcs at 0x40 - SetClip 0x002be7cc, FrameRect
    # 0x003150a4, OpenPort 0x002be72c); the DDK never defines the switch
    "ConfigQD.h": [
        ("\t#define QD_SupportUnicode\n",
         "\t#define QD_SupportUnicode\n\n\t#define QD_Gray\t\t\t/* the MP2100 ROM's PixelMap has the grayTable (sync_ddk_headers.py) */\n"),
    ],
    "UserPorts.h": [
        ("\t\tfriend void SleepTill(TTime* futureTimeToSend);",
         "\t\tfriend void SleepTill(TTime* futureTimeToSend);\n\t\tfriend void Sleep(TTimeout timeout);\n\t\tfriend class TUTaskWorld;"),
    ],
    # objects.h: a Ref is the ARM's 32-bit word, a tagged integer or a tagged pointer;
    # on a host it has to be pointer-sized (Long), and the DDK's `long` casts and
    # conversions would truncate it there
    "objects.h": [
        ("const long kRefTagBits = 2;",
         "typedef Long Ref;\t\t/* the ARM's word: pointer-sized on a host (sync_ddk_headers.py) */\n\nconst long kRefTagBits = 2;"),
        ("typedef long Ref;\n\n", ""),
        ("#define\tMAKEINT(i)\t\t\t(((long) (i)) << kRefTagBits)",
         "#define\tMAKEINT(i)\t\t\t((Ref) (int) (((ULong32) (Ref) (i)) << kRefTagBits))"),
        # the shift is unsigned (a negative shifted left is undefined) and in
        # 32 bits, sign-extended back: an integer Ref holds exactly what the
        # ARM's word would, so the machine's integers are the Newton's 30-bit
        # ones however wide a Ref is here.  A host that kept the extra bits
        # would compute sums the device could not hold, and they would change
        # under it the moment they were written to a store.
        ("#define\tMAKEIMMED(t, v)\t\t((((((long) (v)) << kRefImmedBits) | ((long) (t))) << kRefTagBits) | kTagImmed)",
         "#define\tMAKEIMMED(t, v)\t\t((((((Ref) (v)) << kRefImmedBits) | ((Ref) (t))) << kRefTagBits) | kTagImmed)"),
        ("#define MAKEMAGICPTR(index)\t((Ref) (((long) (index)) << kRefTagBits) | kTagMagicPtr)",
         "#define MAKEMAGICPTR(index)\t((Ref) (((Ref) (index)) << kRefTagBits) | kTagMagicPtr)"),
        ("const long kRefValueMask = -1 << kRefTagBits;", "const Ref kRefValueMask = (Ref) (~(ULong) 0 << kRefTagBits);"),
        ("const long kRefTagMask = ~kRefValueMask;", "const Ref kRefTagMask = ~kRefValueMask;"),
        ("const long kRefImmedMask = -1 << kRefImmedBits;", "const Ref kRefImmedMask = (Ref) (~(ULong) 0 << kRefImmedBits);"),
        ("\toperator long() const;\n#else\n\tinline\tRefVar();", "\toperator Ref() const;\n#else\n\tinline\tRefVar();"),
        ("\toperator long() const\t\t\t\t{ return h->ref; }", "\toperator Ref() const\t\t\t\t{ return h->ref; }"),
        ("\toperator long() const;\n#else\n\tinline\tRefStruct();", "\toperator Ref() const;\n#else\n\tinline\tRefStruct();"),
        ("\t\t\toperator long() const\t\t\t\t\t{ return h->ref; }", "\t\t\toperator Ref() const\t\t\t\t\t{ return h->ref; }"),
        # the ROM's TObjectIterator (0x30 bytes) ends with an ExceptionCleanup
        # (verify-report.txt: header 0x24 vs ROM 0x30): a stack iterator
        # registers it so that a Throw unwinding past it frees its RefHandles
        # the object header's two words are pointer-sized on the host, like a
        # Ref (frames/ObjHeader.h explains); the DDK leaves it eight opaque bytes
        ("#ifndef DEFINED_OBJHEADER\n#define DEFINED_OBJHEADER\nstruct ObjHeader {\n\tchar\t_[8];\n};\n#endif",
         "#ifndef DEFINED_OBJHEADER\n#define DEFINED_OBJHEADER\nstruct ObjHeader {\t\t/* the ROM's two 32-bit words, pointer-sized here (sync_ddk_headers.py; frames/ObjHeader.h) */\n"
         "\tULong\tfSizeAndFlags;\t/* size << 8 | flags */\n"
         "\tULong\tfGCStuff;\t\t/* lock count in bits 24-31; the collector's slot index or forwarding address */\n};\n#endif"),
        # a string literal is const on a host compiler
        ('inline void OutOfMemory(char* msg = "out of memory")\n\t{ throw2(exOutOfMemory, msg); }',
         'inline void OutOfMemory(const char* msg = "out of memory")\n\t{ throw2(exOutOfMemory, msg); }'),
        ("\tRefStruct\tfMapRef;\t// NILREF indicates an Array iterator\n};",
         "\tRefStruct\tfMapRef;\t// NILREF indicates an Array iterator\n"
         "\tExceptionCleanup\tfCleanup;\t// +0x20 (ROM; not in the DDK header - sync_ddk_headers.py)\n};"),
    ],
}


def main(argv=None) -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    # The DDK's CLibrary headers describe Newton's own C library (stddef.h,
    # stdlib.h, ...).  They would shadow the host's, so a host build does not
    # get them; the reconstruction uses the host C library instead.
    excludes = {n: "CLibrary header, host C library used instead" for n in CLIBRARY}
    excludes.update(REPLACED)
    names = prepare(src, out, excludes=excludes, write_all_cpp=False)
    for n in excludes:            # prepare() copies everything; drop the excluded ones
        if os.path.exists(os.path.join(out, n)):
            os.remove(os.path.join(out, n))
    patched = 0
    # `#endif __FOO_H` (a name where a comment belongs) is in several headers
    endif_tokens = re.compile(r"^(#endif)[ \t]+(\w+)[ \t]*$", re.M)
    for name in names:
        path = os.path.join(out, name)
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8").read()
        fixed = endif_tokens.sub(r"\1 /* \2 */", text)
        if fixed != text:
            write_text(path, fixed)
            patched += 1
    for name, edits in PATCHES.items():
        path = os.path.join(out, name)
        text = open(path, encoding="utf-8").read()
        for old, new in edits:
            if text.count(old) != 1:
                print(f"error: patch for {name} does not apply exactly once", file=sys.stderr)
                return 1
            text = text.replace(old, new)
            patched += 1
        write_text(path, text)
    print(f"wrote {len(names)} headers to {out}, {patched} patches applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
