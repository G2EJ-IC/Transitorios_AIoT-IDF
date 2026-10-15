# PCNT_AIoT

Hardware pulse counter library for ESP-IDF v5.5 (PCNT), intended for
pulse-output flow meters. Part of the **Transitorios AIoT** project.

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
| `PCNT_AIoT_create(gpio, glitch_ns)` | Creates the counter (glitch filter up to 12000 ns) |
| `PCNT_AIoT_init(h)` | Creates the PCNT unit and starts counting |
| `PCNT_AIoT_get_count(h, &n)` | Total pulses since init/clear |
| `PCNT_AIoT_clear(h)` | Total back to zero |
| `PCNT_AIoT_get_frequency(h, &hz)` | Pulse frequency since the previous call |
| `PCNT_AIoT_set_k_factor(h, k)` | K of the meter in pulses per liter |
| `PCNT_AIoT_get_flow(h, &lps)` | Flow in L/s since the previous call |
| `PCNT_AIoT_get_volume(h, &l)` | Accumulated volume in liters |
| `PCNT_AIoT_destroy(h)` | Releases the unit |

## Example (K = 450 pulses/L, flow read every 500 ms)

```c
PCNT_AIoT_handle_t fm = PCNT_AIoT_create(18, 1000);
PCNT_AIoT_set_k_factor(fm, 450.0f);
PCNT_AIoT_init(fm);

for (;;) {
    float q_lps, v_l;
    PCNT_AIoT_get_flow(fm, &q_lps);       /* Q [m3/s] = q_lps / 1000 */
    PCNT_AIoT_get_volume(fm, &v_l);
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

## Notes

- Pulses are counted by hardware on rising edges: no CPU load, none lost.
- The ESP32-S3 has 4 PCNT units (up to 4 meters).
- Internal pull-up enabled: suitable for open-collector (hall effect) outputs.
- If the datasheet gives `f = k * Q[L/min]`, then `K [pulses/L] = 60 * k`.
- Add `PCNT_AIoT` to the `REQUIRES` list of the component that uses it.
