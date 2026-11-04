# Power

`src/power/` is the MessagePad's power manager and its sleep. It is reached
from three directions: the scripts' battery and backlight natives
(`system/SystemNatives.cpp`), the NewtonScript power-off sequence that ends
in `PowerOffRingiSho` (the native `FPowerOff`, `newt/NewtWorld.cpp`), and
the power switch.

## The pieces

- **The power manager** (`PowerManager.h`) is the `'pg&e` world,
  `TPowerManager`. `InitPowerManager` starts it, as `TLoader::TheMain` does;
  the host starts it from `TNewtWorld::MainConstructor` (DEVIATION, like the
  sound server and the alert manager). Its handler, `TPowerEventHandler`,
  takes two kinds of message:
  - **Battery requests** by RPC, a `TPowerManagerEvent` with a command:
    - 4: a battery's status as last read;
    - 5: a fresh reading;
    - 6: how many batteries there are;
    - 7: what cells a battery holds.

    The manager answers these through the battery driver. The natives
    `BatteryStatus`, `BatteryRawStatus`, `BatteryCount` and `SetBatteryType`
    ask them (`GetBatteryStatus`, `SetBatteryType`, `FBatteryCount`).
  - **Switch events**, sent from an interrupt by `SendPowerSwitchEvent`:
    `{'newt, 'pg&e, 'powr}` or `{..., 'bklt}`.
    - `'powr`: every system event handler is told `'ppen`, and the
      application is sent `{'newt, 'idle, 'powr}`. Its handler runs the
      root's `GotoSleep`, which is `PowerOff('user)`. A press less than a
      second after waking is not taken: that is the press that woke it. If
      the application does not answer within ten seconds, the machine is
      taken to be hung, and `AECompletionProc` powers it off and reboots it.
    - `'bklt`: the backlight is turned over and the application is told.
- **The battery driver** (`BatteryDriver.h`) is the protocol
  `PBatteryDriver`: `Init`, `WakeUp`, `ShutDown`, `Count`, `Status`,
  `RawStatus`, `StartSleepCharge`, `SetType`, `ReadADCVoltage`,
  `ConvertVoltage`.
  - `BatteryInitialize` makes `"PMainBatteryDriver"` if one is registered,
    so a machine's own driver comes first. Otherwise it makes the MP2x00's
    `PCirrusBatteryDriver` (the Cirrus chip's ADC and charger: hardware,
    NOT YET RECONSTRUCTED).
  - The host registers its own `PMainBatteryDriver`
    (`host/HostBatteryDriver.cpp`), which reports the host's own battery -
    see "The host's battery" below.
- **The sleep** is `CyclePower`, which `FPowerOff` reaches through
  `SleepUntilNextWakeup`:
  1. Every handler is told `'pwof`.
  2. The battery driver, the screen (`LCDPowerOff`) and the tablet are shut
     down, and any flash erase is allowed to finish.
  3. With the stack locked and the scheduler held, the machine checks that
     no message is waiting for the application.
  4. The machine is turned off: the generic system call 0x44
     (`PowerOffSystem`) runs the kernel glue, then the platform's
     `PlatformPowerOffSystem` (`hal/Power.h`).
  5. When it wakes, it reads the power event word from the platform. If only
     the real-time clock's alarm woke it, and that alarm does not want the
     machine awake (`TRealTimeClock::SleepingCheckFire`), it goes straight
     back to sleep.
  6. On the way up, every handler is told `'pwon` with the event word; the
     screen, battery and tablet are woken, and the switch's message is
     aborted.

  `TranslatePowerEvent` (the platform's `TVoyagerPlatform::TranslatePowerEvent`)
  turns the event word into a reason. The power switch has no bit of its
  own, so it wakes the machine `'because`.

### The NewtonScript half

This is the ROM's own and runs as it is.

- **The root's idle script** runs every 30 s. It turns the backlight off
  after the `backOff` preference, and calls `PowerOff('idle)` once the pen
  has been still for `SleepTime` seconds, unless the machine is on the
  mains.
- **`PowerOff`** defers to `PowerOffSoodan`, which does the rest in order:
  1. `PowerOffJooHooShuuShuu` asks every `RegPowerOff` function
     `'okToPowerOff`.
  2. `PowerOffYobiKaiGi` calls them with `'powerOff`. A function answering
     `'holdYourHorses` holds the sequence until it calls `PowerOffResume`.
  3. `PowerOffRingiSho` sleeps (`FPowerOff`) - or, on a host shutting down,
     ends there instead (`docs/host-lock-screen.md`, "Shutting down").
  4. `PowerOnSequence` calls the `RegPowerOn` functions with the reason.

## The host

The stand-ins for the Voyager's hardware are in `hal/host/HostPower.h`
(DEVIATION).

- **Sleep.** `PlatformPowerOffSystem` blocks the task that put the machine
  to sleep, and that task holds the machine, so everything else is idle.
  - **What wakes it:**
    - the power switch, newton's **F12**;
    - a tap on the window, or any key. A MessagePad's pen does not wake it,
      but a host has no switch within the pen's reach;
    - a real-time clock alarm falling due;
    - a test's `HostWakeAfter(ms)` running out.
  - **With no window and no wake armed**, nothing could wake it, so it does
    not sleep at all.
- **The backlight button** is **F11**.
- **The host's battery** (`host/HostBattery.h`, plain C++ so that a
  board's code can use it) is read, each time the driver is asked, from the
  first of:
  1. a reading pinned by a test or a script (`HostSetBattery`);
  2. a board's own gauge, registered with `HostBatterySetSource` - the
     T-Deck Pro's BQ27220 fuel gauge and BQ25896 charger
     (`src/host/esp32/main/tdeck_battery.cpp`);
  3. the operating system:

     | Host | Where | What it says |
     |---|---|---|
     | Windows | `GetSystemPowerStatus` | per cent, mains, charging |
     | macOS | IOKit's power sources (the internal battery) | per cent, mains, charging, mV, mA - **not yet built on a Mac** |
     | Linux, the reMarkable | `/sys/class/power_supply` | per cent (or charge/energy of full), status, mV, mA, temperature, mains |
     | ESP32 | none of its own: the board's gauge | |

     Linux's supplies are found by their `type`, not by name: the first
     `Battery` present (by name, so `BAT0` before `BAT1`) that is not of
     scope `Device` (a mouse's), and the mains is any other supply
     online that has no charge of its own (the reMarkable's docked Marker
     is a Wireless supply online, with a capacity: not the mains). Its current's sign is taken from its status, since drivers
     disagree on it.
  The driver reports what the host says: the voltage, the current drawn
  (`batteryCurrent`) or charging (`chargeCurrent`), the temperature, the
  mains, and the charge state (charging is `fastCharging`; on the mains
  and not charging, "Full" or Linux's "Not charging", `fullyCharged`). What
  the host cannot say is -1, so `BatteryStatus` leaves it out. Charging
  without a word on the mains means it is on the mains. `batteryLow` is
  10%: the ROM's `MainBatteryCheck` warns when the charge falls to it, off
  the mains, as a MessagePad's does. `batteryDead` is 0%: only then does
  `FMinimumBatteryCheck` hold the machine asleep.
  A host with no battery (a desktop computer) is reported as before: a
  machine on fresh cells, full, no mains, never low - so it still sleeps
  when idle. A laptop on the mains does not: the root's idle script
  leaves a Newton on the mains awake.
  The kind of cells is whatever the machine is told (`SetBatteryType`,
  the Prefs), whatever the host's battery is.
- **The display** (`THostScreenDriver`) keeps the drawing and shows it again
  as it was afterwards:
  - asleep, it is blank;
  - with the backlight on, the ink is shown a quarter lighter, as an
    electroluminescent panel washes it out.
- **Script functions** (`host/HostPowerSwitch.cpp`): `HostPowerSwitch()`,
  `HostBacklightButton()`, `HostWakeAfter(ms)`, `HostSleepCount()`,
  `HostSetBattery(frame)` - the host's battery pinned:
  `{percent, mains, charge: 'charging | 'full | nil, millivolts, milliamps,
  tenthsCelsius}` (charging or full, it is on the mains);
  `HostSetBattery('none)` a host with no battery; `HostSetBattery(nil)` the
  host's own again.
- **Tracing:** `NEWTON_TRACE_POWER` prints each press the manager takes and
  each wake with its event word.

## Tests

- **`power.PowerManager`** starts the manager and asks it for the battery
  count, a status and a new battery kind. It pins host readings (charging
  on the mains, low off it, held full, no battery, a board's gauge) and
  checks what the driver makes of each, and reads a `power_supply` tree it
  writes itself (a mouse's battery and an absent one passed over, a
  reMarkable-style name, the current's sign, energy instead of a
  percentage). It then sleeps the platform and translates the event word.
- **`host.NewtonPower`** (`src/host/demo/power.ns`) runs on the whole
  machine:
  - the host's own battery is printed, then one is pinned (80%, off the
    mains: a laptop on the mains would otherwise never idle to sleep) and
    shown through a `protoBatteryGauge`;
  - the backlight is turned on, then off by its button;
  - the idle timer puts the machine to sleep, and a tap wakes it;
  - the power switch puts it to sleep, and a tap wakes it;
  - the `RegPowerOff` and `RegPowerOn` calls are counted.

## Not yet

- `PCirrusBatteryDriver`: the MP2x00's ADC readings and its charging state
  machine.
- The platform driver's power side, `TVoyagerPlatform`:
  - the power switch's GPIO interrupt and its state machine
    (`SamplePowerSwitchStateMachine`);
  - `IOPowerOn`/`IOPowerOff` of the subsystems;
  - `PauseSystem`, the generic system call 0x45.
- `SCCPowerInit`: the serial chips' power, after a sleep.
- The tablet driver's `ShutDown`/`WakeUp` (`TabShutDown`, `TabWakeUp`).
