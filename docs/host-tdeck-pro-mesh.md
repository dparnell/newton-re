# The T-Deck Pro on a LoRa mesh: MeshCore

*Begun 2026-10-13, branch `tdeck-meshcore`, at the owner's request: the
board's SX1262 LoRa radio as a [MeshCore](https://github.com/meshcore-dev/MeshCore)
chat node, with a Newton application that works as Messages does, and a
Host preferences setting to power the radio down as the 4G module can be.
The owner's earlier firmware for the same board, lilyphone1, was a MeshCore
node (`src/apps/mesh_net.cpp`, its companion protocol `mesh_companion.cpp`);
it is where the radio's set-up, the presets and the node's rules come
from.*

DEVIATION (host addition): the ROM has no mesh.

## The pieces

As the 4G module's (`docs/host-tdeck-pro-phone.md`), in five layers:

| Layer | File | What |
|---|---|---|
| The protocol | `src/thirdparty/meshcore/` | MeshCore 1.10.0, rweather's Crypto, orlp's ed25519, base64 and RadioLib's SX126x driver, vendored from lilyphone1 with their licences, trimmed (its `README.md` says what was left out and why); `compat/` stands in for the little of Arduino they include |
| The engine | `src/hal/host/HostMesh.h`, `HostMesh.cpp` (library `hal_host_mesh`) | a MeshCore `BaseChatMesh` node on its own thread, behind a plain interface: status, contacts, messages received, sends, adverts, the name, the radio presets, the power switch; no Newton headers |
| A desktop's radio | `src/hal/host/HostMeshAir.cpp`, `newton --mesh-air lan[:port]` | packets as UDP multicast datagrams (239.255.78.121, port 3685 by default): desktop newtons and `meshpeer` on one air mesh with each other |
| The natives | `src/host/HostMeshNatives.cpp` | `HostMesh*` (the list is `HostMesh.h`'s header) |
| The application | `src/comms/host/HostMesh.ns`, started by `comms/host/HostMeshApp.cpp` | Mesh, in the Extras drawer |
| The board | `src/host/esp32/main/tdeck_lora.cpp` | the SX1262 through RadioLib on a HAL of the board's own, the engine's task, NVS, the power switch |

`meshpeer` (`src/hal/host/MeshPeer.cpp`) is a node on the pretend air for
the application and the tests to talk to; `tools/mesh/meshrun.py` runs
nodes together on one air (`tools/mesh/README.md`).

## The node

Ported from lilyphone1's `mesh_net.cpp`, kept: the node does not forward
(a phone in a pocket is a poor repeater); MeshCore adds every node it hears
announce itself as a contact; a flood's acknowledgement is waited for
12 s + 12 airtimes, a direct one's 6 s + 2 airtimes a hop; the Public
channel (the key every MeshCore node has, `izOH6cXN6mrJ5e26oRXNcg==`); an
advert ten minutes apart, and one after starting, retuning or the radio
coming back on.

**The radio's settings are values** - frequency, bandwidth, spreading
factor, coding rate, power, and the mesh's path hash size (bytes of each
hop's hash in a flood's path, 1-3: MeshCore's network setting, used for
every flood this node sends) - kept by the engine (`KeptSettings` version
2) and changeable to anything the radio takes (`HostMeshSetRadio`:
150-960 MHz, a LoRa bandwidth, SF 5-12, CR 4/5-4/8, -9 to 22 dBm).  **The
presets only fill them in**: MeshCore's community-suggested settings, the
list its own apps offer, from https://api.meshcore.nz/api/v1/config
(`suggested_radio_settings`), kept in `tools/mesh/meshcore-presets.json`
and made into `src/hal/host/HostMeshPresets.inc` by
`tools/mesh/meshpresets.py` (`--fetch` to refresh; 26 presets on
2026-10-13).  The default is **Australia (Narrow)**: 916.575 MHz, 62.5
kHz, SF 7, CR 4/7.  The status names the preset the settings match, or
says "Custom".  Version 1 of the kept settings named one of lilyphone1's
four presets by index: its "Victoria AU" becomes the community's Australia
(Narrow) (CR 4/7 where lilyphone1 had 4/8 - the coding rate travels in
each packet's header, so either hears the other), the others keep their
values exactly.

The radio is set up as lilyphone1's build flags have it - the private sync
word (0x12), a 140 mA current limit, the DC-DC regulator, 22 dBm, a 32-symbol preamble at SF 7 (MeshCore's wrapper's
`setParams`).  MeshCore's own tuning is left at its defaults, which are
what lilyphone1 set (airtime budget 1.0, receive delay base 10, no extra
acknowledgements).  Checked against lilyphone1's source, 2026-10-13, at
the owner's asking - except for three things that are LilyGO's
(`examples/test_lora_recv` in its T-Deck-Pro repository; Meshtastic's
`t-deck-pro` variant has the same): **DIO2 drives the module's antenna
switch** (`SX126X_DIO2_AS_RF_SWITCH`), the TCXO is at **2.4 V** (not 1.8),
and the receive gain is boosted (`SX126X_RX_BOOSTED_GAIN`, as MeshCore's
own T-Deck variant has it).

**Without the RF switch the board was all but deaf** (2026-10-14: the
owner's other MeshCore receiver had a morning of channel messages, the
board none; `HostMeshStatus()` said 50 packets sent and 1 heard
overnight, its contacts last heard the day before): the switch was never
turned to receive, so the radio sent normally and heard only what was
very close.  lilyphone1 has the same omission.

Changed from lilyphone1 (each a thing its source shows):

- **The clock.** lilyphone1's `VolatileRTCClock` was never ticked, so the
  mesh's time stood still between its 30-second re-syncs; the engine's
  clock is the host's whenever the host knows the time (after 2023), and
  counts on from the last time it was told otherwise.
- **Several sends at once**, each with its own acknowledgement and
  timeout (lilyphone1 had one in flight), tried up to three times, the
  last by flood (the route may have gone).  `onSendTimeout`, which
  `BaseChatMesh` calls for the last message only, is not used.
- **Contacts kept** across a restart (lilyphone1 kept them in RAM): the
  newest 32, without their routes (learnt again: the first message
  floods), written only when the list changes - who is in it, by what
  name - not each time a node announces itself again.  A board's NVS
  partition is 24 KB.
- **The radio put to sleep** before its power is cut, its interrupt let go
  (lilyphone1 cut the power to a radio still listening).
- **Only the engine's thread touches MeshCore** (lilyphone1's UI task
  called `advert_send` and wrote SPIFFS itself).
- Not kept: the UI's tables, GPS position in adverts, the BLE companion
  (no internal RAM to spare: ~31 KB free with the OS up).

A desktop's node keeps its identity, settings and contacts beside the
store (`<store>.mesh-identity`, `-settings`, `-contacts`; `--erase`
forgets them); the board's in NVS, namespace `mesh`.

## The application

`comms/host/HostMesh.ns`: Messages' design (`docs/host-tdeck-pro-phone.md`,
"Messages") over the mesh's natives rather than a transport -

- conversations: one per node, and the **Public channel**, always first;
  the folder tab's picker lists them; the list's foot says who this node
  is (name, id, preset) or that the radio is off;
- **New** lists the saved contacts (below), then the people's nodes heard
  and not saved (chat nodes and room servers: repeaters take no messages),
  then **Add by Key…**; **Announce** sends this node's advert; **Say** and **Send** below a conversation: the Say line
  wraps and grows with what is written (up to half the conversation's
  height; the ROM's `protoLabelInputLine` is one line only, so its entry
  loses `vjOneLineOnly`, and its `textChanged` lays the application out
  again when the line count changes), Send empties it and does nothing
  when it holds nothing but spaces (`StrFilled` is only "not empty", as
  the ROM's is);
- **an entry line is emptied with `Clone("")`, never a literal `""`**:
  typing changes the line's string in place, so a literal set once
  becomes the next message typed, and the next `SetValue` with it changes
  nothing (the first send emptied the line, the later ones did not;
  Messages and the Wi-Fi password line had the same);
- a message sent is *Sending*, then *Sent* (a channel's: on the air),
  *Delivered* (acknowledged) or *Not sent*; on a channel each bubble says
  who said it;
- the frame `HostMesh:host` polls `HostMeshNextMessage` every 2 s whether
  the application is open or not, and keeps the messages in the soup
  `Mesh:host` (`{class: '|meshMessage:host|, thread, name, sender, text,
  time, dir, state, sendId}`; a thread is a node's key or `"public"`);
  open, a message is told with a beep;
- the node is named after the owner (Setup's name) the first time.
- **deleting**: a message tapped in the conversation is selected (drawn
  inverted); the status bar's **Delete** removes it, or - nothing selected
  - asks (`AsyncConfirm`) and removes the whole conversation (the Public
  channel stays listed, empty);
- **closed, a message is told in one alert slip of the application's
  own** (the owner's ask, 2026-10-14: `Notify` piled an alert on an alert):
  the first message's sender and text, then "N unread messages" and the
  latest; **Show** opens the application at the conversation (or its list,
  when they came from more than one); closing the slip, or opening the
  application, begins the count again;
- **the screen locked** (2026-10-25), what the slip would say is a
  notification on the lock screen instead, whether Mesh is open or not; at
  the unlock it opens Mesh at the conversation if it was tapped there,
  otherwise brings the slip up (or, Mesh open, marks the open
  conversation's messages read) - `docs/host-lock-screen.md`;
- a message that cannot be filed is told on the console and the poll goes
  on (a throw out of `Poll` used to end the polling until a restart).

**Channels** (the owner's ask, 2026-10-14, when the board missed a
message the owner's other device showed: it was on a channel the board was
not in - `channelUnknown`, hash 154): New's last item, **Join Channel…**,
takes a name and, for a private channel, its secret.

- A **hashtag channel** (`#name`) needs no secret: it is the first 16
  bytes of SHA-256 of the name, `#` included, as every MeshCore
  application makes it (meshcore_py's `set_channel`) - anyone who knows the
  name is in it.  The name is made small (`#Melbourne` would be another
  channel).
- A **private channel's** secret is typed as 32 or 64 hex digits, or base64
  (24 or 44 characters).
- Up to 8 channels, Public always first (`MAX_GROUP_CHANNELS` 8 now, was 4:
  it sizes `BaseChatMesh`, so the desktop's and the board's builds both
  have it); those joined are kept across restarts (the blob `channels`).
- Each is a conversation listed under Public, in the order joined (thread
  `"ch:" & name`; Public's stays `"public"`); its header offers **Show
  Key** (the secret, to type into another device) and **Leave Channel**
  (once confirmed; its messages are kept until deleted).
- Natives `HostMeshChannels`, `HostMeshJoinChannel`, `HostMeshLeaveChannel`,
  `HostMeshSendChannelText`; meshpeer's `--join` (and `--echo` answers on
  that channel too).

**Location** (the owner's ask, 2026-10-14), where the host has a GPS
(`HostGPSStatus`: on the board its own u-blox M10, `tdeck_gps.cpp`; a
desktop's `--gps`):

- the Prefs' **Location in adverts** (shown only with a GPS; off by
  default, kept in the application's prefs as `shareLocation`) puts this
  node's position in its adverts (`HostMeshSetLocation`; MeshCore's
  `createSelfAdvert(name, lat, lon)`), as MeshCore's own firmware can, so
  that it shows on the mesh's maps; the poll keeps it current once a
  minute, and switching it on announces at once; `HostMeshStatus().location`
  is the position the adverts carry (nil: none);
- every conversation's header offers **Send Location**: "My location: LAT,
  LON" and a map link, sent as a message (the GPS must have a fix).

**Saved contacts are Names cards** (the owner's choice, 2026-10-14): a
node's key is kept in one of the Names file's ten custom fields
(`custom1`..`custom10`, values of class `'|string.custom|`), the one
labelled **"Mesh Key"** - the labels are the Names soup's info
`customFields` (`cardfile:GetCustomInfo`/`SetCustomInfo`), and the first
free one is labelled so the first time a node is saved.  A card shows
"Mesh Key: ..." in Names and the key can be edited there.

- **Save to Names**: tapping a conversation's header (a node not saved)
  offers it; a card is made (`cardfile:AddCard('person, ...)`) named as the
  node calls itself.
- **Add by Key…** (New's last item): a slip taking a name and a key - 64
  hex digits, spaces and colons let be, or a `meshcore://contact/add?name=
  ...&public_key=...&type=1` link as MeshCore's apps share a contact (its
  name used when none is typed) - for a node never heard; the card is
  made and its conversation opened.
- The cards' keys are given to the engine as **MeshCore favourites**
  (`HostMeshSaveContact`) when the application starts or opens; a
  favourite whose card has gone is no longer one.  A favourite is kept
  first across restarts (up to 64 contacts are kept now, favourites first)
  and is never the one to make room, and a node added by key alone is
  known by its key until it announces itself.
- A conversation is named by its node's card.
- **A full contact table makes room** (`shouldOverwriteWhenFull`, found
  2026-10-14): MeshCore's default is to add nobody once its 64 contacts
  are taken - a busy mesh's repeaters fill it - and a direct message from
  a node not in the table cannot be decrypted, so it was lost unseen.  The
  oldest node not saved now goes.  A message sent to a key the table does
  not hold adds it (the first then floods).

**Why the board misses messages another node shows** (2026-10-14, a day's
work with the owner's TTGO LoRa module - external antenna, the same radio
settings - on the same desk): **the board's internal antenna.**  The
per-packet trace (`HostMeshTraceRx(true)`) showed every packet the board
decoded arriving through one repeater (VIC-Macleod-EDG-01, hash 64/6473)
at -124 to -127 dBm, SNR -7 to -9 - at the edge of what SF7 decodes -
while the TTGO heard more repeaters.  Ruled out on the way, each by
measurement: the radio left out of receive or an interrupt missed (both
watched: never); packets dropped by MeshCore's parser or a full packet
pool (counted: none); the frequency (the board's crystal is within ~200
Hz of the repeaters'; the *TTGO* is 8 kHz off; the TCXO voltage, 1.8 or
2.4 V, changes nothing); the 4G modem (a message was missed with it off);
our MeshCore against upstream (identical).  Messages on other channels
(hashes 153, 154, 40 seen) are rightly unread.  **lilyphone1, flashed on
the same board beside the TTGO, missed the same messages** - the board,
not the firmware.  (Its flash was backed up whole first and restored:
`esptool.py read_flash 0 0x1000000` / `write_flash 0`, 33 minutes to read
over the board's USB.)  `tools/mesh/meshdecode.py` takes a raw packet
apart (`HostMeshLastUnreadable()`) and decrypts a channel message.

**Found: a screw missing from the case**, so the board was not held down
on the antenna's spring contacts.  With the screw in, the same repeater's
advert came at -102 dBm, SNR +8.8 (from -125 dBm, SNR -9 - about 23 dB),
and the board heard the mesh through several repeaters, several packets a
minute.  A board that hears only very strong or very close signals: check
the antenna's contacts first.

**A message heard by another node but not by this one** is most likely a
collision: the radio is half-duplex, and a channel message is neither
acknowledged nor sent again.  A long one is on the air for most of a
second at SF7/62.5 kHz/CR 4/8, so a message sent at the same moment can
deafen this node to it (MeshCore listens before it talks, which helps
only when the other's preamble has already begun).  Found 2026-10-13:
the board's counters (`HostMeshStatus()`'s `received`, `sent`) matched
the soup exactly - the message never reached the engine.

**What the radio hears** (added 2026-10-14, to tell a deaf radio from
packets dropped later): `HostMeshStatus()` also says `heard` (packets the
dispatcher parsed, by MeshCore payload type - `advert`, `text` (others'
direct messages, unreadable here), `channelText`, `ack`, ...; repeats
included), `channelKnown` / `channelUnknown` (channel packets a channel of
this node's could or could not decrypt, `lastUnknownHash` the last such
channel hash), `radioErrors` (the radio's CRC errors; nil where not
counted), `radioRecoveries` (the board's DIO1 found raised for more than
250 ms - a missed interrupt, handed on late; nil where not watched),
`radioIdleRestarts` / `radioIdleMs` (the board reads the chip's own mode
every 100 ms; out of receive for more than a second while MeshCore's
wrapper thinks it is receiving, it is put back and counted - nil where
not watched), `errFlags` (MeshCore's dispatcher's: 1 packet pool full, 2
channel busy too long, 4 not receiving for 8 s), `noiseFloor` (MeshCore's
sampling, dBm), `lastRssi`, `lastSnr`; `rawPackets` (every packet the radio
read with a good CRC, before MeshCore parses it), `rawDropped` and
`lastDroppedHeader` (those its parser drops unseen: a payload version past
1, path mode 3, too long), `poolFree` (MeshCore's packet pool: none free and
a packet read is dropped unseen), `channelMismatched` and `HostMeshLastMismatched()` (channel packets
whose hash is a channel of this node's but whose MAC is wrong - the
puzzling kind), `rereads`, `rereadDiffers` and `lastRereadOffset` (the
board reads each packet out of the radio a second time and compares),
`lastFreqError` (Hz, the SX1262's
estimate for the last packet: at 62.5 kHz an offset costs sensitivity).
Our MeshCore's packet format, dispatcher and radio wrappers were checked
against upstream's main on 2026-10-14: identical.  **RadioLib's
`SX126x::getStatus()` always says 0** (it reads no byte after the
command); the board reads the status byte itself
(`TDeckRadioHal::ReadStatus`) - the first try at the watchdog used
`getStatus`, took every reading for "out of receive" and restarted the
receiver each second.  On the
board, 2026-10-14: noise floor -118 dBm, packets decoded down to -127 dBm
/ SNR -9, no interrupt missed in half an hour.

Laid out for any screen as Messages is (ctests at 240x320, 320x480,
480x320, 810x1080).

**Only where there is a radio** (the owner's ask, 2026-10-13): the
application is built only when the host has a mesh node - the T-Deck Pro,
or a desktop started with `--mesh-air` - and a newton started without one
on a store that had it takes its Extras icon out again
(`comms/host/HostAppIcon.h`); Messages does the same without a cellular
module (`--cell-port`).  A reMarkable has neither.  ctests
`host.NewtonMesh.unplugged`, `host.NewtonMessages.unplugged`.

## Settings

In the Mesh application, as other Newton applications have them: the
**i** button in its status bar (`protoInfoButton`, @478), whose only item
is **Prefs** (the application's `DoInfoPrefs`), opens the Mesh
Preferences slip:

- **Keep** - how long messages are kept: a day, a week (the default), a
  month, a year, or always; never more than 1000 in all, the oldest going
  first, and never one still sending.  Pruned at the start, at most hourly
  after, and at once when Keep is changed (the owner's ask, 2026-10-14: a
  busy mesh says a lot);
- **Name** - this node's name on the mesh (taken, and announced, when the
  slip closes);
- **Preset** - fills in the settings below it; shows which preset they
  are, or "Custom";
- **Frequency** (MHz, typed), **Bandwidth**, **Spreading**, **Coding**,
  **Power** (dBm, typed), **Path hash** - the settings themselves, taken
  when the slip closes (settings the radio cannot take are told, and left
  as they were);
- **The radio on** - where the board can switch it (GPIO 46, kept in NVS
  as `loraOff`), at once.

They were Host preferences (`loraPower`, `meshRegion`) until 2026-10-13;
the owner wanted them in the application.

## The board

`tdeck_lora.cpp`.  The pins are lilyphone1's: chip select GPIO 3, reset 4,
DIO1 5, BUSY 6, power 46; the bus the panel shares (SCK 36, MOSI 33, MISO
47), now started once in `app_main` (`HostDeviceSpiBusStart`,
`tdeck_panel.cpp`) with every chip select parked high before either adds
its device.

- **RadioLib's HAL** is the board's own (RadioLib has none for the S3; its
  ESP-IDF example drives the original ESP32's registers): GPIO, the GPIO
  interrupt service, `esp_timer`, and the SPI master - the chip select a
  GPIO RadioLib drives, each exchange taking the bus
  (`spi_device_acquire_bus`) so the panel's transfers wait, the bytes
  through 256-byte buffers in internal RAM.  `Module::init` calls the
  HAL's `init`, not `spiBegin`: the device is added there.
- **Tasks**: the engine's on core 0 with a 12 KB PSRAM stack; a "keeper"
  with an internal stack does its NVS reads and writes and the power
  switch (a task with a PSRAM stack must not touch the flash).
- **The build**: MeshCore, RadioLib and the engine are left out of
  `newton_sources.cmake` (`tools/tdeck/idf_sources.py`) and listed in
  `main/CMakeLists.txt` with their `-I` paths and definitions (MeshCore's
  contact and channel counts and RadioLib's `GODMODE`/`STATIC_ONLY` change
  class layouts, so every file of them has the same), each compiled
  through a forwarding file of its own name - PlatformIO names an object
  by its file name, and ed25519's `sha512.c` met Crypto's `SHA512.cpp`.
  The firmware grew by ~180 KB (4.85 of 6 MB).

On the owner's board, 2026-10-13: the node starts with the OS ("[mesh] up
as ..."), is named after the owner, its first advert goes out
(`packetsSent`), the radio switches off and on again from the setting, and
after the advert that follows a first packet was heard.

## Tests

- `hal.MeshPeers`: two `meshpeer`s on one air - one hears the other,
  sends, is acknowledged and gets the echo back (the first copy can come
  before the other has heard this one's advert, and is then answered on
  the retry: ~17 s).
- `host.NewtonMesh.<size>`: `src/host/demo/mesh.ns` against a `meshpeer`
  "Ada" - her channel hello filed under Public, a conversation begun from
  New, delivered and echoed; on Public, Send with only spaces doing
  nothing, the Say line growing and emptied by Send (typed text too); a
  message to the Public channel, the Host
  setting's presets; snapshots `mesh-<w>x<h>-<n>.pgm`.

## Not yet

- `host.NewtonMesh.810x1080` failed once in a full `ctest -j 8` run (the
  peer received the message and both retries, but neither its
  acknowledgements nor its echoes reached newton; the channel message
  after did); it passes alone and four at a time.  Not yet understood.

- A message exchanged with another real node on the board.
- The WiFi companion (MeshCore's companion protocol on TCP port 5000, the
  frames lilyphone1's `mesh_companion.cpp` speaks), so the MeshCore phone
  apps can drive the node.
- Scroll arrows (as Messages); a channel shared as a link or a QR code.
- Picking a saved contact through Names' own people picker (a
  `|nameRef.meshKey:host|` data def over the custom field), and adding a
  heard node's key to an existing card rather than a new one.
