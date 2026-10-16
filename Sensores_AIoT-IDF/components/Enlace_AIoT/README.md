# Enlace_AIoT

UART "hello" link test between the two boards of **Transitorios AIoT**, built
on `UARTn_AIoT`. The component is **identical in both projects**; each board
only chooses its role.

| Board | Role | Behaviour |
|---|---|---|
| Sensor node (ESP32-S3) | `ENLACE_AIOT_INITIATOR` | Sends its greeting every `period_ms` and prints the answer |
| CrowPanel 4.3" | `ENLACE_AIOT_RESPONDER` | Prints every line received and answers with its greeting |

## Wiring (crossed)

| Sensor node (Freenove ESP32-S3) | CrowPanel 4.3" UART1 connector |
|---|---|
| GPIO 17 (TX) | IO18 (RX) |
| GPIO 18 (RX) | IO17 (TX) |
| GND | GND |

Do not join the supply wire when both boards have their own USB cable.

## Example (sensor node)

```c
const Enlace_AIoT_config_t cfg = {
    .role      = ENLACE_AIOT_INITIATOR,
    .uart_num  = 1,
    .tx_pin    = 17,
    .rx_pin    = 18,
    .baud_rate = 115200,
    .greeting  = "Hola soy un ESP32-S3",
    .period_ms = 2000,
    .task_core = 0,
};
Enlace_AIoT_Start(&cfg);
```

Expected monitor output on the sensor node:

```
I (...) Enlace: TX -> Hola soy un ESP32-S3
I (...) Enlace: RX <- Hola soy un CrowPanel 4,3 inch
```

## Notes

- Lines end with `\n` (a trailing `\r` is removed). Text only, 8N1.
- The task runs on core 0 with low priority: it never competes with the
  display (CrowPanel) or the acquisition task (sensor node).
- Add `Enlace_AIoT` to the `REQUIRES` list of the component that calls it.
