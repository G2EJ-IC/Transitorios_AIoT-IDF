/*
 * File: components/Configuracion_AIoT/include/System_Defines_AIoT.h
 * Description: Single source of truth for the CrowPanel 4.3" (480x272, NV3047,
 *              ESP32-S3-WROOM-1-N4R2) display, touch and LVGL settings.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 *
 * Revision 2 (PSRAM bandwidth fix):
 *  Symptom: ghost / doubled / shifted image ONLY during screen transitions.
 *  Cause:   the 2 MB QUAD PSRAM is shared by two streams:
 *             - read : bounce-buffer refill for the panel  (~11.5 MB/s, fixed)
 *             - write: LVGL copying rendered areas into the frame buffer
 *           During a full-screen animation at 50 FPS (20 ms period) the write
 *           stream reached ~13 MB/s, the bounce refill arrived late, the RGB DMA
 *           underran and the picture jumped every frame (re-synced at VSYNC).
 *  Fix:     - LVGL refresh back to 33 ms (~30 FPS): ~40 % less PSRAM writes.
 *           - Draw buffer 48 -> 32 lines: shorter write bursts.
 *
 * Revision 4 (static "dancing" image fix):
 *  Symptom: doubled image, shifted vertically by exactly 16 lines, even on a
 *           static screen (revisions 2 and 3).
 *  Cause:   CONFIG_LCD_RGB_RESTART_IN_VSYNC restarts the DMA from bounce
 *           buffer 0 at EVERY VSYNC. With 16 lines there are 272/16 = 17
 *           chunks per frame (ODD), so the first chunk of every other frame is
 *           preloaded into bounce buffer 1 -> that frame is shown shifted by
 *           one bounce buffer (16 lines). Alternating frames = double image.
 *  Fix:     back to 8 lines (34 chunks, EVEN) and a compile-time check in
 *           Configuracion_AIoT.c that rejects any odd chunk count.
 *
 * Revision 5 (factory-demo configuration) - REJECTED on hardware:
 *  Bounce buffers OFF (GDMA reading PSRAM directly, like Arduino_GFX) made the
 *  image drift permanently: with WiFi + LVGL + flash images sharing the same
 *  quad SPI bus, the direct DMA could not keep up.
 *
 * Revision 6 (current):
 *  - Bounce buffers ON with 8 lines (34 chunks per frame, EVEN): proven stable.
 *  - Pixel clock 6 MHz (factory value): ~14 % less PSRAM traffic than 7 MHz,
 *    more margin for the bounce refill.
 *  - LVGL draw buffer 48 lines (internal RAM: 15 KB bounce + 45 KB draw).
 */
#ifndef SYSTEM_DEFINES_AIOT_H
#define SYSTEM_DEFINES_AIOT_H

/* -------------------------------------------------------------------------- */
/* RGB LCD panel (NV3047, 16-bit RGB565 parallel interface)                   */
/* -------------------------------------------------------------------------- */
#define AIOT_LCD_H_RES              480
#define AIOT_LCD_V_RES              272

/* Pixel clock. 6 MHz = value used by the Elecrow factory firmware.
 * (480+43+8+4) x (272+12+8+4) = 535 x 296 clocks -> 6e6 / 158360 = 37.9 Hz
 * PSRAM read stream for the panel: 480 x 272 x 2 B x 37.9 Hz = ~9.9 MB/s. */
#define AIOT_LCD_PIXEL_CLOCK_HZ     (6 * 1000 * 1000)

#define AIOT_LCD_HSYNC_BACK_PORCH   43
#define AIOT_LCD_HSYNC_FRONT_PORCH  8
#define AIOT_LCD_HSYNC_PULSE_WIDTH  4
#define AIOT_LCD_VSYNC_BACK_PORCH   12
#define AIOT_LCD_VSYNC_FRONT_PORCH  8
#define AIOT_LCD_VSYNC_PULSE_WIDTH  4

/* Number of PSRAM frame buffers inside the RGB driver.
 * Must be 1 when LVGL works in PARTIAL mode: the driver copies every flushed
 * area into the *current* frame buffer, so a second buffer is never shown and
 * only wastes 261 KB of the 2 MB PSRAM. */
#define AIOT_LCD_NUM_FBS            1

/* Bounce buffer height in lines (internal SRAM, two buffers are allocated).
 * 0 = disabled (GDMA reads the PSRAM frame buffer directly). Tested on this
 *     board with WiFi active: the image drifts. Do NOT use 0.
 * Constraints for non-zero values:
 *  1. ESP-IDF driver: V_RES must be a multiple of the line count.
 *  2. CONFIG_LCD_RGB_RESTART_IN_VSYNC: the number of chunks per frame
 *     (V_RES / lines) must be EVEN, otherwise every other frame is shifted.
 * With V_RES = 272 the valid values are: 1, 2, 4, 8 (34 chunks),
 * 34 (8 chunks), 68 (4 chunks), 136 (2 chunks).  NOT 16 or 17 (odd).
 * 8 lines = 7.5 KB per buffer (15 KB total), refill window ~0.7 ms at 6 MHz. */
#define AIOT_LCD_BOUNCE_LINES       8

/* -------------------------------------------------------------------------- */
/* RGB LCD pins (CrowPanel 4.3" Basic)                                        */
/* -------------------------------------------------------------------------- */
#define AIOT_PIN_NUM_BK_LIGHT       2
#define AIOT_PIN_NUM_HSYNC          39
#define AIOT_PIN_NUM_VSYNC          41
#define AIOT_PIN_NUM_DE             40
#define AIOT_PIN_NUM_PCLK           42
#define AIOT_PIN_NUM_DISP_EN        -1  /* Not connected on this model */

/* RGB565 data bus: B0..B4, G0..G5, R0..R4 */
#define AIOT_PIN_NUM_DATA0          8   /* B0 */
#define AIOT_PIN_NUM_DATA1          3   /* B1 */
#define AIOT_PIN_NUM_DATA2          46  /* B2 */
#define AIOT_PIN_NUM_DATA3          9   /* B3 */
#define AIOT_PIN_NUM_DATA4          1   /* B4 (blue MSB) */
#define AIOT_PIN_NUM_DATA5          5   /* G0 */
#define AIOT_PIN_NUM_DATA6          6   /* G1 */
#define AIOT_PIN_NUM_DATA7          7   /* G2 */
#define AIOT_PIN_NUM_DATA8          15  /* G3 */
#define AIOT_PIN_NUM_DATA9          16  /* G4 */
#define AIOT_PIN_NUM_DATA10         4   /* G5 (green MSB) */
#define AIOT_PIN_NUM_DATA11         45  /* R0 */
#define AIOT_PIN_NUM_DATA12         48  /* R1 */
#define AIOT_PIN_NUM_DATA13         47  /* R2 */
#define AIOT_PIN_NUM_DATA14         21  /* R3 */
#define AIOT_PIN_NUM_DATA15         14  /* R4 (red MSB) */

/* -------------------------------------------------------------------------- */
/* XPT2046 resistive touch controller (SPI2 / FSPI IOMUX pins)                */
/* -------------------------------------------------------------------------- */
#define AIOT_TOUCH_SPI_HOST         SPI2_HOST
#define AIOT_PIN_NUM_TOUCH_CS       0
#define AIOT_PIN_NUM_TOUCH_MISO     13
#define AIOT_PIN_NUM_TOUCH_MOSI     11
#define AIOT_PIN_NUM_TOUCH_CLK      12
#define AIOT_PIN_NUM_TOUCH_IRQ      36

/* XPT2046 datasheet: DCLK max 2.5 MHz. 2 MHz keeps a safety margin. */
#define AIOT_TOUCH_SPI_CLOCK_HZ     (2 * 1000 * 1000)

/* Raw ADC calibration (12-bit) measured on this panel */
#define AIOT_TOUCH_RAW_X_MIN        200
#define AIOT_TOUCH_RAW_X_MAX        3900
#define AIOT_TOUCH_RAW_Y_MIN        240
#define AIOT_TOUCH_RAW_Y_MAX        3800

/* Minimum pressure (Z) to accept a sample as a real touch.
 * Increase it if phantom touches appear, decrease it if light taps are lost. */
#define AIOT_TOUCH_Z_THRESHOLD      350

/* -------------------------------------------------------------------------- */
/* LVGL middleware                                                            */
/* -------------------------------------------------------------------------- */
/* Draw buffer height in lines (single buffer in internal SRAM).
 * 48 lines = 480 * 48 * 2 = 46 080 bytes -> a full screen needs ~6 chunks
 * (it was 34 chunks with the original 8 lines). */
#define AIOT_LVGL_DRAW_BUF_LINES    48
#define AIOT_LVGL_DRAW_BUF_BYTES    (AIOT_LCD_H_RES * AIOT_LVGL_DRAW_BUF_LINES * 2)

/* Display refresh period (ms). 33 ms = ~30 FPS max: keeps the PSRAM write
 * stream low enough so the panel never starves during animations.
 * Do NOT go below 30 ms with QUAD PSRAM and bounce buffers. */
#define AIOT_LVGL_REFR_PERIOD_MS    33

/* Touch read period (ms). SPI only, does not load the PSRAM. */
#define AIOT_TOUCH_READ_PERIOD_MS   20

/* Dedicated UI task (LCD init + LVGL + EEZ Flow). Pinned to core 1, so the
 * RGB/GDMA interrupts also live on core 1. WiFi and lwIP stay on core 0. */
#define AIOT_UI_TASK_CORE           1
#define AIOT_UI_TASK_PRIORITY       3
#define AIOT_UI_TASK_STACK_BYTES    (12 * 1024)

#endif /* SYSTEM_DEFINES_AIOT_H */
