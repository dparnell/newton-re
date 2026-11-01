# The T-Deck Pro as a phone: calls, messages and (perhaps) faxes

*Begun 2026-10-12, branch `tdeck-cellular`.  The owner's board is the
T-Deck Pro's 4G variant: a SIMCom A7682E on UART1.  The owner's earlier
firmware for the same board, lilyphone1 (PlatformIO, Arduino, LVGL), made
and took calls and sent and received texts on it; its modem service
(`src/peripherals/peri_modem.cpp`) is where the AT dialect below comes
from.*

## What NewtonOS 2.1 already has

The ROM carries a whole telephony framework (established by reading
`romsrc/`, the decompiled ROM objects):

- **The Calls application** (Extras) is only the call log: the data
  definition `'calllog` under `'callapp` (`romsrc/objects/misc-004.ns`).
- **The Phone transport**, `|PhoneHome:Newton|` (`basicCallTransport`), titled
  "Phone", owner `'callapp`, does the work.  The Names card's Call action
  (`kDoCallSlip`, Cardfile) opens its routing slip.
  - *Outgoing*: `SendRequest` → `InstallEndpoint` → `FinishDialing`, which
    calls `callEndpoint:MakeOutgoing(number)`; statuses "Dialing ^0…" to
    "Call to ^0 in progress…".
  - *Incoming*: `EventIncoming` plays the ring sound (@102), makes a log
    item, asks `callEndpoint:GetCallerId()` when the provider has caller
    ID, and shows "Call from ^0…" with a **Take Call** button, which is
    `receiveRequestComplete` → `AnswerIncoming`.  `StartAutoReceive`
    ("Ready for call…") listens when the preference `phoneAutoReceive`
    is set; `EventOffHook`/`EventOnHook` follow the handset.
- **The call endpoint** is `@662` (`protoTapiEndpoint`, `'tapiEndpoint`, a
  protoBasicEndpoint @383 with the service option `"taps"`):
  `MakeOutgoing` (option `"outg"`), `AnswerIncoming` (`"answ"`),
  `hangup`, `SendDigit`, `Hold`/`UnHold`, `Transfer`, `Forward`,
  `GetCallerId` (a `"rout"` address option read back) and `SetSpeaker`.
  Its `eventHandler` takes event code 7 with data 1 incoming, 2 ringer, 4
  clear display, 5 off hook, 6 on hook, 8 conversation, 9 dial tone, 10
  ringback, 11 busy, 12 reorder.
- **Providers are pluggable**: `RegCommConfig(sym, frame)` puts one in
  `CommConfigRegistry`; `GetTAPICommFrame` reads it back (none: `{name:
  "Telephone"}`).  A provider says what it can do in its `config`:
  `supportsMakeOutgoing`, `supportsSendDigit`, `supportsEventIncoming`,
  `supportsCallerId`, `supportsAnswerIncoming`, `outgoingNeededDelay`,
  `supportsLineCurrentSense`.  The Phone preferences' "Using" picker
  (`protoTapiPicker`) lists the registered ones.
- **The ROM registers none.**  A MessagePad alone has only "Telephone":
  the number dialled as tones from the speaker held to a handset
  (`RawDial`).  The modem tool (`comms/ModemTool.h`) has the TAPI service
  option (`kCMOTAPIService`, the off-hook and on-hook commands `X1H1`,
  `H0X`), for a Hayes voice modem.

So a call needs no new application: a cellular **provider** registered with
`RegCommConfig`, its service a comm tool that answers the `taps` endpoint's
options over the module, makes the ROM's own Phone transport and Calls log
make, take and log calls (step 2).

**Messages**: the ROM has nothing for SMS - a new transport is needed (texts
received into the In Box, sent from any routing slip; step 3).

**Faxes**: the ROM's fax tool (`comms/fax/FaxTool.h`) sends and receives
over a Class 1/2 modem.  An LTE module carries no circuit-switched data,
and the A7682E's voice audio goes to its own codec pins, not to the
ESP32-S3 - so no fax over the module (AT+FCLASS=? to be asked of the real
one).  An internet fax service over the data connection would be the way
(step 4).

## Step 1: the module driven (2026-10-12)

**The engine** (`src/hal/host/HostCellular.h`, `.cpp`) is portable C++: one
thread owns the module's byte link and speaks AT to it; the Newton's side
asks through a request queue and reads a status snapshot and two queues
(call events, received texts).  It has none of the Newton's headers (it is
its own library, `hal_host_cellular`, because `<thread>` and `<mutex>` reach
`<locale.h>`, which `src/intl/Locale.h` would shadow).  Kept from
lilyphone1:

- configuration: `ATE0`, `AT+CMEE=1`, `AT+CLIP=1`, text mode
  (`AT+CMGF=1`) with the GSM character set, messages stored in the SIM and
  their slots told (`AT+CPMS="SM",...`, `AT+CNMI=2,1,0,0,0`), the coding
  scheme with each message (`AT+CSDH=1`), the network's time kept and its
  changes told (`AT+CTZU=1`, `AT+CTZR=1`);
- a URC arriving in another command's answer (RING, +CLIP, +CMTI, VOICE
  CALL: BEGIN/END, NO CARRIER, BUSY, NO ANSWER, the time-zone ones) is
  handled where it is read, never taken for the answer; a +CMTI only notes
  the slot, read and then deleted between commands;
- a UCS2 body (anything outside GSM-7: curly quotes, emoji) arrives as hex,
  told apart by the coding scheme; a message is sent as GSM-7 when it is
  plain ASCII, as UCS2 otherwise (`AT+CSCS="UCS2"` *and* `AT+CSMP`'s dcs
  8, the number in hex too, the defaults put back after), folded to ASCII
  as a last resort; `AT+CMGS` is ended by CR alone;
- `AT+CLCC` every 2 s while a call is up (the URCs alone do not reliably
  say when it ends);
- the network's time read with `AT+CCLK?` once registered and when the
  network says it moved.

Not kept: lilyphone1's UDP relay over the data context and its
notification tones.  New: the SIM's state (`AT+CPIN?`: no SIM, locked),
a RING told once its +CLIP has come (or half a second without), events for
the Phone transport-to-be (ring, connected, ended, busy, no answer).

**The natives** (`HostCellularNatives.cpp`, listed in `HostCellular.h`):
`HostCellAvailable`, `HostCellStatus`, `HostCellDial`, `HostCellAnswer`,
`HostCellHangUp`, `HostCellSendText`, `HostCellSendState`,
`HostCellNextText`, `HostCellNextEvent`, and `HostCellCommand(command)` -
a command sent as it is, for looking at what a module can do from the
board's NewtonScript prompt (`HostCellCommand("AT+CLAC")`).  `callState`,
not `call`: `call` is a NewtonScript reserved word.

**The desktop's module** is `tools/modem/fakecell.py` on `newton
--cell-port 0` (`HostCellularSocket.cpp`): the A76xx dialect above, with
commands of its own a script drives the far end with (`AT+FAKERING`,
`AT+FAKETEXT`, `AT+FAKEHANGUP`).  ctest `host.NewtonCellular`
(`src/host/demo/cellular.ns`): registered, a stored message picked up,
a call made and hung up, one refused busy, one rung in with caller ID,
answered and hung up by the far end, a message received with an emoji
(a surrogate pair) and two sent (GSM-7 and UCS2).  ctest `hal.Cellular`
tests the parsing (UCS2, time stamps, +CMGR headers).

**The board** (`src/host/esp32/main/tdeck_modem.cpp`): UART1 at 115200
(our TX GPIO 10, RX GPIO 11), the module's supply GPIO 41 (an SGM6609
boost), its power key GPIO 40 - a button: a 50 ms press, as LilyGO's
`examples/A7682E/test_AT` gives it, and a second's press when that gets no
answer.  GPIO 7-9 (RI, DTR, RESET) are left alone.  The engine runs on a
task of the board's own (8 KB internal stack, core 0); its large buffers
are `HOST_LARGE_STATIC`.  The network's time sets the board's clock
(`HostDeviceClockSetTo`), and the Newton's follows as it does after SNTP.

What the board showed:

- **GPIO hold.** The supply pin is released from GPIO hold first
  (`gpio_hold_dis`): lilyphone1 latches its power pins for deep sleep, and
  a held pin outlasts a reset and ignores every write.
- **No battery, no module.** The SGM6609 boost that feeds the module runs
  from the battery: on USB power alone GPIO 41 reads high and the module
  stays dark (no LED, no answer).  The owner found this from the circuit.
- lilyphone1's own start is not what its comments say: its `setup()`
  drives the power key high early and holds it through the I2C scan and
  the display's start (seconds) before `A7682E_init`'s 50 ms pulse - the
  press that really starts the module there is the long one.

With the battery in (2026-10-12) the module answered at once, was
configured, registered on its home network (Optus, -77 dBm) and set the
board's clock from the network's time (zone +11 h).  Asked from the
board's NewtonScript prompt with `HostCellCommand`:

- `ATI`: Model A7682E, Revision A7682M7_V1.11.1, `+GCAP: +CGSM,+FCLASS,+DS`;
- `AT+FCLASS=?`: `+FCLASS: 8` - voice only, **no fax class**: a fax cannot
  go through the module (step 4 is an internet fax service, or nothing);
- `AT+CSDVC=?`: `(1,3)` - the audio to the handset or the speaker;
- `AT+COPS?` named the operator by its number ("50502") until the engine
  asked for names (`AT+COPS=3,0`).

## Step 2: calls through the ROM's own Phone transport (2026-10-12)

The cellular module is a telephone provider now, and the ROM's Phone
transport and Calls log make, take and log calls over it, unchanged but
for one ROM bug fixed.

- **The provider** (`src/comms/host/HostCell.ns`, compiled in and started
  from PreMain by `HostCellProviderStart`, `comms/host/HostCellProvider.h`,
  when the host has a module): `RegCommConfig('|Cellular:host|, {name:
  "Cellular", type: "TAPI", serviceId: "cell", config: {...}})` with every
  `supports...` the transport asks about.  DEVIATION: it is made the
  default once, when the user's `'currentDialSpeaker` names a provider with
  no service (a fresh store says `'speaker` - the ROM registers that one
  itself at boot: "Speaker", the tones through the loudspeaker), calls are
  then received (`'phoneAutoReceive`), the transport's `'enableAutoUI` is
  set so that its preferences offer receiving, and auto-receive is started
  there (the boot's own `StartAutoCallReceive` runs before PreMain).
- **The comm tool** (`src/comms/host/HostCellTool.h`, service `'cell`,
  `THostCellService` in `HostServices.h`): answers the `'tapiEndpoint`'s
  options as the ROM's modem tool does its TAPI ones - `taps` (connected
  at once), `outg` and `sdgt` (the number in the `rout` address beside
  them, kept to what a GSM dial string holds: the transport's number comes
  as " 5551234"), `answ`, `disc`, `tasp` (`AT+CSDVC` 3 or 1), `rout` read
  for the caller's number; `hold`, `unho`, `forw`, `focr`, `tran` not
  supported.  The engine's call events become T_TAPIEVT events: a ring 1,
  connected 8, ended 6 (on hook), busy 11 then 6.  The engine keeps a queue
  of events for each reader (the natives' and the tool's), since the tool
  listens all the time.
- **The engine** (moved to `src/hal/host/HostCellular.*`, library
  `hal_host_cellular`, because `comms` now uses it): keypad tones
  (`AT+VTS`, one a digit) and the loudspeaker or the handset (`AT+CSDVC`);
  `HostCellularWillRun`, which a board calls before its task has started
  the module, so that the Newton's side, booting meanwhile, counts it as
  there.
- **ROM BUG (fixed)**: `ItemCompleted` (`basicCallTransport.ItemCompleted`
  in `romsrc/objects/apps/basicCallTransport.ns`) hangs the endpoint up only for a call's status, read
  after `SetStatusDialog` has made it `'HangingUp` - so the keypad's Hang
  Up never told the endpoint.  Unseen with "Speaker", which has none.
  Fixed behind `RomBugFixed()`; `docs/curiosities.md`, "A telephone
  framework with nobody to call".
- **A reconstruction slip**: `TSerialEndpoint::HandleEventReply` (ROM
  0x001dc064) passed a tool's event to the script endpoint without its
  code and data (only its time and service); the ROM copies all of them.
  No `eventHandler` had seen a tool event's code before.

ctest `host.NewtonPhoneCall` (`src/host/demo/phonecall.ns`, fakecell.py):
the provider the default and listening; a call made as the Call slip makes
it (`NewItem`, `ItemSubmit`: the Calls app's `StartSend`), a keypad tone and
the speaker switched in it, hung up from the Newton and logged; one hung up
by the far end; one refused busy; one rung in with caller ID, taken (Take
Call) and hung up by the caller.

On the board the Phone transport is "Listening" on the Cellular provider
from the start.  A real call each way is the owner's to make.

**The number is dialled as the card has it.** The Phone transport dials
`GetRoutingInfo`'s `phoneNumber`, which the dialling rules of the current
worksite shape - a landline's model: the worksite's area code and country
(`currentAreaCode`, `currentCountry`) against each card's.  The owner's
first outgoing call (2026-10-12) reached a "number disconnected" message:
the worksite ("Work", Melbourne, Australia) had the area code 04 - an
Australian mobile prefix, not an area code - so a card's `0412 345 678`
was taken for a local call and dialled `12 345 678`, and `+61 ...` lost
its `+`.  Melbourne's 03 is no better: a card with no country is a US
one, and every call became an international call to the US (`0011 1
0412 345 678`).  A mobile network takes a number as written, national or
`+`-international, as every mobile phone sends it - so the provider says
`dialsAsWritten`, and the transport's `SendRequest` (DEVIATION in
`basicCallTransport.SendRequest` in `romsrc/objects/apps/basicCallTransport.ns`) then dials the card's
number as it is (`GetRoutingInfo`'s `rawNumber`); other providers dial
as the ROM does.  The engine prints each dial on the console
(`[cell] dialling: ATD...`).

## Step 3: text messages through the In/Out Box (2026-10-12)

A transport, "Text Message" (`'|SMS:host|`, `src/comms/host/HostText.ns`,
started with the telephone provider by `HostCellProviderStart`), made as
the Newton Programmer's Guide 2.0 says a transport is made (chapter 22,
"Transport Interface"; `documentation/NewtonProgrammerGuide20.pdf`):

- **The protos**, identified by their slots: `protoTransport` @389,
  `protoFullRouteSlip` @655 (the sender pop-up, the format picker, the Send
  button, `PrepareToSend`; built on the base routing slip @486, the Phone
  transport's), `protoAddressPicker` @259 (`selected`, `_picker`, the
  remembered names).  The slip's own views are its `stepChildren`: as
  `viewChildren` they replaced the proto's, and the slip had no Send
  button.  It sets no bounds of its own, as Beam's does not.
- **Sending**: the transport's `dataTypes` is `['text]`, so anything with a
  text format (a Note, a Names card, ...) has a "Text" action; the To line
  is the Names file's numbers (`'|nameRef.phone|`).  `SendRequest` (cause
  `'submit` ignored, as the Guide says) takes the Out Box's items one at a
  time; the text is the format's `TextScript` - whose second argument, which
  the Reference calls "reserved, ignore", is the data the ROM's formats
  read (the body, resolved); the number the one chosen in the To line, the
  card's mobile number when none was, kept to digits and a leading `+`;
  sent to each recipient, and the item completed `'sent` - or, when the
  network will not take it, with the transport's error ("The message could
  not be sent.", its `TranslateError`), which protoTransport's
  `ItemCompleted` shows and then reopens the item's routing slip (any
  transport's "send now" item, Beam's too).
- **Receiving**: `HostCellTransportText`, every 3 s, each message a Note in
  the In Box (`NewFromItem`, `ItemCompleted(item, 'Received)`): its body of
  class `'paperroll` with one paragraph, titled "Text from <name>" when the
  number's last eight digits are on a card in the Names file, the number
  otherwise; the sender its `fromRef`.  Its time is the Newton's own:
  `Time()` less the message's age by the host's clock (`age`, which the
  native now gives beside the GMT `time`).  The transport's configuration
  says `dontAutoPutAway: 'never` - without it the In Box's `AutoFunction`
  puts each one away into the Notepad at once (Notes has `AutoPutaway`,
  as for a beamed note), and the In Box shows nothing.
- **The engine** gives received messages to each reader, as it does the
  call events (`HostCellNextText` a script's, `HostCellTransportText` the
  transport's).

ctest `host.NewtonTextMessage` (`src/host/demo/textmessage.ns`;
`fakecell.py --refuse` a number the network will not send to): a text from
a number on a card titled with the card's name, a Note in the In Box;
one from a stranger titled with the number; a Note sent as a text from
its routing slip's Send; one refused, the error told and the slip opened
again; the routing slip opened as the Action button opens it
(`textmessage-slip.pgm`: the sender, "Text Message", the stamp, To, the
Text button).

NOT YET: a message longer than one SMS (160 GSM-7 characters, 70 UCS2)
goes as one, which the network may refuse - concatenated messages (a user
data header, PDU mode) to come; Reply in the In Box (`GetTransportScripts`);
an alert when a text comes.

## Messages: the application (2026-10-12)

(Only on a host with a cellular module: without one, a store's Messages
icon is taken out of Extras at boot - `comms/host/HostAppIcon.h`,
2026-10-13; ctest `host.NewtonMessages.unplugged`.  A T-Deck Pro without
the 4G variant still counts as having one: the board starts the engine
before it knows whether a module answers - NOT YET.)

The In Box is not how people text, so text messages have an application of
their own, built in wherever the host has a cellular module
(`src/comms/host/HostMessages.ns`, started with the provider and the
transport).  The owner liked the mockups
(https://claude.ai/artifact/LYYrm5ksveSNVE2HSAK2Lq: a Newton-like look -
the folder tab, diamond pickers, the status bar, bubbles with the
Newton's hard shadow for your own) and asked that it work on **any screen a
host gives it**.

- **Installed as a package's form part installs an application**
  (`InstallFormPart`: `GetRoot().(appSymbol) := BuildContext(template)`),
  with an icon in the Extras drawer as the ROM's own applications have one:
  an entry in the Extras soup - whose name is `@307.name`, "Packages" -
  with `app`, `text` and a bitmap `icon` drawn at boot (`MakeBitmap`,
  `DrawIntoBitmap`).
- **Received**: the transport gives each message the application's
  `appSymbol` (and its number, text and card's name); the In Box's
  `AutoFunction` sends `AutoPutAway`, which files it in the soup
  "Messages:host" (`RegUnionSoup`) and says so - a beep when Messages is
  open; closed, **one alert slip of its own** (2026-10-14, the owner's ask,
  as Mesh's: `Notify` piled an alert on an alert): the first text's sender
  and words, then "N unread" and the latest; **Show** opens the
  conversation (or the list, from more than one); closing the slip or
  opening Messages begins the count again.  **While the screen is
  locked** (2026-10-25) what the slip would say is a notification on the
  lock screen instead, open or not; at the unlock it opens Messages at the
  conversation if it was tapped there, otherwise brings the slip up (or,
  Messages open, marks the open conversation's texts read) -
  `docs/host-lock-screen.md`.  The transport no longer keeps texts in the
  In Box (`dontAutoPutAway` cleared on a store that saved `'never`).
- **Sent**: `Send` straight to the transport, the item hidden from the Out
  Box with a completion script (`ItemCompletionScript`): the message
  `'sending`, then `'sent` or `'failed`.  The transport shows no status
  slip for a hidden item, and takes a string body as the text.
- **A conversation** is a number's last eight digits (`thread`), so
  "+61 400 ..." and "0400 ..." are one.
- **Any screen**: the application lays itself out from `GetAppParams`
  (again in `ReOrientToScreen`); the list and the conversation are drawn by
  the application, rows and bubbles measured from the system fonts, a
  bubble at most seven tenths of the width; at least 400 wide, the list and
  the conversation side by side (the list two fifths, at most 300).
- **The folder tab** is the system's, as the Extras drawer's (the owner
  asked for it): `protoClockFolderTab` @670 (on `protoNewFolderTab` @669,
  on the folder tab @211 - told apart by their slots: @670 has
  `TimeString` and `viewIdleScript`, @669 a `title`), across the top of
  the application whatever it shows - over a conversation alone on a
  narrow screen too, so the top stays the same (the owner asked for it).
  Its picker lists
  the conversations, not folders (the owner asked for it): "All Messages"
  and each conversation, newest first, the one shown checked; choosing one
  opens it, "All Messages" goes back to the list.  The tab's button
  (`showBarText`) is given the application's `ConversationPicker` and
  `ConversationPicked` as its `buttonClickScript`/`pickActionScript`, and
  the tab its own `RedoText` (the title: the conversation shown, or
  "All Messages", placed as @669 places it).  The tab still wants
  `appAll`, `labelsFilter` (always `'_all`) and `GetTargetInfo` (it finds
  the application through it: without one it threw -48811, labelsFilter).
- **The protos**, from the ROM's 1.x name table (`@269`: protoApp @157,
  protoStatus @219, protoClosebox @166, protoInputLine @185,
  protoLabelInputLine @189, protoTextButton @226, protoGlance @183, ...) and
  by their slots (`@73` protoStatusBar, with `menuLeftButtons`; `@209` the
  Action button).

Found on the way: `'in` and `to` are reserved words (a symbol `'in`, a
local `to`); `StrTokenize` answers a tokenizer, not an array;
`StrTruncate` is not `StyledStrTruncate`; bold is face 1 (`1 << 20` in a
packed font: 10 point bold is 1058816) - the Programmer's Guide's table of
packed fonts has its columns misaligned; `GetPoint(4)`/`(5)` are a tap's
final x and y; `protoAddressPicker`'s viewJustify is full width (a right
edge is an inset), and on the host its own label came out half drawn
("T.") - the header draws "To ◆ name" and taps open the hidden picker's
Names picker (`DoOpenPicker`).

**Deleting** (2026-10-14, as in Mesh): a message tapped in the
conversation is selected (drawn inverted; tapped again, it is not); the
status bar's **Delete** removes it, or - nothing selected - asks
(`AsyncConfirm`) and removes the whole conversation.  The **Reply** line
wraps and grows with what is written (up to half the conversation), as
Mesh's Say line does; Send empties it (with `Clone("")`: typing changes
the line's string in place) and does nothing with only spaces.  Tests tap
with `HostPenTap(x, y)` (`host/HostPenReplay.h`), at the bubble as drawn.

ctests `host.NewtonMessages.240x320`, `.320x480`, `.480x320`,
`.810x1080` (`src/host/demo/messages.ns`, snapshots
`messages-<w>x<h>-<n>.pgm`): its icon; texts filed into conversations
(unread, read once opened; none left in the In Box); a reply from the
Reply line, `'sent`; an answer arriving into the open conversation; a new
message to a card; the tab's picker listing the two conversations, one
chosen from it opened and its name the tab's title.

NOT YET: the Action picker on a message (Reply, Forward, Copy to Notes,
Delete); scrolling by the status bar's arrows (the application has
`viewScrollUpScript`/`DownScript`; no arrows drawn); Reply on the alert
slip (it has Show); a message longer than one SMS; removing
the Extras icon on a host with no module (a store moved to one).

## Open questions

- Whether a speaker and microphone are fitted to the module's audio pins
  (lilyphone1 never found out: it could only try `AT+CPTONE`).
- Whether `AT+CSCS="GSM"` loses the ASCII characters that differ in GSM-7
  (`@`, `$`, `_`): lilyphone1 says they survive; worth one test message.
