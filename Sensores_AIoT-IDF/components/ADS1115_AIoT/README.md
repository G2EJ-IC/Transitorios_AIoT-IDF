# ADS1115_AIoT

Driver for the **ADS1115** 16-bit external ADC (I2C, up to 860 samples/s) on
ESP-IDF v5.5, built on top of `I2C_AIoT`. Part of the **Transitorios AIoT**
project (sensor node), where it reads a 4-20 mA pressure transducer through a
shunt resistor.

## Design

The logic lives in a **C++ class** inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with an **opaque handle**. The same library can be
called from C and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): detects the device, leaves it powered down
...       -> methods
destroy() -> C++ destructor (power down, releases the I2C device)
```

## API

| Function | Description |
|---|---|
| `ADS1115_AIoT_create(bus, addr, hz)` | Creates the object (address 0x48..0x4B) |
| `ADS1115_AIoT_set_pga(h, pga)` | Range +/-6.144 .. +/-0.256 V (default +/-4.096 V) |
| `ADS1115_AIoT_set_data_rate(h, dr)` | 8 .. 860 SPS (default 860) |
| `ADS1115_AIoT_init(h)` | Detects the device (config read-back check) |
| `ADS1115_AIoT_start_continuous(h, ain)` | Continuous conversions of one input |
| `ADS1115_AIoT_read_mv(h, &mv)` | Latest conversion in millivolts |
| `ADS1115_AIoT_read_single_mv(h, ain, &mv)` | One single-shot conversion of any input |
| `ADS1115_AIoT_destroy(h)` | Power down and release |

## Example (4-20 mA transducer, 150 Ohm shunt on AIN0)

```c
ADS1115_AIoT_handle_t adc = ADS1115_AIoT_create(bus, 0x48, 400000);
ADS1115_AIoT_set_pga(adc, ADS1115_AIOT_PGA_4096MV);
ADS1115_AIoT_set_data_rate(adc, ADS1115_AIOT_DR_860SPS);
ADS1115_AIoT_init(adc);
ADS1115_AIoT_start_continuous(adc, ADS1115_AIOT_AIN0);

float mv;
ADS1115_AIoT_read_mv(adc, &mv);
float ma = mv / 150.0f;                         /* 4..20 mA -> 0.6..3.0 V */
```

## Notes

- Address by the ADDR pin: GND 0x48, VDD 0x49, SDA 0x4A, SCL 0x4B.
- With a 3.3 V supply no input may exceed 3.6 V, whatever the PGA. A 150 Ohm
  shunt keeps 4-20 mA inside 0.6-3.0 V.
- Continuous mode gives one input at the full data rate. Single-shot mode
  reads any input, but the rate is shared among the inputs.
- I2C transactions use a 10 ms timeout so a disconnected device never blocks
  the sampling loop for long.
- Add `ADS1115_AIoT` to the `REQUIRES` list of the component that uses it.
