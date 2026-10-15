# MQTT_AIoT

MQTT client library for ESP-IDF v5.5 (esp-mqtt), to send the measurements to
a broker (PC, server or cloud). Part of the **Transitorios AIoT** project.

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
| `MQTT_AIoT_create(uri, client_id)` | e.g. `"mqtt://192.168.2.10:1883"` |
| `MQTT_AIoT_set_credentials(h, user, pass)` | Before init (optional) |
| `MQTT_AIoT_set_callback(h, cb, ctx)` | Function for received messages |
| `MQTT_AIoT_init(h)` | Starts the client (connects and reconnects by itself) |
| `MQTT_AIoT_is_connected(h)` | Connection state |
| `MQTT_AIoT_publish(h, topic, data, len, qos, retain)` | Binary payload |
| `MQTT_AIoT_publish_str(h, topic, text, qos, retain)` | Text / JSON payload |
| `MQTT_AIoT_subscribe(h, topic, qos)` / `unsubscribe` | Remembered and repeated after reconnections |
| `MQTT_AIoT_destroy(h)` | Disconnects and frees the object |

## Example

```c
static void on_message(const char *topic, const uint8_t *data, size_t len, void *ctx)
{
    /* runs in the MQTT task: keep it short */
}

MQTT_AIoT_handle_t mq = MQTT_AIoT_create("mqtt://192.168.2.10:1883", "crowpanel-01");
MQTT_AIoT_set_callback(mq, on_message, NULL);
MQTT_AIoT_subscribe(mq, "aiot/cmd", 1);
MQTT_AIoT_init(mq);

if (MQTT_AIoT_is_connected(mq)) {
    MQTT_AIoT_publish_str(mq, "aiot/pressure", "{\"p_kPa\":101.3,\"q_Lps\":0.85}", 0, false);
}
```

## Notes

- WiFi must be connected; the client retries every 5 s while it is not.
- The callback runs in the MQTT task: to update LVGL widgets wrap the calls
  with `Configuracion_AIoT_Lock()` / `Configuracion_AIoT_Unlock()`.
- A free local broker for tests: Mosquitto on the PC (port 1883).
- Add `MQTT_AIoT` to the `REQUIRES` list of the component that uses it.
