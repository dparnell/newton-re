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
    (`host/HostBatteryDriver.cpp`). It reports the host's battery where
    plain C can read it (Linux's `/sys/class/power_supply`), and fresh cells
    at 100% otherwise.
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
  3. `PowerOffRingiSho` sleeps.
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
- **The display** (`THostScreenDriver`) keeps the drawing and shows it again
  as it was afterwards:
  - asleep, it is blank;
  - with the backlight on, the ink is shown a quarter lighter, as an
    electroluminescent panel washes it out.
- **Script functions** (`host/HostPowerSwitch.cpp`): `HostPowerSwitch()`,
  `HostBacklightButton()`, `HostWakeAfter(ms)`, `HostSleepCount()`.
- **Tracing:** `NEWTON_TRACE_POWER` prints each press the manager takes and
  each wake with its event word.

## Tests

- **`power.PowerManager`** starts the manager and asks it for the battery
  count, a status and a new battery kind. It then sleeps the platform and
  translates the event word.
- **`host.NewtonPower`** (`src/host/demo/power.ns`) runs on the whole
  machine:
  - the batteries are shown through a `protoBatteryGauge`;
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
