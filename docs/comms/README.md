# Communications

The Newton's communications framework - options, endpoints, the comm
manager and the comm tools - and how the host reaches its own network
through it.  Reconstructed in `src/comms/`; the host's side of the seam is
`hal/host/HostSockets.h` / `host/HostSockets.cpp`.

## The owner's decision: the host's TCP/IP, not a TCP/IP stack

Apple's Newton Internet Enabler (NIE 2.0, `fixtures/packages/apple/NIE2/`)
is NewtonScript over native ARM protocol parts: the parts carry the TCP/IP
stack, PPP, SLIP, the Ethernet driver glue and the DNS resolver, and
register them with the comm manager as services.  The owner decided that
the host provides those parts itself, backed by the host's sockets and
name resolution: *"I want the host to provide the necessary parts for an
interface to the host TCP/IP network stack.  There is no point
implementing a TCP/IP stack if we do not need one."*

So:

* the ROM's framework above the seam - options, `TEndpoint` and
  `TSerialEndpoint`, the NewtonScript endpoint (`protoBasicEndpoint` and
  its `CINew*` natives), the comm manager, the `TCommTool` base - is
  reconstructed faithfully, bugs and all, like the rest of `src/`;
* below the seam, a host service and a host comm tool are registered under
  the names the NIE's protocol parts register, and do their work through
  the host's sockets.  The NIE's ARM code is never ported or emulated, and
  no TCP/IP stack is written.  This is the one owner-chosen replacement in
  the area, and the code says so where it sits (`DEVIATION:` at the seam);
* the NIE's own NewtonScript - Internet Setup, `InetGrabLink` and the link
  controller, the DNS calls - and a script's TCP endpoint then run
  unchanged.

## The layers, bottom up, with their size in the ROM

Sizes are the code of the ROM's functions (from `build/MP2x00US/symbols.txt`,
each function running to the next symbol).  The 22 `CINew*` natives a
`protoBasicEndpoint` calls reach 283 functions (54 KB) not yet
reconstructed (`analysis/callgraph.py build/MP2x00US CINewInstantiate ...
--through-done`); that is a lower bound, since the comm tool runs in a task
of its own and is reached through messages, not calls.

| # | layer | ROM classes | size |
|---|-------|-------------|------|
| 1 | options | `TOption`, `TOptionExtended`, `TSubArrayOption`, `TOptionArray`, `TOptionIterator` (0x0014aa38-0x0014b9f0) | 2.4 KB, 53 fns |
|   |  | the `TCMO*` option classes (constructors: label, type, length, defaults) | ~100 B each, ~130 classes; only the ones reached |
| 2 | comm tool messages | `TCommToolOpenRequest`, `...Connect/Bind/Get/Put/Kill/Control/Status...Request`, their replies, `TCommToolAEvent`, `TCommToolOptionInfo` | ~3 KB |
| 3 | the comm tool | `TCommTool` (a `TUTaskWorld`: its request queues, the open/connect/listen/accept/disconnect/get/put/kill state machine, option processing, the event and reply paths) and the `TCommToolProtocol` thunks | 10.7 KB (106) + 2 KB |
| 4 | the comm manager | `TCMWorld`, `TCMEventHandler` (`StartService`: the `TCMService` implementation whose `'serv` capability is the service option's label), `TStartInfo`, `TServiceInfo`, `CMStartService`, `CMGetEndpoint`, the `TCMService` protocol | ~4 KB |
| 5 | the endpoint | `TEndpoint` (abstract), `TSerialEndpoint` (the endpoint that talks to a comm tool), `TEndpointEventHandler`, `TEndpointClient`, `TEndpointPipe` | 1.2 + 14.2 + 1.6 + 0.6 + 1.9 KB |
| 6 | the NewtonScript endpoint | `TNewScriptEndpointClient` (option frames to and from `TOptionArray`s, the input specs, the async callbacks) and the `CINew*` natives | 19.6 KB (66) |
| - | *the seam* | a host `TCMService` (`'serv` = the NIE's service labels) and a host `TCommTool` subclass | host code |
| - | *the host* | `hal/host/HostSockets.h` - a small C interface: connect, listen, accept, send, receive (non-blocking, polled), close, resolve | host code |

The serial tools (`TSerTool`, `TAsyncSerTool`, 14 KB) and the ROM's other
comm tools (MNP, IrDA, modem, fax) sit beside the host tool at layer 3 and
are NOT YET; they are not on the way to the network.

## Where the seam sits

* **What the NIE registers** decides the names: a `TCMService`
  implementation per service label its NewtonScript asks for (the
  endpoint options `inet` with `ilid`, `itsv`, `itrs`, `ilpt`, `iexp` are
  in `inetenbl.pkg`'s code; `ilnk` and `idns` are in the link and module
  packages' NewtonScript), and possibly a `TEndpoint` implementation named
  by an `endp` option.  The package manager's side (`analysis/classinfo.py`
  over a package's protocol parts) gives the exact class and capability
  names; the host registers its own implementations under those.
* **The host service** (`TCMService`) starts the host comm tool's task
  and answers its port, exactly as the ROM's services start theirs; the
  link services answer "up" at once, since the host's link is the host's
  business.
* **The host comm tool** (a `TCommTool` subclass, so the ROM's request
  handling, option processing and event paths are the ROM's own) turns
  connect, get and put into the socket calls, polled from the tool's idle.
* **The sockets** are `hal/host/HostSockets.h`: a plain C interface over
  int handles, implemented in `host/HostSockets.cpp`, which is built without
  the Newton include paths (as `host/win32/HostWindow.cpp` is - the DDK's
  names collide with the platform headers').  Winsock on Windows, BSD
  sockets elsewhere; name resolution through `getaddrinfo`.

## Host format of an option

An option is a 12-byte header (label, length of the data, flags) and its
data, options following one another in a pointer block, each rounded up to
four bytes (`(fLength + 0xf) & ~3`).  On the host the header is
`sizeof(TOption)` - the DDK's `ULong` is pointer-sized - and options are
rounded to the size of a pointer, so an option class's `ULong` members are
aligned (`OptionStep` in `comms/OptionArray.h`).  An option array is only
ever read by the task it was made in or through a shared-memory copy
between tasks of one host process, so the format is private to the host
and nothing on a store or a wire depends on it.  DEVIATION (pointer size).

## Order and milestones

1. Options (layer 1), with the `TCMO*` classes the path reaches.
2. The comm tool (layers 2 and 3) and the host TCP tool under it;
   **M0**: a C++ client opens the host tool's task, connects it to a TCP
   echo server the test starts on 127.0.0.1, puts bytes and gets them back
   through the comm tool messages.
3. The comm manager (layer 4) and the host service; **M1**: `CMGetEndpoint`
   with the NIE's options answers an endpoint bound to the host tool.
4. The endpoint (layer 5): `TSerialEndpoint` over the tool; **M2**: the
   same echo through `TEndpoint::Connect`/`Snd`/`Rcv`.
5. The NewtonScript endpoint (layer 6); **M3**: a script's
   `protoBasicEndpoint` with the NIE's TCP options connects to the echo
   server, outputs a string and its input spec receives it back.
6. The link and DNS services; **M4**: the NIE's own `InetGrabLink` and a
   DNS lookup run unchanged against the host.

## Status

| piece | state |
|-------|-------|
| options (`TOption`, `TOptionExtended`, `TSubArrayOption`, `TOptionArray`, `TOptionIterator`) | see `src/comms/OptionArray.h` |
| everything else above | NOT YET |
