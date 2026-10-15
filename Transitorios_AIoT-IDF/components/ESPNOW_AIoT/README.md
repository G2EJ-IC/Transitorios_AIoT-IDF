# ESPNOW_AIoT

ESP-NOW library for ESP-IDF v5.5: direct ESP32-to-ESP32 packets, no router,
latency of a few milliseconds. Part of the **Transitorios AIoT** project
(sensor nodes -> CrowPanel).

## Design

The logic lives in a **C++ class** inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with an **opaque handle**. The same library can be
called from C and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): touches the hardware
...       -> methods
destroy() -> C++ destructor (releases only what the object took)
```

## API

| Function | Description |
|---|---|
| `ESPNOW_AIoT_create(channel, rx_queue_len)` | Creates the (single) ESP-NOW object |
| `ESPNOW_AIoT_init(h)` | Starts ESP-NOW (and WiFi STA if nobody started it) |
| `ESPNOW_AIoT_add_peer(h, mac)` / `remove_peer` | Registers / removes a destination |
| `ESPNOW_AIoT_send(h, mac, data, len)` | Sends up to 250 bytes and waits for the ACK (`mac = NULL` = broadcast) |
| `ESPNOW_AIoT_receive(h, &pkt, ms)` | Takes the oldest packet from the RX queue |
| `ESPNOW_AIoT_get_own_mac(mac)` | MAC of this board (give it to the other side) |
| `ESPNOW_AIoT_get_channel(h)` | Channel in use |
| `ESPNOW_AIoT_get_dropped(h)` | Packets lost because the queue was full |
| `ESPNOW_AIoT_destroy(h)` | Stops ESP-NOW and frees the object |

## Example

```c
/* Sensor node */
static const uint8_t PANEL_MAC[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC };   /* example MAC */
ESPNOW_AIoT_handle_t link = ESPNOW_AIoT_create(6, 8);     /* channel of the router */
ESPNOW_AIoT_init(link);
ESPNOW_AIoT_add_peer(link, PANEL_MAC);
ESPNOW_AIoT_send(link, PANEL_MAC, &frame, sizeof(frame));

/* CrowPanel (any task) */
ESPNOW_AIoT_packet_t pkt;
while (ESPNOW_AIoT_receive(link, &pkt, 100) == ESP_OK) {
    /* pkt.mac, pkt.rssi, pkt.len, pkt.data */
}
```

## Notes

- **Channel:** the CrowPanel is connected to the router, so ESP-NOW uses the
  router channel. Every sensor node must be created with that same channel
  (read it with `ESPNOW_AIoT_get_channel()` on the panel).
- On a node without WiFi, call `nvs_flash_init()` before `ESPNOW_AIoT_init()`.
- Packets are copied into a FreeRTOS queue: the WiFi task is never blocked.
- Add `ESPNOW_AIoT` to the `REQUIRES` list of the component that uses it.
