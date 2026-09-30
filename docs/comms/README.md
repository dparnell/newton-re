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

**The NIE is built in** (the owner's decision, 2026-09-30): its Newton
Devices, Newton Internet Enabler and Internet Setup packages are part of
the ROM extension the default boot runs on (`romsrc/rex/`,
`romsrc/README.md`'s "The Newton Internet Enabler, built in"), so every
boot has it with no store and no `--package` - Internet Setup is in the
Extras drawer's Setup folder, and HostLink.ns registers the host's link
as it does for a stored copy.  The one change the NIE needed to live in
the ROM (a package in the ROM has no pkgRef, which it names its state
after) is `romsrc/rex/inetenbl.patches.tsv`.  A copy installed over it is
kept on its store but not activated, as the ROM does for any package of a
name already in use (ctest `host.NewtonNIEOverBuiltIn`).  Its modules
(Ethernet, LocalTalk, Modem & Serial) are still packages to install; they
import the Enabler's units from the ROM.  `newton --rom <image>` boots the
original ROM, which has no NIE.

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
  resolver, the setup's default domain tried after a name with no dot.

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

### The DNS service

The NIE's scripts look names up through its *domain manager*, a state
machine in `inetenbl.pkg`'s NewtonScript (read with
`tools/newton-rom/analysis/pkgns.py fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg --disasm ...`):

* **connect.action** (0x37a0d) instantiates a protoBasicEndpoint with a
  `'sid '` service option whose template data is `["dnst", 0]`
  (`['struct, ['array, 'char, 4], 'ulong]` - the service and a port of
  nought, which the comm manager fills in), `ilid` (the link id, a
  `'ulong`), `ddom` (the default domain, a C string, "." when the link has
  none) and a `dnic` (a server's address, a `'ulong` of four bytes) per
  DNS server; it reads the port back out of the `'sid '` option.
* **ProcessNextQuery.action** (0x38369) sends each query as an option
  request (`endpoint:Option`, op code 1024, opGetCurrent) of one `dnsq` -
  `["dnst", 0, queryID, address, type, nameLength, name]` by
  `['struct, ['array, 'char, 4], 'ulong x 5, ['array, 'char, 0]]` - and
  four `rrcd` records, `[0, 0, [0,0,0,0], [0,0,0,0], 0, 0, "", ""]` by
  `['struct, 'short, 'short, ['array, 'byte, 4] x 2, 'ulong, 'ulong, ['array, 'char, 0] x 2]`.
* **fCompletionSpec_Option.completionScript** (0x3874d) takes the `dnsq`'s
  second word as the result (non-zero: the query failed), and
  **ProcessQuerySuccess.MAddCacheEntry** (0x38cdd) reads each record whose
  `result` is nil as the target address (3rd), the result address (4th),
  the target name (7th) and the result name (8th), the 2nd short saying how
  long to cache it (nought: fifteen minutes).

The query types are the DNS's own (`TDNSTool`, part 10 +0x1390: 12 a
name, 15 a mail exchanger, 0x40, anything else an address); the tool's
option handler (part 10 +0x2b88) takes `ddom`, `dnic` and `ilid` and
answers a `dnsq` later, writing its result into the query's second word
(+0x2c58); records it has no answer for are marked processed with a result
of -2 (+0x173c).  A name that is not found is -60791 (the NIE's error
table, 0x43e3d: "The host name you requested wasn't located").

The host's `THostDNSService` (`serv` = `dnst`) starts a `THostDNSTool`
(`comms/host/HostDNSTool.h`) that answers the query at once through the
host's resolver (`HostResolveName`, `HostResolveAddress`): an address
query fills a record per address (target name the query's name), a name
query one record (the result name).  An answered record is replaced by
one holding the whole answer, as the NIE's own tool does (part 10 +0x15c4
calls, through its NTK glue, +0x463c and +0x4604 - `TOptionArray::RemoveOptionAt`
and `InsertVarOptionAt`, identified by their arguments: the array, the
record's index, then a header, the data and its length); a query's option
array is the client's own (the request is not "outside"), so it grows and
the names come back whole although the domain manager asks with empty
strings.  The script's `'sid '`
data is the device's two big-endian longs; the translators turn it into
the host's `TCMOServiceIdentifier` and back (DEVIATION).  Demo:
`src/host/demo/dns.ns`, ctest `host.NewtonDNS`.

The servers (`dnic`) and the link id are taken and left alone; the
default domain (`ddom`, the setup's `defaultDomain`, "." for none) is kept
- the NIE's own tool keeps it too (part 10 +0x2b88 -> +0x1798) - and a
name with no dot in it that the host's resolver does not find as it is is
asked for again with the domain after it, as a resolver's search list
does (the host asks for it as it is first, its own resolver applying the
host's own search domains).  `NEWTON_TRACE_DNS` prints the domain and
each name asked for (ctest `host.NewtonInetHostSetup.restart`).

### The Host network setup

The host's link is a setup in Internet Setup's soup like any other
(`comms/host/HostLink.ns`), and Internet Setup edits it through a data
definition made from Ethernet's.  So it must hold what an Ethernet setup
holds: Ethernet's FillNewEntry (inetstup.pkg 0x1e751) deep-clones its
`blankEntry` into a new setup - `configuration` (`'usingServer`, shown as
"DHCP Server", or `'manual`), `subnetMask`, `localAddress`,
`gatewayaddress` (four numbers each), `defaultDomain` ("") and
`dnsServers` (an array of addresses) - and the editor reads them: the
generic stationery's ValidateTarget (0x1a001), run when a slip is closed,
takes `Length(entry.DNSServers)`, which on a setup without the slot threw
`type.ref.frame` (-48418, an immediate where a frame was wanted).  The
host's setup has them (less Ethernet's `cardData`), `'usingServer` by
default since the host's network configures itself; a setup kept from
before is given the missing ones at boot.  The NIE's domain manager
(inetenbl.pkg connect.action, 0x37a0d) hands `defaultDomain` and each valid
address of `DNSServers` to the DNS tool; the addresses of a manual
configuration are otherwise ignored, the host's network being the host's
business.  Demo `src/host/demo/inethostsetup.ns` (ctests
`host.NewtonInetHostSetup` and `.restart`).

On a restart the store's packages are activated (`TNewtWorld::PreMain`)
just before the host's PreMain hook starts HostLink.ns, so their changes to
the "Packages" soup have gone by and the NIE makes its service registry a
second after them: HostLink.ns looks for it again two seconds after it
starts (before, the host's link was missing after a restart until another
package was installed).

**NTK's glue.**  A package's native code (its protocol parts and its
native-compiled NewtonScript) reaches the ROM through stubs `ldr pc,[pc,#-4]`
followed by an address 0x018xxxxx.  The ROM maps that range onto a table
at physical 0x13000 - just after the patchable jump table - one B
instruction per word, each into a slot of the 2.x jump table, so
0x01800000 + 4k is the function the word at ROM 0x13000 + 4k branches to
(found by fitting every table word's target to a jump-table slot: only
the base 0x13000 makes all 512 of the first ones land on one; confirmed by
the DNS tool's calls being `TOptionArray::OptionAt`, `RemoveOptionAt` and
`InsertVarOptionAt`, and a native function's first calls
`AllocateRefHandle`).  `pkgdisasm.py`/`pkgns.py --rom BUILD` name them.

**The NIE's own scripts do not yet run on the host**: its state machines
are NTK's protoFSM, whose engine - `DoEvent`, `DoEvent_Loop`, the event
queue and the periodic events, 19 functions - is *native-compiled*
NewtonScript (function class 0x232: ARM code in one 61 KB binary, `pkgns.py
--natives`), which a host cannot call; `InetStartUp` stops at the first.
Running `DNSGetAddressFromName` or `InetGrabLink` end to end wants host
re-expressions of those 19 functions, bound to the package's function
objects.

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

## The desktop connection (Dock) - the plan

How a desktop (NCU, NCX, UnixNPI, the Newton Toolkit) connects to a 2.1
MessagePad, established from the ROM (symbols, `classinfo.py`, the strings of
the built-in Connection package - `packages.py build/MP2x00US --extract`):

- **The application.** The Connection application ("Dock" in Extras) is a
  NewtonScript package in the ROM extension (`Connection`, 131 KB, an NTK
  form part).  Its "Connect via" methods are Serial (and Serial at 2400,
  4800, 9600), AppleTalk (`'adsp`/`'atlk`), Modem (`'mods` with the MNP
  options `'mnpa`, `'mnpc`, `'mnpn`) and IrDA (`'irda`).  **There is no
  TCP/IP method** on 2.1, and the Newton Internet Enabler adds none (its
  packages carry no dock code).  A desktop reaches a MessagePad over a
  network only by carrying the serial link: Einstein, the emulator NCX is
  used with, puts the Newton's serial port on a TCP socket (port 3679), and
  the desktop speaks the serial dock protocol - MNP - over it.
- **The docker.** The application makes its connection through a prototype
  frame in the ROM (`0x006482d1`: `Instantiate`, `Connect`, `DoConnection`,
  `ReadCommand`, `WriteCommand`, ... - 31 natives `FConn*`, 0x00096464 -
  0x00097124) over the C++ `TDocker` (0x00092000 - 0x0009c500, about 42 KB,
  140 functions: the command loop, `ProcessCommand`/`ProcessBuiltinCommand`,
  the soup and store sync commands, packages (`ReadPackage`,
  `DoRestorePackage`), passwords, protocol extensions), `TDockerDynArray`
  and `TEzPipeProtocol` (the command header: `'newtdock'` plus the
  four-character command and a long length, padded to four).
- **The transport.** Serial docking runs over MNP: the service `'mnps`
  (`TMNPService`, 0x001197d0) starts `TMNP` (0x00116b14 - 0x0011b838, about
  20 KB: LR/LA/LT/LN/LD frames, DLE framing and CRC-16, the class 5
  compression) over the serial tool `TSerTool`/`TAsyncSerTool` (0x001b8574 -
  0x001b9cd8, 0x0003913c - 0x0003aeac, about 14 KB), which drives a serial
  chip through the `TSerialChip` protocol found in the chip registry
  (`PTheSerChipRegistry`).
- **The host's part** (the one DEVIATION): a host `TSerialChip`
  implementation whose "wire" is a TCP socket - `hal/host/HostSerialChip`:
  it listens on a port (3679, Einstein's, by default), and a desktop that
  connects is a cable plugged in.  Everything above it - the serial tool,
  MNP, the docker, the Connection application - is the ROM's, reconstructed.

The order, each a verified piece:

1. `hal/host/HostSerialChip` over a TCP socket, registered in the chip
   registry (`PTheSerChipRegistry`, reconstructed as far as the tool needs),
   its interrupts (receive available, transmit empty) delivered through
   `hal/host/HostInterruptSources.h`; a test writes and reads bytes through it.
2. `TSerTool`/`TAsyncSerTool` and the `'aser` service (`TAsyncService`): a
   protoBasicEndpoint with `'aser` echoes through the socket (ctest).
3. `TMNP` and `TMNPService` (`'mnps`): an MNP link established with a test
   peer (`tools/dock/mnp.py`, documented) and data through it.
4. `TDocker`, `TEzPipeProtocol`, `TDockerDynArray` and the `FConn*` natives:
   the Connection application docks; `tools/dock/dock.py` (the desktop side:
   MNP, the `newtdockrtdk` handshake, `lpkg` with a fixture package) is
   ctest `host.NewtonDock`.  A real desktop (NCX, UnixNPI with a TCP serial
   bridge) connects to `localhost:3679` the way it connects to Einstein.

**A livelock seen under load (a host bug, fixed).** While the Connection
application's docker was working - its world forked by `DoConnection`, or
blocked in `Connect` reading from the desktop - a script that asked the
package manager for its packages (`GetPackages()`, which `TPMIterator::Init`
answers) was seen twice in about 40 stressed runs (`tools/host/stress.py
--hogs 8`) to leave every task waiting: the forked world on the world's
mutex in `TForkWorld::AcquireMutex`, the script's task in
`TULockingSemaphore::Acquire` under `TPMIterator::Init`, two or three
threads each taking a third of a core.  No lock was held across the
other's wait - `TPMIterator::Init` lets the mutex go before it waits on
`gPackageSemaphore`, as the ROM's does (0x0015c08c-0x0015c0a4) - and the
ROM's code is not at fault: the host's semaphore stub redid a semaphore op
that had *succeeded* whenever the task was switched out at the call's exit
(SWIBoot retries only an op that blocked, 0x003adf04), so a release that
woke a waiter raised the kernel semaphore twice and a wait that had
succeeded waited again, and a wake-up went round the waiters for ever.  A
second host bug showed on the way: `IsSuperMode` answered false at
interrupt level, so the serial receive interrupt's `GetGlobalTime`
(`TSerTool::IHRequest`) was a system call that overwrote the interrupted
task's results - a fork being started lost its start message and
`GetPackages` threw "couldn't fork it over".  Both are in
`docs/host-runtime.md`; ctest `host.NewtonDockGetPackages`
(`src/host/demo/dock-getpackages.ns`) polls `GetPackages()` every tick
while the docker loads a package, and passes 6 of 6 copies beside 8 hogs.
`dock.ns` still waits on the docker's own slots, which is the better way
to wait anyway.

## Beaming - the plan

How one MessagePad beams an item to another, established from the ROM (the
Beam transport's frame, `nsfunctions.py --object 0x4d2565`; the C++ side
decompiled with `decompile.py --class TBeamer`, `TIrProbeTool`,
`TSharpIRTool`; the services' class infos with `classinfo.py`) and sized
with `analysis/classsizes.py` (a function's size is its extent to the next
code symbol):

- **The transport.** Beam is a NewtonScript transport in the ROM (the frame
  at `0x4d2565`, `appSymbol '|beam:Newton|`, over protoTransport @389).  Its
  `SendRequest` and `ReceiveRequest` call two natives, `BeamSend` and
  `BeamReceive` (`ZapSend`/`ZapReceive`, 0x0003d818/0x0003d9b4 - "Zap"
  was beaming's early name; `BeamCancel` is `ZapCancel`), which make a
  C++ `TBeamer` over the transport frame and call back into it
  (`BeamNextItem`, `BeamCommitSend`, `BeamCommitRecv`, `SetStatus`,
  `HandleError`).  The receiving side is asked by the user (the In Box's
  Receive, which is `ReceiveRequest`); the root's `IRConnectRequest` and
  the `'snif` service (`IRSniffService`, `TSniffIRTool`, 2 KB) are the
  sniffing a 2.x Newton does for an incoming beam - NOT in the first plan.
- **`TBeamer`** (0x0003b6f0 - 0x0003de8c, 18 functions, 7.9 KB, plus
  `TBeamerCallback`): `Open` chooses the IR protocol and opens a C++
  endpoint (`CMGetEndpoint`, done) with it; `OpenPipe` puts a
  `TEndpointPipe` (done) on it; `SendNewton`/`ReceiveNewton` move the items
  as NSOF (`TObjectWriter`/`TObjectReader`, done) with the progress
  callback; `SendWizard`/`ReceiveWizard` talk to a Sharp Wizard organiser
  instead (through `PFrameSink`/`PFrameSource`).
- **Which protocol.** `TBeamer::Open` reads the user preference
  `zapCommToolId`.  Unset (the default), it first opens the **probe**
  service `'pkir'` (`IRProbeService`, `TIrProbeTool`, 0x000f6fe0, 34
  functions, 3.7 KB, over `TAsyncSerTool`), which alternates the IR
  hardware between IrDA SIR and Sharp's ASK and sends each kind of test
  frame (an IrDA TEST frame through `TIrSIR`/`TIrLAPPutBuffer`; a Sharp
  control packet) until the other side answers, and reports what it found
  in a `TCMOSlowIRProtocolType` option (`IdentifyProtocol`, an
  `nOptMgmt` get).  Then it closes that endpoint and opens the real one:
  `'irda'` when the peer answered IrDA (bit 3 of the protocol type - two
  2.1 MessagePads), `'slir'` otherwise (an older Newton).  Set, the
  preference names the service outright, skipping the probe.
- **Sharp IR, `'slir'`** (`TIRService` → `TSharpIRTool`, 0x001e07b4, 56
  functions, 9.2 KB, over `TAsyncSerTool`): the Newton 1.x / 2.0 beaming
  protocol - lead-in, control, negotiate and data packets, a state machine
  (`NextState`, 1.2 KB), timers as delayed messages to itself; options
  `TCMOSlowIRConnect`, `TCMOSlowIRProtocolType`, `TCMOSlowIRStats`,
  `TCMOSlowIRBitBang`, `TCMOSlowIRSniff`.
- **IrDA, `'irda'`** (`TIrDAService` → `TIrDATool` 5.2 KB, `TIrGlue` 6.0
  KB, `TIrLAP` 11.2 KB, `TIrLAPConn` 3.3 KB, `TIrLMP` 1.3 KB, `TIrQOS` 1.7
  KB, `TIrSIR` 1.7 KB, `TIrLAPPutBuffer` 0.5 KB, `TIrDscInfo` 0.5 KB,
  `TIrStream` 0.4 KB, `TIrCRC16` done; options `TCMOIrDADiscovery`,
  `...ConnectionInfo`, `...ReceiveBuffers`, `...LinkDisconnect`,
  `...ConnectUserData`, `...ConnectAttrName` - about 32 KB): discovery,
  the link (IrLAP) and a single IrLMP connection, the beamer asking for
  one 1 KB receive buffer and a 20-second link-disconnect time.
- **The hardware.** Every one of these tools is a serial tool: it claims
  the chip at `'infr'` (`kHWLocBuiltInIR`, `TSerTool::ClaimSerialChip` -
  done) and switches its IR mode with the HAL option `'irlk'`
  (`THMOSerIRLinkConfig`: mode 0 Sharp ASK, 1 IrDA 1.6 µs, 2 IrDA 3/16, 3
  either; the auto-receive flag, and a status bit saying which kind the
  last byte came as).  On the MessagePad that is the Voyager chip's IR
  port; `TIRQTimer` (a platform timer the ADC and battery use too) is not
  part of the IR stack.

**The host's part** (the one DEVIATION): `hal/host/HostIRChip`, a
`TSerialChip` registered at `'infr'` whose medium is a TCP connection to
another host Newton (`newton --ir-peer listen:PORT` on one,
`--ir-peer HOST:PORT` on the other).  Every byte crosses as a pair - the
byte and the modulation it was sent with (ASK or IrDA, from the chip's
`'irlk'` mode) - and a receiver in the other mode does not hear it (in
auto-receive it hears both and notes which in the status), so the probe
meets the same behaviour it meets on the air.  A receiver that is
transmitting does not hear anything (IR is half duplex).

The order, each a verified piece with a test:

1. `hal/host/HostIRChip` and `--ir-peer`: two chips over a socket, the
   modulation rule (`hal.HostIRChip`).
2. `TSharpIRTool` and `TIRService` (`'slir'`) with the slow IR options:
   an endpoint on each of two chips connects, listens and moves data
   (`comms.SharpIR`).
3. `TBeamer`, `ZapSend`/`ZapReceive`/`ZapCancel`: with `zapCommToolId`
   set to `"slir"`, one host `newton` beams a Note to another (ctest
   `host.NewtonBeam`: two processes, both scripts polling, `HostQuit`).
4. `TIrSIR`, `TIrLAPPutBuffer`, `TIrProbeTool`, `IRProbeService`
   (`'pkir'`): the default path's first half - between two 2.1s the probe
   answers IrDA (both sides send and echo IrDA TEST frames themselves).
5. IrDA (`'irda'`): `TIrSIR` upward to `TIrDATool` - two 2.1 MessagePads
   then beam over IrDA as the ROM would.  Done: `comms.IrDA`, `host.NewtonBeamIrDA`.

### A beam that fails

TransportNotify's SendRequest and ReceiveRequest return when the beam
has ended.  A failed beam leaves the note in the Out Box, the error in
its entry's `error` slot and the transport idle; a beam nobody answers
gives up after about 100 s.  Under heavy load the IrLAP connection can
fail by the ROM's own timers (-38506, kIrDAErrLAPFailedConnection, and
the receiver's listen then -38001); `demo/beam-irda-send.ns` and
`beam-irda-receive.ns` send and listen again, up to three times, as a
user would (ccdec403).

## The web browsers

NetHopper and Newt's Cape browse over the built-in NIE and the host's
own TCP/IP (ctests `host.NewtonNetHopper`, `host.NewtonNetHopperNewtsCape`,
`host.NewtonNewtsCape`; `tools/host/httpserve.py` is the web server).
Newt's Cape's behaviour, kept as it is:

- **It loads a page without its images unless asked.**  View > Load with
  Images… (`getURL(url, {loadImages: true})`) scans the page's HTML in
  "HTMLCache:NewtsCape" and opens a list "Images: N"; the user ticks
  pictures (All) and picks Load from the action menu.  The preferences
  have no loadImages slot by default.
- **A screen-wide page sits at x = -3.**  Copperfield's
  `BuildDisplayParams` computes dpCu.left = (appAreaWidth - page width -
  6) div 2 = (320 - 320 - 6) div 2, so the first three columns are off
  the screen, as on a MessagePad.
- **Return on the location line breaks Load with Images.**  `currentURL`
  trims the line's own string in place and answers that same string; the
  ROM's `RealDoCommand` then inserts the return into it (a field with no
  default button, viewJustify 48), so the page is cached under a URL
  ending in a return and the later lookup of the trimmed URL misses.
  `demo/newtscape.ns` uses File > Open Location instead.

## Devices that bring their own package

A device plugged into the serial port can carry the package that drives
it.  When the interconnect pin says something was plugged in, the comm
manager (`SCPCheck`, or a client's `CMSCPLoad`) starts the docking loader,
the 'scpl task (`comms/SCPLoader.h`), which opens the port through the
framed serial service ('fser, 9600 bps) and waits half a second for the
device to say who it is.  The two ends talk in *connection-protocol*
messages (`comms/CPMessages.h`, ROM 0x495e4-0x49cd0): a message is one
frame holding a run of tuples - a four-character tag, a big-endian length
and the data - ended by an `'nofm'` tuple.

    device -> 'd_id' (type, manufacturer, version)
    Newton -> 'n_id' (machine type, manufacturer, ROM version),
              'sire' ('pack', 0, 0), 'csre' (0x7c: the five speeds it takes)
    device -> 'csrp' (the speed chosen), 'sirp' ('pack', version, size)
    Newton -> 'rese' ('pack', version)
    device -> 'pack' (size), the package, 'nofm'
    Newton -> 'abrt' (1)

The device is recorded as the comm manager's last device and announced as
a 'dnot' system event.  Its package is asked for only when the load was for
any device (`'****'`) or for its type, and no service of its type is
registered (`CMGetServiceVersion` answering -26002; the reconstruction's
constant had been -26030, which would have made every device look served).
The package is stored on the internal store (`StorePackage`) and the newt
world sent a `TSCPEvent` (`newt/SCPEvents.h`): the previous device's
package removed (`RemovePackage`) and the new one registered
(`RegisterNewPackage(package, store, true)`) - unless it is the same device
again, when the copy just stored is deleted.  Last, a service of the
device's type with an `'auto'` capability is opened on the port and left
running.  A load tries the external port, then (nobody having answered) the
modem's, each up to the tries asked for, at most five.

`tools/dock/scpdevice.py` is such a device on the host's serial port;
`src/host/demo/scpload.ns` plugs it in with `HostInterconnect(1)` (what the
interconnect handler, `TICHandler`, NOT YET, would send) and waits for the
package: ctest `host.NewtonSCPLoad`.  ROM bugs kept: `TCPReadMessage::Init`
allocates a new buffer every time the port is opened and never frees the
old one; `ReadTuple` reads a tuple's data whatever its length into a
0x100-byte buffer; a comm-manager load that fails after its message is made
leaves it in flight, so every later load answers busy.  ROM quirk: a device
of a type other than the one asked for still has its package fetched.

## Status

| piece | state |
|-------|-------|
| options (`TOption`, `TOptionExtended`, `TSubArrayOption`, `TOptionArray`, `TOptionIterator`) | done: `comms/Options.h`, `test_Options` |
| `CBufferList`, `CShadowBufferSegment` (what a tool's data comes in) | done: `utility/BufferList.h`, `utility/ShadowBufferSegment.h`, `test_BufferList` |
| the comm tool: `TCommTool`, the requests and replies, the tool's own options, `StartCommTool`, `ServiceToPort` | done: `comms/CommTools.h` |
| the host TCP tool and the sockets | done: `comms/host/HostTCPTool.h`, `hal/host/HostSockets.h`; **M0** passes (`test_CommTool`) |
| the comm manager: `TCMWorld`, `TCMEventHandler` (starting a service by its `serv` capability), `TStartInfo`, `TAsyncServiceMessage`, `OpenCommTool`, `CMStartService`, the last device and package, `CMGetServiceVersion` | done: `comms/CommManager.h`; **M1** passes (`test_CommManager`) |
| the host's `inet` service (`THostInetService`) | done: `comms/host/HostServices.h` |
| the endpoint: `TEndpoint` (the DDK's interface, its methods virtual - `comms/Endpoint.h` replaces the DDK's header), `TEndpointEventHandler`, the endpoint events, `TEndpointClient`, `CMGetEndpoint`; `TSerialEndpoint` and the `TCommTool...PB` parameter blocks | done: `comms/Endpoint.h`, `comms/SerialEndpoint.h`; **M2** passes (`test_Endpoint`: Open, Bind, Connect, Snd, Rcv, Disconnect, UnBind, Close against the echo server) |
| the docking loader (`TSCPLoader`, `TCMWorld::SCPLoad`, `TCMSCPAsyncMessage`, the connection-protocol messages `TCPReadMessage`/`TCPWriteMessage`/`TCP*Tuple`, the newt world's `TSCPEvent`/`HandleSCPEvent`) | done: `comms/SCPLoader.h`, `comms/CPMessages.h`, `newt/SCPEvents.h`; ctest `host.NewtonSCPLoad` (see "Devices that bring their own package") |
| the ROM's own services (`RegisterROMProtcols`) | done for the reconstructed ones, handed over with `CMAddROMServices` (DEVIATION); NOT YET: `RegisterNetworkROMProtocols` (the host's own services stand in), P3, LocalTalk, Keyboard, VRemote, IRSniff, `PMuxServiceStarter` |
| `TICHandler` (the interconnect pin: plugging in starts the docking loader, then AutoDock), `InitializeCommHardware` | NOT YET (a script's `HostInterconnect(state)` sends the comm manager what the handler would) |
| `CMemObject` (a status request's answer goes through `TUSharedMem` meanwhile) | NOT YET |
| `TPCommTool`/`StartCommToolProtocol` (a tool as a `TCommToolProtocol`) | NOT YET |
| marshalling out (`MarshalArguments`: a script's `{arglist, typelist}` into bytes, in the MessagePad's byte order) | done: `frames/MarshalOut.cpp`, `test_Marshalling` (the NIE's `itrs` data) |
| the frame translators: `PFrameSink`/`PFrameSource`, `PScriptDataOut`/`In` (a value by its form - string, char, number as a big-endian long, bytes, binary, template), `POptionDataOut`/`In` (option frames to a `TOptionArray` and back; a `'service` frame becomes a `'sid '` option naming it), `GetDataForm`, `InitTranslators` | done: `comms/Translators.h` (library `comms_script`), `test_Translators`.  the flatteners `PFlattenPtr`/`PUnFlattenPtr`/`PFlattenRef`/`PUnFlattenRef` (NSOF over `utility/Pipes.h`'s `CPtrPipe` and `stores/RefPipe.h`'s `CRefPipe`) are done too - the endpoint's `'frame` form, `echo.ns`; and `PStreamInRef`/`PStreamOutRef` (NSOF through `comms/EndpointPipe.h`'s `TEndpointPipe`) |
| the NewtonScript endpoint: `TNewScriptEndpointClient` (protoBasicEndpoint, @383) and its 22 `CINew*`/`CIRequestsPending` natives - requests synchronous or queued with their callbacks, output by form, the input spec (form, termination by byteCount/endSequence/useEOP, filter, target, rcvOptions, partialScript), inputScript and completionScript, exceptions to the endpoint's exceptionHandler | done: `comms/NewScriptEndpoint.h`, registered by `RegisterCommsNatives`; the newt world starts the comm manager and the host services (DEVIATION: the ROM's loader does); **M3** passes - `src/host/demo/echo.ns` (ctest `host.NewtonEcho`, `newton --tcp-echo port` runs the echo server, `comms/host/HostEchoServer.h`).  protoStreamingEndpoint too (`comms/StreamingEndpoint.h`: `TStreamingEndpointClient`, `TStreamingCallBack`, the `CIS*` natives - StreamOut/StreamIn of a whole object as NSOF, ctest `host.NewtonStream`).  A configuration asking for the modem service goes through the modem navigator first, as the ROM's |
| the DNS service (**M4**, `dnst`): `THostDNSService`/`THostDNSTool` answering the NIE's `dnsq`/`rrcd` requests through the host's resolver | done: `comms/host/HostDNSTool.h`, ctest `host.NewtonDNS`; NOT YET: the NIE's own domain manager (its protoFSM engine is native-compiled) |
| the link controller (**M4**, `ictl`) | NOT YET |
| the serial chips (Dock layer 1): `TSerialChip`, `PSerialChipRegistry` and the ROM's `PTheSerChipRegistry`; the host's external port over a TCP socket (`THostSerialChip`, port 3679; the desktop's bytes paced at the line's speed) | done: `hal/HALSerialChip.h`, `hal/host/HostSerialChip.h`, ctest `hal.HostSerialChip` |
| the serial tools (Dock layer 2): `TCircleBuf`; the serial options; the fast timers (`TFIQTimer`) and `TDelayTimer`; `TSerTool` (claiming, binding and turning on a chip; the serial options), `TAsyncSerTool` (the byte stream a byte at a time from the chip's interrupts, XON/XOFF and CTS/RTS flow control, break framing, the serial events, statistics; DMA where a chip has it) and the `'aser` service `TAsyncService`; the name server's resource arbitration (a chip is claimed through it) | done: `utility/CircleBuf.h`, `comms/SerialOptions.cpp`, `hal/FIQTimer.h`, `hal/DelayTimer.cpp`, `comms/SerialTool.h` (library `comms_serial`), `os600/user/NameServer.cpp`; ctests `utility.CircleBuf`, `hal.FIQTimer`, `comms.SerialTool` (an `'aser` endpoint echoed through the socket, both buffers wrapping) |
| MNP (Dock layer 3): `TFramedAsyncSerTool` (`'fser`: SYN DLE STX, DLE doubled, DLE ETX, CRC-16 each way), `TMNP` (the link request negotiated as originator or acceptor, LT frames sent in pieces while they fill, LA with credit, class 4's short headers, retransmission and the one-second timers, LD, the termination procs), class 5 compression, `TMNPService` (`'mnps`) | done: `comms/SerialTool.h`, `comms/MNP.h`, `comms/MNPClass5.cpp`; ctests `comms.MNP`/`comms.MNPLongHeaders` (an `'mnps` endpoint connects to `tools/dock/mnp.py`, the desktop end, and its data comes back), `comms.MNPClass5`; V.42bis (`comms/V42bis.cpp`: the ROM's BTLZ coder over its own 0x39d4-byte block, big-endian halfword node arrays - N2 up to 1024 each way, 2048 one way - cross-checked against `tools/dock/v42bis.py`, written from the recommendation: ctests `comms.V42bis`, `comms.V42bisRoundTrip`, and `comms.MNPV42bis`, a link negotiating it with `mnp.py --v42bis` and its data back in compressed mode).  NOT YET: V.42bis's internal-buffer mode |
| the docker (Dock layer 4): `TEzEndpointPipe` and the modem navigator hook; `TEzPipeProtocol` (the `'newt' 'dock'` headers); `TDocker` - `Connect` (`'rtdk'`, then the desktop's answer), `DoConnection` (the world forked), the package loader's session (`'lpkg'`: `CompatabilityHacks`, `ReadPackage` over `SuckPackageThruPipe`, `'dres'`, `'disc'`), the protocol extensions (`TDockerDynArray`), stopping, aborting and cleaning up; the `FConn*` natives the Connection application's `dtEndpoint` calls, `ConnBuildStoreFrame` (with `StoreGetPasswordKey`); options a script makes rewritten into the host's layout (`comms/HostOptionLayouts.h`, DEVIATION: a FastInt/ULong is pointer-sized on the host) | done: `comms/EzEndpointPipe.h`, `comms/Docker.h` (library `comms_dock`); ctests `comms.EzEndpointPipe`, `host.NewtonDocker`, `host.NewtonDock` (the Connection application's autodock over the host serial port to `tools/dock/dock.py`, which loads `fixtures/packages/fonts/monaco.pkg`).  the docking session: the handshake (`'dock'`, `'name'`, `'dinf'`/`'ninf'`, `'wicn'`, `'stim'`), the password exchange over the ROM's DES (`utility/DES.h`; the desktop's copy is `tools/dock/newtondes.py`), every command of `ProcessCommand` - packages (load, list `'gpin'`, restore, remove), session kinds, time, timeout, icons, cancelling, stores and soups (choose, make, info, back up `'bksp'` as runs of ids, send), cursors (`'qury'` and the rest, `TCursorArray`) and entries (add, return, change, delete; `ConvertEntry` for a 1.x Newton's, `IsDuplicateEntry` for a selective restore), the class inheritance, sync options, test echoes, remote function calls, the Connection application's slips (`CallConnectionApp`), protocol extensions either side; the application's own `ReadCommand`/`WriteCommand` natives and the keyboard passthrough - all exercised by ctest `host.NewtonDockSession` (`dock.py --session`, `tools/dock/nsof.py`; the slip tapped and the keys typed by `src/host/demo/dock.ns`).  NOT YET: `'rpat'` (a system patch installed into the ROM) and `BackupPatches` (`'gpat'` answers the host's none).  Two ROM bugs worth knowing: a backup cannot be cancelled (`CheckCancel`), and `'rtst'` writes its header twice (`docs/curiosities.md`) |
| the host IR port (Beaming layer 1): `THostIRChip` at `'infr'` over a TCP connection to another host (`newton --ir-peer listen:PORT` / `HOST:PORT`), the modulation rule ('irlk': ASK or IrDA, auto-receive and its status), half duplex, `THMOSerIRLinkConfig` | done: `hal/host/HostIRChip.h`, ctest `hal.HostIRChip` |
| Sharp IR (Beaming layer 2): `TSharpIRTool` ("SlowIR", `'slir'`: lead-in, control, negotiation and data packets over `TAsyncSerTool` with the port in ASK mode; the negotiation - an offer, the answer, the protocol and speed agreed (two 2.x Newtons agree the Senior protocol, 4, at 19200); a data packet after ENQ/SYN, ACKed or NAKed, three tries, 0x200 bytes at most, a frame's last numbered 0xffff; the timers as delayed messages to the tool's own port), `TIRService`, the slow IR options | done: `comms/SharpIRTool.h`; ctest `comms.SharpIR` (two host IR chips in one process, one listening and one connecting, a stream, a frame, a frame of three packets and a reply).  A ROM behaviour worth knowing: one counter numbers the packets both ways, so a reply after a stream put is refused as out of sequence - only the end of a frame starts both ends again |
| the beamer (Beaming layer 3): `TBeamer` (the protocol chosen - the probe `'pkir'` unless the preference `zapCommToolId` names a service - an endpoint opened in a fork of the world and connected or listening, a pipe over it; the item count, then each item's header frame, the other side's answer whether it has the room, the item as NSOF with the progress told to the status dialog; the Wizard translators' path, which the ROM does not supply), `TBeamerCallback`, the Beam transport's natives `BeamSend`/`BeamReceive`/`BeamCancel` (`ZapSend`/`ZapReceive`/`ZapCancel`), the IrDA options' constructors | done: `comms/Beamer.h` (`NEWTON_TRACE_BEAM`); ctest `host.NewtonBeam` (`tools/host/twonewtons.py`: two `newton`s with `--ir-peer`, `src/host/demo/beam-send.ns` routes a note to Beam and sends it from the Out Box, `beam-receive.ns` receives it into the In Box - Sharp IR, `zapCommToolId` "slir").  With `class: 'paperroll` in its body the note does not stay in the receiver's In Box: the In Box's AutoFunction (@0x4b3ef5) calls the Notes application's `AutoPutaway` (autoPutawayEnabled: its PutAwayScript), which files it straight into the Notes soup - the ROM's behaviour, as the Beam transport's "Put away automatically" preference (`dontAutoPutAway`) says; the classless body the demo sends stays in the In Box (observed; the Notes PutAwayScript not traced), so the demo's has none and the note stays in the In Box to be found.  The default path too: ctest `host.NewtonBeamIrDA` (`beam-irda-send.ns`/`beam-irda-receive.ns`, `zapCommToolId` nil - the probe answers IrDA and the note goes over `'irda'`) |
| the IR probe (Beaming layer 4): `TIrProbeTool` ("IrProbe", `'pkir'`: connecting, four IrDA TEST frames with a tenth of a second for each echo, then a Sharp offer of protocols 0xf (or every third time an ENQ), round again for about two minutes; listening, half a second at a time in auto-receive - a TEST frame echoed answers IrDA, two ENQs or an offer without IrDA answer Sharp IR; the answer is its 'irpt' option), `IRProbeService`; and IrDA's SIR framing under it, `TIrSIR` (extra BOFs, BOF, the frame escaped, the IrDA CRC-16, EOF; a frame received for this station into a buffer segment, the medium busy meanwhile) with `TIrLAPPutBuffer` | done: `comms/IrProbeTool.h`, `comms/IrSIR.h`; ctest `comms.IrProbe` (probe against probe answers IrDA both ends; probe against a Sharp IR listener answers 7).  So the default beam between two 2.1s now picks `'irda'`, which is NOT YET |
| IrDA (Beaming layer 5): `TIrDATool` ("IrDA", `'irda'`: connecting, discoveries for two minutes until a device with the service hints asked for answers, this side's class and LSAP registered in its IAS database, the other's looked up in the other's IAS (unless given) and connected to; listening, registered and listened for two minutes, then accepted; the IrDA options, the speed, `'irpt'` 4 and the statistics), `TIrDAService`; the stack in `comms/irda/`: `TIrStream` and the event blocks (a request's block goes down the layers and comes back as its answer; the glue's run queue runs the streams from the tool's task), `TIrGlue`, `TIASClient`/`TIASServer` (GetValueByClass over an LSAP connection to LSAP 0), `TLSAPConn` (the LM connect and its confirm with the user data, a thirty-second connect timer, data with the two-byte LMPDU header, the disconnect reasons as errors), `TIrLMP` (the requests routed; two devices with one address asked to pick another), `TIrLAPConn` (the connections on the one link, frames to the get waiting for them), `TIrLAP` (NDM discovery in XID slots, answering another's discovery in a slot picked at random; SNRM/UA with the `TIrQOS` negotiated, the lower address winning a race; NRM as primary or secondary - windows of I frames with 3-bit sequence numbers, RR/RNR/REJ, FRMR, DISC/RD/UA/DM, the F, WD and turn-around timers, the link disconnect threshold as N1/N2; a TEST frame echoed) | done: `comms/irda/IrDATool.h`, `IrGlue.h`, `IrIAS.h`, `IrLMP.h`, `IrLAP.h`, `IrStream.h` (`NEWTON_TRACE_IRDA`: every event and every frame); ctest `comms.IrDA` (two host IR chips: discovery, the IAS lookup of the listener's class, the connection at the agreed 512-byte frames, eleven bytes and 1500 (three I frames) one way and five back, a disconnect).  ROM bugs kept: with more than one address conflict a discovery never ends (the block goes back to the link as a reply); a failed put allocation releases the wrong blocks; a lookup whose IAS server cannot be made releases the block its client still has.  A host detail: device addresses are 32 bits, the ROM's -1 `kIrAllDevices`.  The default beam goes over it: ctest `host.NewtonBeamIrDA` |
| the Newton Toolkit's inspector: `TNTKNub` (the debugger nub: `'newt' 'ntp '` messages - `'cnnt'`/`'okln'`, `'code'` and `'lscb'` code blocks run, `'pkg '`/`'pkgX'`/`'stou'`, `'text'`, `'fobj'`, `'fstk'`, `'eerr'`/`'estr'`/`'eref'`, `'eext'`/`'bext'`, `'dpkg'`, `'term'`), its task `TNTKTask` ('ntk ') and `TNTKEndpointClient` moving bytes between the endpoint and two `TTaskSafeRingBuffer`s, the REP translators `PNTKInTranslator`/`PNTKOutTranslator` and the tethered listener's `PSerialInTranslator`/`PSerialOutTranslator`, `TREPEventHandler` (the REP's idler, made by `NTKInit` at boot), `NTKStackTrace`; the natives `ntkListener`, `ntkDownload`, `NTKSend`, `NTKAlive`, `ntpTetheredListener`, `ntpDownloadPackage` | done: `comms/NTK.h` (library `comms_ntk`); ctest `host.NewtonNTK` - `tools/ntk/inspector.py`, a desktop inspector over MNP on the host serial port (3679), installs and deletes a package, evaluates expressions (hand-assembled code blocks calling the Newton's own `Compile`), reads the REP's text, NTKSend's object and all three exception forms, then answers `ntkDownload`.  ROM bugs kept: `NTKInit` registers `PStdioInTranslator` twice and never `PStdioOutTranslator`; the nub's destructor frees the buffers the task may still write (it is told to finish, not waited for - so its name is still registered for a moment and an `ntkDownload` straight after answers -10068); `ntkDownload` deletes a running listener's nub without stopping it; a `'code'` answer carries the command's length, not the result's.  NOT YET: the AppleTalk (ADSP) connection, the Hammer translators |
| protoEndpoint (@174), the 1.x NewtonScript endpoint: `TScriptEndpointClient` - the 1.x option frames (`{label, type, opCode, data}`, a 'service naming its service; 'rout, 'siop, 'iflc/'oflc and 'mdo  from named slots - `TCMOModemDialing` and `SetDialingOptionsFromPrefs`), Connect/Listen synchronous and then asynchronous, output with 1024 bytes outstanding (Yield over a `TPseudoSyncState`), OutputFrame (NSOF after a length, through `CNullPipe`), input specs (byteCount, endCharacter, recvFlags' end of packet, discardAfter, nullProxy, sevenBit, partialScript and its frequency; 'string, 'frame, 'raw or bytes), completion errors to the exceptionHandler as evt.ex.comm; the 41 CI... natives | done: `comms/ScriptEndpoint.h` (library `comms_dock`), `comms/ModemOptions.h`, `utility/Pipes.h`'s `CNullPipe`; ctest `host.NewtonProtoEndpoint` (`src/host/demo/protoendpoint.ns` against `--tcp-echo`).  Host bug fixed on the way: `TSerialEndpoint::HandlePutReply` did not give the client the count of bytes put (the ROM copies it from the reply).  ROM bugs kept: a raw-binary option's bytes are copied from the option frame, not its data; the dialing option's five switches take the low byte of the Ref; `CIJustListen` passes the options as the address; the destructor tests the frame against 0, not nil; `startCCL` leaves `gCCLState` pointing at its dead state.  NOT YET: the CCL modem scripts `startCCL`/`stopCCL` wait for |
| the trace frame (@661, "Client Trace": `cfinstantiate`, `cfrecord`, `Dispose` - CFInstantiate, CFRecord, CFDispose) over a `THistoryCollector`, and `translate` (FTranslate: 'flattener, 'unflattener, 'unflattenNoCode through PFlattenRef/PUnFlattenRef) | done: `comms/CommTrace.cpp`, `utility/EventCollector.h` (the TEventCollector protocol and THistoryCollector, registered by the loader's `InitEvents`); ctests `host.NewtonCommTrace`, `utility.EventCollector`.  ROM bugs kept: `AddDescriptions` answers false even when it added them; CFInstantiate gives the collector the whole count of trace elements though only those with a trace string are entered, and writes the count before checking its table; `cfrecord` on a frame with no collector records through nil (a DEVIATION on the host: nothing); `translate` throws a stale MemError after a translation that went well |
