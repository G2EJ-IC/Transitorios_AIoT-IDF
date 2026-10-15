# ADC_AIoT

High-rate ADC library for ESP-IDF v5.5 (continuous mode with DMA and voltage
calibration), intended for pressure transducers. Part of the **Transitorios
AIoT** project.

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
| `ADC_AIoT_create(rate_hz)` | Total conversions per second (611..83333 Hz) |
| `ADC_AIoT_add_channel(h, gpio, atten)` | Adds an ADC1 input (GPIO 1..10), before init |
| `ADC_AIoT_init(h)` | DMA driver + calibration per channel |
| `ADC_AIoT_start(h)` / `ADC_AIoT_stop(h)` | Starts / stops the conversions |
| `ADC_AIoT_read(h, samples, max, ms)` | Converted samples: index, gpio, raw, mV |
| `ADC_AIoT_get_rate_per_channel(h)` | Samples per second of each channel |
| `ADC_AIoT_get_overruns(h)` | Times data was lost because reads were late |
| `ADC_AIoT_destroy(h)` | Releases everything |

## Example (two transducers at 2 kHz each)

```c
ADC_AIoT_handle_t adc = ADC_AIoT_create(4000);
ADC_AIoT_add_channel(adc, 4, ADC_AIOT_ATTEN_12DB);   /* index 0 */
ADC_AIoT_add_channel(adc, 5, ADC_AIOT_ATTEN_12DB);   /* index 1 */
ADC_AIoT_init(adc);
ADC_AIoT_start(adc);

ADC_AIoT_sample_t s[256];
for (;;) {
    int n = ADC_AIoT_read(adc, s, 256, 100);
    for (int i = 0; i < n; i++) {
        /* s[i].index -> sensor, s[i].mv -> voltage */
    }
}
```

## Notes

- **Sensor node only:** on the CrowPanel 4.3" the ADC1 pins are used by the
  RGB display. Use a separate ESP32-S3 board for the transducers.
- ADC2 is not used: on the ESP32-S3 it conflicts with WiFi.
- Range with 12 dB attenuation is about 0..3.1 V. A 0.5..4.5 V transducer
  needs a resistor divider; never put more than 3.3 V on a pin.
- Data arrives in DMA frames of about 1 ms (16..256 results), so a task that
  reads every 1-2 ms always gets fresh samples (low latency).
- Read in a loop from a dedicated task; if the DMA pool fills up, the oldest
  data is discarded and the overrun counter increases.
- Add `ADC_AIoT` to the `REQUIRES` list of the component that uses it.
