# MPU6050_AIoT

Driver for the **MPU-6050** 6-axis IMU (3-axis accelerometer + 3-axis
gyroscope + die temperature) on ESP-IDF v5.5, built on top of `I2C_AIoT`.
Part of the **Transitorios AIoT** project (sensor node).

## Design

The logic lives in a **C++ class** inside `src/*.cpp`; the header exposes a
**C API** (`extern "C"`) with an **opaque handle**. The same library can be
called from C and from C++.

```
create()  -> C++ constructor (stores the configuration only)
init()    -> begin(): detects, resets and configures the sensor
...       -> methods
destroy() -> C++ destructor (sleep mode, releases the I2C device)
```

## API

| Function | Description |
|---|---|
| `MPU6050_AIoT_create(bus, addr, hz)` | Creates the object (address 0x68 or 0x69) |
| `MPU6050_AIoT_set_ranges(h, accel, gyro)` | Full scales: 2/4/8/16 g, 250/500/1000/2000 dps |
| `MPU6050_AIoT_set_dlpf(h, dlpf)` | Low-pass filter 260..5 Hz (default 184 Hz) |
| `MPU6050_AIoT_init(h)` | Detects (WHO_AM_I), resets and configures |
| `MPU6050_AIoT_read(h, &d)` | One 14-byte burst: accel [m/s2], gyro [rad/s], temp [degC] |
| `MPU6050_AIoT_calibrate_gyro(h, n)` | Gyro offset at rest (n readings, 1 ms apart) |
| `MPU6050_AIoT_get_who_am_i(h)` | Identity register read in init |
| `MPU6050_AIoT_destroy(h)` | Sleep mode and release |

## Example

```c
I2C_AIoT_bus_t bus = I2C_AIoT_bus_create(0, 11, 12, true);
I2C_AIoT_bus_init(bus);

MPU6050_AIoT_handle_t imu = MPU6050_AIoT_create(bus, 0x68, 400000);
MPU6050_AIoT_set_ranges(imu, MPU6050_AIOT_ACCEL_8G, MPU6050_AIOT_GYRO_500DPS);
MPU6050_AIoT_set_dlpf(imu, MPU6050_AIOT_DLPF_184HZ);
if (MPU6050_AIoT_init(imu) == ESP_OK) {
    MPU6050_AIoT_calibrate_gyro(imu, 200);       /* keep the board still */
    MPU6050_AIoT_data_t d;
    MPU6050_AIoT_read(imu, &d);
    printf("az = %.3f m/s2, gz = %.4f rad/s\n", d.az, d.gz);
}
```

## Notes

- Units are SI: m/s2, rad/s and degC.
- The internal sample rate is fixed at 1 kHz; read at 1000 Hz or slower.
  For 500 Hz sampling use the 184 Hz filter (below the 250 Hz Nyquist limit).
- I2C transactions use a 10 ms timeout so a disconnected sensor never blocks
  the sampling loop for long.
- Clones with WHO_AM_I 0x70/0x71/0x73 (MPU-6500/9250) or unknown values are
  accepted with a warning.
- GY-521 modules already have 4.7 kOhm pull-ups and a 3.3 V regulator.
- Add `MPU6050_AIoT` to the `REQUIRES` list of the component that uses it.
