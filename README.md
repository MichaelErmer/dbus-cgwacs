# dbus-cgwacs: ET340 phase-netting fix for Victron Venus OS

This fork adds **software phase netting (vector summation / saldierend)** for
**Carlo Gavazzi ET340 three-phase grid meters** used with **Victron Venus OS,
Cerbo GX, ESS and VRM**. It addresses grid import/export energy counters that
are too high compared with a utility meter that nets the phases: energy
exported on one phase and imported on another should cancel before the grid
energy totals are accumulated.

The correction is **opt-in**, selected by the energy meter's serial number.
It keeps the existing ET340 and publishes corrected counters through the
usual Victron D-Bus paths, so GX, VRM and downstream dashboards can use them.
It integrates the power samples already collected by the driver.

This is a community fork of [victronenergy/dbus-cgwacs](https://github.com/victronenergy/dbus-cgwacs),
not an official Victron firmware release. The driver reads Carlo Gavazzi wired
AC sensors and publishes their measurements on D-Bus. Supported upstream
models include EM24, ET112, ET340, EM530, EM540 and selected EM-300 models;
**the new netting correction applies only to an ET340 configured as a
three-phase grid meter without a secondary/piggyback meter**.

## What this fork fixes

Typical symptoms include **ET340 import and export at the same time** and
**Victron VRM energy totals not matching the electricity meter**, even with ESS
phase compensation enabled. The required accounting is also called
**Saldierung über alle Phasen** or a **saldierender Zähler** in German.

The ET340's hardware energy counters accumulate each phase's import and
export independently. A phase-netting utility meter instead sums the signed
phase powers first. For example, held for one hour:

| Phase or result | Power / energy |
| --- | ---: |
| L1 | +1000 W import |
| L2 | -1000 W export |
| L3 | 0 W |
| ET340 arithmetic counting | 1 kWh import **and** 1 kWh export |
| Phase-netted counting | **0 kWh** net import or export |

This difference can inflate both the VRM **from grid** and **to grid** energy
figures when PV generation or battery discharge on one phase offsets loads on
another. Instantaneous total grid power can already be correct while the
accumulated kWh counters differ. Victron documents this behavior in the
[ET340 energy-meter FAQ, Q8](https://www.victronenergy.com/media/pg/Energy_Meter_ET340/en/faq.html)
and [ESS FAQ, Q14](https://www.victronenergy.com/media/pg/Energy_Storage_System/en/faq.html).

This fork calculates:

```text
Pnet = PL1 + PL2 + PL3
import_kWh += max(Pnet, 0)  * elapsed_seconds / 3_600_000
export_kWh += max(-Pnet, 0) * elapsed_seconds / 3_600_000
```

It uses trapezoidal integration and splits intervals at a change in direction.
Synthetic per-phase energy counters allocate the net flow to phases contributing
in that direction, and their sum equals the corrected total. These synthetic
phase counters represent allocated net energy, rather than the ET340's original
individual-phase energy history.

The earlier phase-counter consistency change in this fork apportioned hardware
totals into phase counters. That alone did not solve phase netting. Simply
summing the existing cumulative phase kWh values cannot reconstruct netted
import/export totals.

## Enable phase-netted energy accounting

Set these variables in the environment of the existing `dbus-cgwacs` service:

```sh
export CGWACS_NET_METER_SERIAL="YOUR_METER_SERIAL"
export CGWACS_NET_STATE_DIR="/data/dbus-cgwacs-net-energy"
```

Use the energy meter's D-Bus **`/Serial`** value, not the USB/RS485 adapter's
serial number, VRM site ID or device instance. The serial match, ET340 model,
grid role, three-phase setting and disabled piggyback mode are all required.
Without the matching serial opt-in, the fork's previous accounting behavior
remains in use.

`CGWACS_NET_STATE_DIR` must be writable and survive reboots. The current default
is `/data/smart-grid-energy`; setting it explicitly as above is recommended for
a separate installation. Each meter has a `<serial>.json` state file.

After building, install the executable and configure the existing Venus meter
service's launcher to use it and export the variables above. **Keep exactly one
reader on the meter's serial port.** Retain `serial-starter` ownership and stop
competing generic Modbus, GPS or other probes on that adapter. A second manually
started driver can interfere with both acquisition and energy accounting.

Venus OS firmware updates can replace custom files under `/opt/victronenergy`.
Keep the executable, state and your boot-time activation mechanism under
persistent `/data`; make sure the custom launcher is restored before the meter
service starts. USB tty numbers can change after reboot, so identify adapters by
their USB serial rather than assuming a permanent `ttyUSB0` or `ttyUSB1`.

### Corrected values and diagnostics

| D-Bus path | Meaning |
| --- | --- |
| `/Ac/Energy/Forward`, `/Ac/Energy/Reverse` | Corrected accumulated grid import/export, kWh |
| `/Ac/L1/Energy/Forward` and the corresponding L2/L3/Reverse paths | Synthetic phase allocations that sum to the corrected totals |
| `/Ac/Energy/ForwardRaw`, `/Ac/Energy/ReverseRaw` | Original ET340 hardware totals, kWh |
| `/Energy/Netting/Active` | Software phase netting is initialized and active |
| `/Energy/Netting/StartedAt` | Initial activation time, Unix seconds |
| `/Energy/Netting/SkippedSeconds` | Recorded gaps excluded from integration, seconds |
| `/Energy/Netting/PersistenceOk` | Most recent state load/checkpoint succeeded |
| `/Energy/Netting/LastSave` | Latest checkpoint time, Unix seconds |

Keep the raw counters available for comparison. Their divergence from the
corrected counters is expected when opposite flows occur across phases.

### Persistence and accuracy limits

- The first baseline preserves the existing hardware lifetime totals. The fix
  changes future increments; **it does not repair historical overcounting** or
  make the starting readings equal your utility meter.
- State is checkpointed atomically with `QSaveFile` every 60 seconds and on
  graceful shutdown/disconnection. An abrupt power loss can lose the last
  uncheckpointed minute. A checkpoint failure is exposed in the diagnostics.
- Invalid readings, communication errors and sample intervals over ten seconds
  break integration. Process downtime is excluded, rather than estimated using
  an old power reading. Missing net energy cannot be reconstructed exactly from
  the arithmetic hardware counters.
- Existing malformed state is rejected instead of silently starting new lifetime
  counters. Back up state before maintenance; deleting it creates a new baseline.
- This is integration of sampled power, not certified billing metering. Its
  usefulness for comparison depends on your utility meter's counting method.

## Build and test

Clone this fork and initialize the public source dependencies used by the Qt 6
build:

```sh
git clone https://github.com/MichaelErmer/dbus-cgwacs.git
cd dbus-cgwacs
git submodule update --init software/ext/qslog software/ext/veutil
```

A matching **`velib` source tree is also required at `software/ext/velib`**.
The upstream submodule points to `victronenergy/velib`, which was not publicly
fetchable when these instructions were checked. Obtain compatible sources
through your Venus development environment or existing upstream access before
attempting a full driver build. This fork does not redistribute that dependency.
The standalone integration test below does not need it.

For a GX build, install the matching
[official Venus OS SDK](https://updates.victronenergy.com/feeds/venus/release/sdk/)
on a Linux host and source its environment. This fork was built and tested with
the Venus v3.85 Cortex A8 SDK and Qt 6.8.3:

```sh
. /path/to/sdk/environment-setup-cortexa8hf-neon-ve-linux-gnueabi
mkdir -p build
cd build
"$OECORE_NATIVE_SYSROOT/usr/bin/qmake" ../software/dbus-cgwacs.pro
make -j4
```

The pure integration test needs only a C++17 compiler and can run on a development
machine, without Qt, a meter, or a D-Bus connection. Run it from the repository root:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic test/net_energy_test.cpp -o /tmp/net-energy-test
/tmp/net-energy-test
```

It covers mixed phases, import/export, exact cancellation, zero crossings,
invalid samples, gaps, phase sums and baseline/restart continuity.

The native offline persistence test uses fake sensor values, without opening a
serial port or a D-Bus connection. Build it from the `build` directory:

```sh
"$OECORE_NATIVE_SYSROOT/usr/bin/qmake" ../software/dbus-cgwacs.pro 'CONFIG+=net_energy_test'
make -j4
```

Run the resulting `grid-net-energy-test` on a machine matching the build target,
using a **fresh temporary directory**:

```sh
test_dir=$(mktemp -d /tmp/dbus-cgwacs-test.XXXXXX)
./grid-net-energy-test "$test_dir"
```

This checks atomic checkpoint/restore, raw-counter separation, diagnostic
notifications, phase sums and shutdown. Deployment validation also included a
controlled live restart and confirmation that downstream Node-RED received the
corrected counters.

## Upstream meter support

### EM530/540 support
Note that for the EM530 and EM540, only Sub-family `X` and `PFC` is supported.

| Family     | Sub-family | Part number        | Measurement |
|------------|------------|--------------------|-------------|
| EM530-IS   | X          | EM530DINAV53XS1X   | CT          |
|            | PFC        | EM530DINAV53XS1PFC | CT          |
| EM540-IS   | X          | EM540DINAV23XS1X   | Internal    |
|            | PFC        | EM540DINAV23XS1PFC | Internal    |

### EM300 support
For the EM-300 series of meters, only the series-27 meters are supported,
and only `X` and `PFB` models are supported.  The series-27 is a meter that
simulates (in part) the registers of an EM24.  The following part numbers are
supported.  Note the 27 series appended at the end of the part number.

| Family     | Sub-family | Part number          | Measurement |
|------------|------------|----------------------|-------------|
| EM330      | X          | EM330DINAV53HS1X27   | CT          |
|            | PFB        | EM330DINAV53HS1PFB27 | CT          |
| EM340      | X          | EM340DINAV23XS1X27   | Internal    |
|            | PFB        | EM340DINAV23XS1PFB27 | Internal    |

Energy counters
===============
* With the exception of the ET340, all 3-phase meters lack per-phase
  reverse energy counters. Only a total energy value is available. dbus-cgwacs
  estimates the per-phase reverse energy by using the proportion of negative
  reverse power of each phase.

* The total energy counters on the EM300 series (the ET340 and the
  EM-300-series-27) is simply the sum of the individual phase counters.
  It does not aggregate the total across all the phases, therefore resulting
  in a different outcome. Where energy is simultaneously imported and exported
  on different phases, the EM24 and EM530/540 will correctly show the total
  energy across all phases, while the other meters will show higher import
  and export numbers that may not correspond with the utility meter.

Architecture
============

The application consists of several layers:
* Data acquisition layer:
    - The _ModbusRtu_ class provides a simple implementation of the modbus
      RTU protocol, which supports the functions ReadHoldingRegisters (3),
      ReadInputRegisters (4) and WriteSingleRegister (6) only.
    - _AcSensorUpdater_ connects to an ac sensor, retrieves its identity
      (type & serial) and retrieves measured data. The data retrieved will be
      stored in an _AcSensor_ object.
* Data model
    - _AcSensor_ contains the latest measurements taken from an AC sensor.
    - _Settings_ Persistent settings objects which contains global settings.
    - _AcSensorSettings_ Persistent AC sensor settings.
* D-Bus layer
    - _AcSensorBridge_ produces the _com.victronenergy.grid.ttyUSB??_ D-Bus
      service. Information is taken from an _AcSensor_, and an
      _AcSensorSettings_ object.
    - _AcSensorSettingsBridge_ consumes a subtree from the local settings.
      Path: /Settings/CGwacs/D[serial]

Finally _AcSensorMediator_ ties everything together.

Error handling
==============

It is important for dbus-cgwacs to terminate if it is not talking to a
Carlo Gavazzi meter, otherwise serial-starter cannot work properly. Two
kinds of errors are possible.

* A timeout:
    - There was no response to a modbus request. The default timeout is 250ms
      (2 seconds for zigbee) and can be overridden with the `--timeout`
      commandline option.
    - dbus-cgwacs will terminate after 5 consecutive requests ends in a timeout.
* A modbus error:
    - These include outright errors such as unsupported registers or CRC errors.
    - In order to be forgiving to intermittent conditions causing CRC errors,
      dbus-cgwacs will terminate after 20 consecutive errors.
