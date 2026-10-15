# I2C_AIoT

I2C master library for ESP-IDF v5.5, built on the new `driver/i2c_master.h`
API. Part of the **Transitorios AIoT** project.

## Design

The logic lives in two **C++ classes** (`I2C_AIoT_Bus`, `I2C_AIoT_Device`) inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with **opaque handles**. The same library can be
called from C (`main_AIoT.c`, EEZ code) and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): touches the hardware
...       -> methods
destroy() -> C++ destructor (releases only what the object took)
```

Two objects: a **bus** (port + SDA + SCL) and one **device** per sensor
(7-bit address + speed). Several devices share one bus.

## API

| Function | Description |
|---|---|
| `I2C_AIoT_bus_create(port, sda, scl, pullup)` | Creates the bus object |
| `I2C_AIoT_bus_init(bus)` | Installs the master bus |
| `I2C_AIoT_scan(bus, found, max)` | Logs every address that answers (0x08-0x77) |
| `I2C_AIoT_probe(bus, addr)` | `ESP_OK` if a device answers at `addr` |
| `I2C_AIoT_bus_destroy(bus)` | Removes the bus (destroy its devices first) |
| `I2C_AIoT_create(bus, addr, hz)` | Creates a device object |
| `I2C_AIoT_init(dev)` | Attaches the device to the bus |
| `I2C_AIoT_set_timeout(dev, ms)` | Transaction timeout (default 100 ms) |
| `I2C_AIoT_write / read` | Raw transfers |
| `I2C_AIoT_write_read` | Write + repeated START + read |
| `I2C_AIoT_write_reg / read_reg / read_regs` | 8-bit register helpers |
| `I2C_AIoT_destroy(dev)` | Detaches and frees the device |

## Example (MPU-6050 style IMU at 0x68)

```c
#include "I2C_AIoT.h"

I2C_AIoT_bus_t bus = I2C_AIoT_bus_create(0, SDA_PIN, SCL_PIN, true);
I2C_AIoT_bus_init(bus);
I2C_AIoT_scan(bus, NULL, 0);

I2C_AIoT_handle_t imu = I2C_AIoT_create(bus, 0x68, I2C_AIOT_SPEED_FAST);
I2C_AIoT_init(imu);

uint8_t who = 0;
I2C_AIoT_read_reg(imu, 0x75, &who);          /* WHO_AM_I */
I2C_AIoT_write_reg(imu, 0x6B, 0x00);         /* wake up  */

uint8_t raw[14];
I2C_AIoT_read_regs(imu, 0x3B, raw, sizeof(raw));   /* accel + temp + gyro */
```

## Notes

- Internal pull-ups (~45 kOhm) are only enough for short wires at 100 kHz.
  For 400 kHz or cables use external 2,2-4,7 kOhm resistors to 3,3 V.
- The driver serializes transfers per bus: different tasks may use different
  devices of the same bus.
- Add `I2C_AIoT` to the `REQUIRES` list of the component that uses it.
