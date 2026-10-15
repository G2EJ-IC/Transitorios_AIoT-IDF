# SD_AIoT

microSD library for ESP-IDF v5.5 (SPI mode + FAT), for logging hydraulic
transient events. Part of the **Transitorios AIoT** project.

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
| `SD_AIoT_create(host, mosi, miso, sclk, cs, "/sdcard")` | Creates the object |
| `SD_AIoT_set_max_freq_khz(h, khz)` | SPI clock of the card (default 20000), before init |
| `SD_AIoT_init(h)` | Initializes or shares the SPI bus and mounts FAT |
| `SD_AIoT_is_mounted(h)` / `get_mount_point(h)` / `get_info(h, &total, &free)` | State |
| `SD_AIoT_write_file(h, name, data, len, append)` | Writes a buffer |
| `SD_AIoT_append_line(h, name, line)` | Adds a text line + `\n` (CSV logs) |
| `SD_AIoT_read_file(h, name, buf, max)` | Reads the start of a file |
| `SD_AIoT_exists(h, name)` / `SD_AIoT_remove(h, name)` | File helpers |
| `SD_AIoT_destroy(h)` | Unmounts the card |

## Example (CrowPanel 4.3": SD on SPI2 together with the touch)

```c
/* After Configuracion_AIoT_Init() so the touch bus already exists */
SD_AIoT_handle_t sd = SD_AIoT_create(1, 11, 13, 12, SD_CS_PIN, "/sdcard");
if (SD_AIoT_init(sd) == ESP_OK) {
    if (!SD_AIoT_exists(sd, "events.csv")) {
        SD_AIoT_append_line(sd, "events.csv", "t_ms;p_kPa;q_Lps");
    }
    SD_AIoT_append_line(sd, "events.csv", "1250;101.3;0.85");
}
```

## Notes

- Check the CS pin of the microSD in the Elecrow schematic of your board.
- The card must be formatted as FAT32; it is never formatted automatically.
- Long file names need `CONFIG_FATFS_LFN_HEAP=y` (menuconfig: Component
  config -> FAT Filesystem support -> Long filename support). Without it only
  8.3 names work (e.g. `EVENTS.CSV`).
- Standard C file functions (`fopen`, `fprintf`...) also work under the
  mount point.
- Add `SD_AIoT` to the `REQUIRES` list of the component that uses it.
