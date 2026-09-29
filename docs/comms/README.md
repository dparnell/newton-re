# Communications

The Newton's communications framework - options, endpoints, the comm
manager and the comm tools - and how the host reaches its own network
through it.  Reconstructed in `src/comms/`; the host's side of the seam is
`hal/host/HostSockets.h` and `comms/host/`.

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

**What the NIE registers** (from its protocol parts, `analysis/classinfo.py
--package <pkg>`; also `docs/packages/README.md`) decides the names:

| package | implementation | protocol | capability |
|---------|----------------|----------|------------|
| Internet Enabler | `TInetService` | `TCMService` | `serv` = `inet` - the TCP/UDP endpoints |
|  | `TInetCCEService` | `TCMService` | `serv` = `ictl` - the link controller (`InetGrabLink` and friends) |
|  | `TDNSService` | `TCMService` | `serv` = `dnst` - name lookups |
|  | `TDNSTool` | `TCommToolProtocol` | `ctiv` = 2 - the DNS service's tool |
|  | `PInetToolMux` | `PMuxTool` | the TCP/IP stack's tool, one per link |
|  | `PInetToolCCE`, `PInetToolCE` | `PConnectionEnd` | its connection ends (the link controller's, an endpoint's) |
|  | `PSerialDriverModule` | `PStrDriverModule` | the stack's serial driver |
| Ethernet Module | `PEnetLinkModule`, `PDhcpDynAddrModule`, `PLanternDriverModule` | `PStrLinkModule`, `PStrDynAddrModule`, `PStrDriverModule` | the Ethernet link, DHCP, the card driver |
| LocalTalk Module | `PMacIPLinkModule`, `PMacIPDriverModule` | `PStrLinkModule`, `PStrDriverModule` | MacIP |
| Modem & Serial Module | `PPPPLinkModule`, `PSLPLinkModule` | `PStrLinkModule` | PPP and SLIP |
| Newton Devices | `TLanternDriverAPI`, `TLanternClientAPI`, `TLanternCardHandler`, `TLanternEventWorld`, `PLinkEnet` | the Lantern (Ethernet card) driver API | |

The NewtonScript reaches only the three services (an endpoint's `service`
option names `inet`, `ictl` or `dnst`, and the comm manager starts the
`TCMService` whose `serv` capability it is - `TCMEventHandler::StartService`);
everything below them - the mux tool, the connection ends, the stream
modules and the Lantern driver - is the stack and its drivers, which the
host does not need.  So the host registers:

* **`THostInetService`** (`serv` = `inet`): starts a `THostTCPTool`
  (`comms/host/HostTCPTool.h`), a `TCommTool` subclass, so the ROM's request
  handling, option processing and abort machinery are the ROM's own; only
  connect, listen, get, put and the termination are socket calls.
* **the link controller** (`serv` = `ictl`): answers the link as up at
  once, the host's link being the host's business.
* **the DNS service** (`serv` = `dnst`): answers lookups through the host's
  resolver.

and none of the stack's parts.

**The sockets** are `hal/host/HostSockets.h`: a plain C interface over int
handles, implemented in `hal/host/HostSockets.cpp` (library
`hal_host_sockets`), built without the Newton include paths (as
`host/win32/HostWindow.cpp` is - the DDK's names collide with the platform
headers').  Winsock on Windows, BSD sockets elsewhere; name resolution
through `getaddrinfo`.  Every call is non-blocking and the tool polls from
its `HandleTimerTick`, so its task never waits inside the host where the
Newton scheduler cannot see it.

### The NIE's endpoint options

The host tool answers the NIE's options with the layout the NIE's own
native code gives them.  None of the NIE's NewtonScript builds them (a
client application does; no application in `fixtures/` does), so the
evidence is the ARM code of `inetenbl.pkg`'s protocol parts, read with
`tools/newton-rom/analysis/pkgdisasm.py fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg --find <label>`
(offsets are within the part; the data starts at +0xc, after the 12-byte
header; everything big-endian):

| label | length | data | evidence |
|-------|--------|------|----------|
| `itrs` | 8 | the address (a long), the port (2 bytes), 2 bytes of padding | its constructor, part 4 +0x52c4 (allocates 0x14, length 8, clears the long at +0xc and the bytes at +0x10/+0x11); part 4 +0x22b4 fills one in: the port's high byte at +0x10, low at +0x11, the address word at +0xc |
| `ilpt` | 4 | the port (2 bytes), a byte set to 1 by default (meaning not established), padding | constructors part 4 +0x5274 and part 10 +0x2b30; part 4 +0x221c writes the port high byte first at +0xc |
| `itsv` | 4 | a long: 1 is TCP, 2 is UDP | constructors part 4 +0x51b0 and part 10 +0x2a74 (default 0); the tool's option handler, part 4 +0x934, accepts only 1 and 2; the DNS tool (part 10), which resolves over UDP, calls its endpoint set-up (+0x2d48) with 2 (+0x1838), which it stores into the `itsv` option (+0x2df4) - so 2 is UDP and 1 TCP |
| `ilid` | 4 | the link id, a long, -1 by default | constructor part 10 +0x2ad0 |

Part 4 is `PInetToolCE` (an endpoint's connection end) and part 10 is
`TDNSTool` (`pkgdisasm.py --list`, `classinfo.py --package`).

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

## The comm tool (`comms/CommTools.h`)

A tool is a task: `StartCommTool` starts it (priority 13, a 6000-byte stack
in the ROM) and finds its port through the name server, where the task
registered it as (its task id in decimal, the service's four characters).
`TaskMain` takes one request per channel at a time - get, put, control,
get-event, kill, status, resource arbitration, each sent with its channel's
bit as the message type - closing the channel until the request is answered
(`CompleteRequest`).  A control request's options are processed one by one
(`ProcessOptionsContinue`; a tool answers its own labels in
`ProcessOptionStart`, and the result goes back into each option: processed,
or `0xFC` - not processed - for the tool below, if there is one, to be
forwarded to); then the op code's `...Start`, whose `...Complete` answers
the request.  Disconnect, release and close of a connected tool are an
*abort*: `kToolStateWantAbort`, the subclass's termination procs phase by
phase (`GetNextTermProc`), then `TerminateComplete` answers what is
outstanding and posts the disconnect event.

ROM bugs kept (commented where they are): an outside connect with data
fails unless the previous connect left its data behind (`ImportConnectPB`
tests the old pointer), a disconnect event is lost when no get-event request
is waiting (`GetCommEvent` looks for -16016 where `PostCommEvent` answers
-16015), the options are never completed when a `ctso` for another tool is
the last of them, and `'sid`'s default is a passive claim's.  The request
classes' constructors leave the 2.0 fields (`fOptions`, `fOptionCount`)
alone, as the ROM's do: a client sets them.

A host note: the host's clock stands still while tasks run, so
`TaskMain`'s countdown of its timeout by the time a message took can come
to nought, which is no timeout at all; the host tool re-arms it in
`HandleInternalEvent`.

## Status

| piece | state |
|-------|-------|
| options (`TOption`, `TOptionExtended`, `TSubArrayOption`, `TOptionArray`, `TOptionIterator`) | done: `comms/Options.h`, `test_Options` |
| `CBufferList`, `CShadowBufferSegment` (what a tool's data comes in) | done: `utility/BufferList.h`, `utility/ShadowBufferSegment.h`, `test_BufferList` |
| the comm tool: `TCommTool`, the requests and replies, the tool's own options, `StartCommTool`, `ServiceToPort` | done: `comms/CommTools.h` |
| the host TCP tool and the sockets | done: `comms/host/HostTCPTool.h`, `hal/host/HostSockets.h`; **M0** passes (`test_CommTool`) |
| the comm manager: `TCMWorld`, `TCMEventHandler` (starting a service by its `serv` capability), `TStartInfo`, `TAsyncServiceMessage`, `OpenCommTool`, `CMStartService`, the last device and package, `CMGetServiceVersion` | done: `comms/CommManager.h`; **M1** passes (`test_CommManager`) |
| the host's `inet` service (`THostInetService`) | done: `comms/host/HostServices.h` |
| `CMGetEndpoint` (needs `TEndpoint`), the docking loader (`TSCPLoader`, `SCPLoad`), `TICHandler`, `InitializeCommHardware`, the ROM's own services (`RegisterROMProtcols`) | NOT YET |
| `CMemObject` (a status request's answer goes through `TUSharedMem` meanwhile) | NOT YET |
| `TPCommTool`/`StartCommToolProtocol` (a tool as a `TCommToolProtocol`) | NOT YET |
| the endpoint, the NewtonScript endpoint, the link and DNS services | NOT YET |
