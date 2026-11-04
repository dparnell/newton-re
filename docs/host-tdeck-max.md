# The LilyGO T-Deck Max

The [T-Deck Max](https://github.com/Xinyuan-LilyGO/T-Deck-MAX) is the T-Deck
Pro's successor: the same ESP32-S3, panel, keyboard and radios, with the 4G
module and the audio on one board instead of two variants, and most of its
power switches moved behind an I/O expander. The owner's board arrived on
2026-11-04. Everything below was found on that board, on COM8, unless it says
otherwise; the Pro's own page is `docs/host-tdeck-pro.md`, and all of it
applies here except for what this page lists.

## One program for both boards

There is no Max build. `src/host/esp32` makes one image, and
`HostDeviceBoardStart` (`tdeck_board.cpp`) tells the boards apart at start:
the XL9555 answers at I2C 0x20 on a Max and on no Pro. It sets `gBoardMax`
before any driver takes a pin. Few pins moved, so the drivers keep their own
and ask `tdeck_board.h` for the lines that differ. Build, bake and flash as
`src/host/esp32/README.md` says, with the board's own port:

    ~/.platformio/penv/Scripts/pio run -d src/host/esp32 -t upload --upload-port COM8

Flashing replaces LilyGO's factory firmware. Reading the factory flash back
failed here (esptool's stub stopped 32 KB in, "Corrupt data"), so none was
kept; LilyGO publishes whole images in its repository's `firmware/`, each
written at 0:

    ~/.platformio/penv/Scripts/python ~/.platformio/packages/tool-esptoolpy/esptool.py --port COM8 write_flash 0 T-Deck-MAX_V3.3_20261009.bin

## What is different from the Pro

| | T-Deck Pro | T-Deck Max |
|---|---|---|
| Power switches, resets | GPIOs | XL9555 at 0x20 (below) |
| Touch chip | CST328, its own protocol | Hynitron, chip type `CACA220E`, CST66xx protocol |
| Below the screen | nothing | three touch keys |
| Sound | none on the 4G variant | ES8311 codec, speaker, microphone |
| Charger | BQ25896 at 0x6B | SY6970 at 0x6A (first boards BQ25896) |
| GPS UART (our TX / RX) | 43 / 44 | 16 / 2 |
| Panel reset | none | GPIO 9 |
| GPIO 41 | the 4G module's supply | the frontlight's PWM |
| Motor | none on the V1.0 | DRV2605 at 0x5A |
| Motion sensor | BHI260AP, unused | BHI260AP, on its own 1.8 V rail, unused |

The XL9555's lines (`kLines` in `tdeck_board.cpp`; high is on unless it says):
P00 the 4G supply, P01 LoRa supply, P02 GPS supply, P03 the IMU's 1.8 V, P04 the
antenna (high internal, low external), P05 the motor, P06 the speaker's
amplifier, P07 touch reset (low resets), P10 the modem's PWRKEY, P11 keyboard
reset (low resets), P12 the speaker's source (high the modem, low the codec).
Its outputs outlast a reset of the ESP32-S3, and its INT is not connected.

### LilyGO's documents against the board

The repository is a good guide but is not always right, and the schematic is
V0.1. Found on the way:

- **I2S data pins.** `docs/pinmap.md` has them backwards. The schematic names
  them from the codec's side (`I2S_DSDIN` IO40, `I2S_ASDOUT` IO17), so the
  ESP32's data out is **40** and its data in **17**. Proved by the codec's
  loop, below.
- **The touch chip.** The README says CST3530, the pin map CST328; what the
  chip reports, and the protocol it speaks, is the CST66xx family's.
- **The schematic's `6609_EN-----IO41`** is stale: that line is XL9555 P00,
  and GPIO 41 is the frontlight.
- **The charger.** The schematic draws a BQ25896 at 0x6B; this board has an
  SY6970 at 0x6A. `tdeck_battery.cpp` tries the one, then the other.

## Touch and the three keys

The report is at register `0xD0070000` and is taken with `0xD00002AB`; every
command is four bytes (`TouchReadMax`'s comment has the layout). Positions are
the panel's own pixels, with no rotation or mirroring: the four corners were
tapped with a finger and came back as (0,6), (235,4), (238,315) and (9,309).
A finger's state is the high nibble of its last byte: 1 landed, 2 held,
0 lifted. The chip goes on answering with its last report if it is asked
between reports, which does no harm: it reads as nothing changed.

The keys are a report of their own with a key block: `0x10`, `0x11`, `0x12` for
keys 0, 1, 2 down, `0x00`, `0x01`, `0x02` up, left to right as the board is
held (heart, circle, paper plane), and the first byte the signal's strength.

They are the three controls of the Newton's own button bar, sent as those
buttons send them (`romsrc/objects/shared/frames.ns`, `upButton`, `ovButton`,
`downButton`): **left scrolls up, the middle is Overview, right scrolls down**.
`DeviceServices` in `newtonmain.cpp` holds the table, `kTouchKeyCommands`. Each
press buzzes. While the screen is locked a touch key only wakes the machine,
as a key on the keyboard does.

The keyboard is the Pro's: the same TCA8418 matrix and numbering (q 9, a 19,
z 28, Alt 29, Sym 31), so the owner's layout in `tdeck_input.cpp` stands. Both
chips interrupt on the Pro's pins, 12 and 15.

## The side button, and Power off

**The side button is a hardware power-on, not an input.** With the board running
it does nothing and the ESP32-S3 cannot see it: pressed, or held for 3 seconds,
GPIO 0 never moved (`!pm`'s BOOT count stayed 0), where the Pro's side button is
GPIO 0 and is the Newton's power switch. The V0.1 schematic shows only S1 on RST
and S2 on IO0 (BOOT) and leaves the charger's QON pin unconnected, but it is
wrong there: the side button wakes the board from the charger's shutdown, which
is what QON does.

So the Newton switches the board off and the side button switches it on.
**Power off** is on the quick sheet, beside Lock screen (the finger pulled down
from the top edge), on the Max only (`HostQuickSheetSetPowerOff`).

An e-paper keeps its last picture with no power, so a board that is off looks
like one that is on. Power off therefore leaves a message: the sheet closes, a
white view fills the screen saying "This device is off" and how to start it
(`ShowOff` in `host/HostQuickSheet.ns`), and `HostDevicePanelFinish` clears the
panel, draws that with a full refresh and takes the panel's high voltages down
(POF). That is about 6 s. Only then does `HostDeviceBatteryPowerOff` hold the
touch chip in reset, as LilyGO's firmware does, and set BATFET_DIS (REG09 bit 5)
in the charger. The next start clears the panel anyway.

Tried on the owner's board on battery: the screen cleared, the message stayed
clean, and both the side button and plugging in USB started the board again.
Should the charger not take it, the panel comes back, the off screen is taken
down and the sheet's message about USB is shown.

**On USB it cannot switch off**, the charger keeping the board up, so the sheet
says to unplug the cable instead.

The Newton's sleep, as against off, has no button on the Max but the BOOT button
on the back: the quick sheet's Lock screen does the same job.

## Sound

`tdeck_audio.cpp` is the board's implementation of `host/HostAudio.h`'s six
calls, so the Newton's sound server, volume included, works as on any host. The
I2S runs at the server's 21600 samples a second with MCLK 256 times that, the
codec a slave at a fixed 0 dB: the sound driver scales the samples for the
Newton's volume. The clocks, codec and amplifier run only while there is sound,
and stop after 1.5 s without any, so an idle board pays nothing and light sleep
carries on. A Pro answers `HostAudioOpen` false and has no sound driver, as
before.

**The console's `!sound` measures the whole path with nobody listening**, and
is the first thing to run if sound seems wrong:

    newton: the room: mean -1, level 60, at 1 kHz 2, 22873 samples a second
    newton: the codec's loop: mean -2, level 5656, at 1 kHz 8000, 21571 samples a second
    newton: the loudspeaker: mean 0, level 7161, at 1 kHz 10114, 21828 samples a second

The tone is sent at 8000. Coming back at exactly that through the codec's own
loop (REG44 `ADCDAT_SEL` 4: the DAC's data as the ADC's left channel) proves the
I2S, both data pins and the sample rate; the speaker heard through the
microphone proves the rest. The owner also heard it.

The one surprise: **ESP-IDF 5.1's receive channel in full duplex gives both
slots of a frame though it is set up mono**, 43200 samples a second, so
`HearTask` keeps every second one. Without that, a recording plays back at half
speed. The rate printed by `!sound` is there to catch it.

During a call (`HostCellularSetCallHandler`, dialling or connected) the speaker
is switched to the modem and the amplifier held on. **Not tried**: the board has
no SIM in it yet.

## The frontlight and the motor

The frontlight is PWM on GPIO 41: **Frontlight** on the Prefs Screen page (Off,
Low, Medium, High), `HostSetSetting('frontlight, 0-3)` from the console, off
while the screen is locked and not kept across a restart. Its PWM stops in light
sleep, so the chip stays awake while it is on.

The motor is a DRV2605 set up as an ERM in open loop, library A. A touch key
plays effect 1, strong click; `DevicePowerSet('buzz, n)` plays any of the
chip's effects. **Effect 24, sharp tick, could not be felt at all**, though the
chip took it; 1, 14 and 47 can. It is put in standby a second after each.

## State on the board

Working, on the owner's board: the boot (56 s the first time, building the
store; 9 s after), the panel, touch, the three keys, keyboard and its light,
sound out and in, frontlight, motor, fuel gauge and charger, WiFi radio start,
the LoRa radio's start and the mesh node, the GPS answering (no fix tried), the
4G module answering (`noSIM`), a WiFi join, beaming to and from a desktop and
a Pro over it (`docs/comms/README.md`, "Beaming over the network").

Not done or not tried:

- a call or SMS (no SIM), so the speaker's switch to the modem;
- a GPS fix, a LoRa exchange with another node;
- on battery: everything so far ran on USB, where the chip does not light-sleep.
  Whether the touch chip's INT pulse is long enough to wake it is not known;
- the BHI260AP (its 1.8 V line stays off; it needs its firmware uploaded);
- the external LoRa antenna (`kLineLoraAntenna` stays on the internal one);
- the SD card, as on the Pro;
- this image on a Pro: the Pro code paths were restructured and built, but no
  Pro was connected. Run it on one before trusting it there.

Debugging: `DevicePowerSet('touchTrace, true)` prints each new touch report and
keyboard key, and looks at the chips every 20 ms whether they interrupt or not,
so that a report with no interrupt shows. `!pm` counts the interrupts and gives
the lines' levels and the keyboard chip's registers.

None of LilyGO's code is copied here. The protocols and register values are
facts read from its sources and datasheets, as for the Pro.
