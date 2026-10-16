# SPI_AIoT

SPI master library for ESP-IDF v5.5. Part of the **Transitorios AIoT** project.

## Design

The logic lives in a **C++ class** (`SPI_AIoT_Device`) inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with an **opaque handle**. The same library can be
called from C (`main_AIoT.c`, EEZ code) and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): touches the hardware
...       -> methods
destroy() -> C++ destructor (releases only what the object took)
```

A **bus** (host + MOSI + MISO + SCLK) is initialized once; each chip on it is a
**device** with its own CS pin, clock and SPI mode.

## API

| Function | Description |
|---|---|
| `SPI_AIoT_bus_init(host, mosi, miso, sclk, max)` | Initializes the bus with DMA (shares it if already initialized) |
| `SPI_AIoT_bus_free(host)` | Frees the bus (destroy its devices first) |
| `SPI_AIoT_create(host, cs, hz, mode)` | Creates a device object |
| `SPI_AIoT_init(dev)` | Attaches the device to the bus |
| `SPI_AIoT_transfer(dev, tx, rx, len)` | Full-duplex transfer |
| `SPI_AIoT_write(dev, tx, len)` | Send only |
| `SPI_AIoT_read(dev, rx, len, fill)` | Receive while sending `fill` |
| `SPI_AIoT_write_then_read(dev, tx, n, rx, m)` | Command + answer in one CS frame |
| `SPI_AIoT_lock(dev, ms) / SPI_AIoT_unlock(dev)` | Exclusive bus access for several transfers |
| `SPI_AIoT_destroy(dev)` | Detaches and frees the device |

Hosts: `SPI_AIOT_HOST_SPI2` and `SPI_AIOT_HOST_SPI3` (SPI1 is reserved for flash/PSRAM).

## Example (external ADC, CS = GPIO 10, 1 MHz, mode 0)

```c
#include "SPI_AIoT.h"

SPI_AIoT_bus_init(SPI_AIOT_HOST_SPI2, 11, 13, 12, 0);
SPI_AIoT_handle_t adc = SPI_AIoT_create(SPI_AIOT_HOST_SPI2, 10, 1000000, 0);
SPI_AIoT_init(adc);

uint8_t cmd[3] = { 0x01, 0x80, 0x00 };
uint8_t ans[3];
SPI_AIoT_transfer(adc, cmd, ans, sizeof(cmd));
```

## CrowPanel 4.3" notes

- The XPT2046 touch already uses **SPI2** (MOSI 11, MISO 13, SCLK 12, CS 0).
  To add chips on that bus, call `SPI_AIoT_bus_init()` **after**
  `Configuracion_AIoT_Init()`; it detects the existing bus and shares it.
  Give every new device its own free CS pin.
- While a device holds `SPI_AIoT_lock()`, the touch cannot be read: keep the
  locked section short.
- Add `SPI_AIoT` to the `REQUIRES` list of the component that uses it.
