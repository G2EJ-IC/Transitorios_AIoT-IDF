# UARTn_AIoT

UART library for ESP-IDF v5.5 (thread safe). Part of the **Transitorios AIoT** project.

## Design

The logic lives in a **C++ class** inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with an **opaque handle**. The same library can be
called from C (`main_AIoT.c`, EEZ code) and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): touches the hardware
...       -> methods
destroy() -> C++ destructor (releases only what the object took)
```

## API

| Function | Description |
|---|---|
| `UARTn_create(uart, tx, rx, baud)` | Creates the object (no hardware touched) |
| `UARTn_init(h)` | Installs the driver (8N1, RX buffer 2048 B) |
| `UARTn_set_timeout(h, ms)` | Read timeout (default 1000 ms, 0 = non-blocking) |
| `UARTn_set_baud_rate(h, baud)` | Changes the baud rate, before or after init |
| `UARTn_write(h, str)` | Writes a null-terminated string |
| `UARTn_write_bytes(h, data, len)` | Writes raw bytes (binary safe) |
| `UARTn_read_until(h, term, buf, size)` | Reads one line; returns 0 while the line is incomplete (partial data is kept, nothing is lost) |
| `UARTn_read_bytes(h, data, len)` | Reads raw bytes with timeout |
| `UARTn_available(h)` | Bytes waiting in the RX buffer |
| `UARTn_flush(h)` | Discards pending RX data and the partial line |
| `UARTn_destroy(h)` | Uninstalls the driver and frees the object |

## Example

```c
#include "UARTn_AIoT.h"

UARTn_handle_t gsm = UARTn_create(1, 17, 18, 115200);   /* UART1, TX 17, RX 18 */
UARTn_set_timeout(gsm, 500);
if (UARTn_init(gsm) == ESP_OK) {
    char line[128];
    UARTn_flush(gsm);
    UARTn_write(gsm, "AT\r\n");
    int n = UARTn_read_until(gsm, '\n', line, sizeof(line));
    if (n > 0) { /* line holds the answer */ }
}
```

## Notes

- UART0 is the USB/serial console of the board: use UART1 or UART2.
- Polling from a loop is supported: with `UARTn_set_timeout(h, 0)` each call
  returns immediately and the line is delivered once the terminator arrives.
- Choose free GPIOs; the RGB display and the touch use most pins of the CrowPanel
  (see `System_Defines_AIoT.h`).
- Add `UARTn_AIoT` to the `REQUIRES` list of the component that uses it.
