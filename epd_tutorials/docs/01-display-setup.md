# Setting up the SSD1680 e‑paper with LVGL and Zephyr

## Table of Contents
- [Setting up the SSD1680 e‑paper with LVGL and Zephyr](#setting-up-the-ssd1680-epaper-with-lvgl-and-zephyr)
  - [Table of Contents](#table-of-contents)
  - [Target Stack](#target-stack)
  - [Overview](#overview)
  - [Hardware \& Wiring](#hardware--wiring)
  - [Project Structure](#project-structure)
  - [Creating the Overlay](#creating-the-overlay)
    - [Selecting the Chosen Display](#selecting-the-chosen-display)
    - [MIPI DBI](#mipi-dbi)
    - [Full Overlay Code](#full-overlay-code)
  - [Configuration (`prj.conf`)](#configuration-prjconf)
  - [How LVGL ↔ Zephyr ↔ SSD1680 interact](#how-lvgl--zephyr--ssd1680-interact)
    - [The `flush` callback](#the-flush-callback)
    - [The `rounder` callback](#the-rounder-callback)
  - [Basic Hello World `main.c`](#basic-hello-world-mainc)
  - [Build \& Flash](#build--flash)
  - [Data Flow Diagram](#data-flow-diagram)
  - [Adding Images](#adding-images)
    - [Convert a PNG to C with LVGL’s script](#convert-a-png-to-c-with-lvgls-script)
    - [Showing the image in `main.c`](#showing-the-image-in-mainc)

---

## Target Stack
- **Board:** nRF52840DK (others work with pin changes)
- **SDK:** Zephyr/NCS v3.0.2
- **GUI:** LVGL v9 (1‑bit I1 format)
- **Panel:** SSD1680 (Adafruit 2.13″)

## Overview
Minimal LVGL app driving an SSD1680 e‑paper via Zephyr’s `ssd16xxfb` driver. We render in LVGL’s I1 format and convert/flush only the invalidated regions for quick partial updates.

## Hardware & Wiring

| Signal | nRF52840DK pin |
|---|---|
| SCK | P1.10 |
| MOSI | P1.11 |
| CS | P1.12 |
| D/C | P1.13 |
| RST | P1.14 |
| BUSY | P1.15 |

The glass is **250×122** but we use a logical **256×128** for byte‑alignment; we clip to 250×122 when writing.

## Project Structure
```
ssd1680_display
├── CMakeLists.txt
├── boards/
│   └── nrf52840dk_nrf52840.overlay
├── prj.conf
└── src/
    └── main.c
```

## Creating the Overlay

### Selecting the Chosen Display
```dts
/ {
    chosen {
        zephyr,display = &epd;
    };
```
### MIPI DBI
```dts
    mipi_dbi_epd: mipi_dbi_epd {
        compatible = "zephyr,mipi-dbi-spi";
        spi-dev = <&spi1>;
        dc-gpios = <&gpio1 13 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpio1 14 GPIO_ACTIVE_LOW>;
        #address-cells = <1>;
        #size-cells = <0>;
```
### Full Overlay Code
```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
    chosen { zephyr,display = &epd; };

    mipi_dbi_epd: mipi_dbi_epd {
        compatible = "zephyr,mipi-dbi-spi";
        spi-dev = <&spi1>;
        dc-gpios = <&gpio1 13 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpio1 14 GPIO_ACTIVE_LOW>;
        #address-cells = <1>;
        #size-cells = <0>;

        epd: ssd16xxfb@0 {
            compatible = "solomon,ssd1680";
            status = "okay";
            reg = <0>;
            label = "EPD0";

            width = <256>;
            height = <128>;
            rotation = <0>;
            busy-gpios = <&gpio1 15 GPIO_ACTIVE_HIGH>;
            mipi-max-frequency = <4000000>;

            /* Optional SSD16xx specifics */
            tssv = <0x80>;
            partial { };
        };
    };
};

&spi1 {
    status = "okay";
    cs-gpios = <&gpio1 12 GPIO_ACTIVE_LOW>;
    pinctrl-0 = <&spi1_default>;
    pinctrl-1 = <&spi1_sleep>;
    pinctrl-names = "default", "sleep";
};

&pinctrl {
    spi1_default: spi1_default {
        group1 {
            psels = <NRF_PSEL(SPIM_SCK, 1, 10)>,
                    <NRF_PSEL(SPIM_MOSI, 1, 11)>;
        };
    };
    spi1_sleep: spi1_sleep {
        group1 {
            psels = <NRF_PSEL(SPIM_SCK, 1, 10)>,
                    <NRF_PSEL(SPIM_MOSI, 1, 11)>;
            low-power-enable;
        };
    };
};
```

## Configuration (`prj.conf`)
```conf
CONFIG_GPIO=y
CONFIG_SPI=y
CONFIG_SPI_NRFX=y
CONFIG_DISPLAY=y
CONFIG_SSD16XX=y

# LVGL
CONFIG_LVGL=y
CONFIG_LV_Z_AUTO_INIT=n
CONFIG_LV_Z_BITS_PER_PIXEL=1
CONFIG_LV_Z_MEM_POOL_SIZE=24576
CONFIG_LV_USE_LABEL=y
CONFIG_LV_USE_THEME_DEFAULT=y

# Logging (optional)
CONFIG_LOG=y
CONFIG_LV_USE_LOG=y
CONFIG_LV_LOG_LEVEL_INFO=y
CONFIG_DISPLAY_LOG_LEVEL_DBG=y

# libc + float printf
CONFIG_NEWLIB_LIBC=y
CONFIG_NEWLIB_LIBC_NANO=y
CONFIG_NEWLIB_LIBC_FLOAT_PRINTF=y
CONFIG_CBPRINTF_FP_SUPPORT=y

# Stack
CONFIG_HW_STACK_PROTECTION=y
CONFIG_MAIN_STACK_SIZE=24576
```

## How LVGL ↔ Zephyr ↔ SSD1680 interact

### The `flush` callback
- LVGL draws 1‑bit, horizontally packed (MSB first) into a buffer (stride = 256).
- SSD16xx RAM is vertically tiled (8 pixels/byte). We convert and call `display_write()`.
- We clip to 250×122 to avoid writing past the panel.

```c
/* ------- EPD flush: LVGL I1 -> SSD16xx vertical-tiling ------- */
static void epd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const struct device *dev = (const struct device *)lv_display_get_user_data(disp);
    px_map += 8; /* skip I1 palette */

    lv_coord_t x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    if (x2 >= 250) x2 = 249;
    if (y2 >= 122) y2 = 121;
    uint16_t w = (uint16_t)(x2 - x1 + 1);
    uint16_t h = (uint16_t)(y2 - y1 + 1);

    static uint8_t vtbuf[256 * ((128 + 7) / 8)];
    uint16_t groups = (h + 7U) >> 3;

    for (uint16_t gx = 0; gx < w; gx++) {
        for (uint16_t gy = 0; gy < groups; gy++) {
            uint8_t out_byte = 0;
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t row = gy * 8U + bit;
                uint8_t bit_val = 0;
                if (row < h) {
                    lv_coord_t px = x1 + gx;
                    lv_coord_t py = y1 + row;
                    uint32_t idx = (uint32_t)py * 256 + px;
                    uint32_t byte_index = idx >> 3;
                    uint8_t bit_off = idx & 0x7;
                    bit_val = (px_map[byte_index] >> (7 - bit_off)) & 1U;
                }
                if (bit_val) out_byte |= (1U << (7 - bit));
            }
            vtbuf[gy * w + gx] = out_byte;
        }
    }

    struct display_buffer_descriptor desc = {
        .buf_size = w * groups,
        .width = w,
        .pitch = w,
        .height = (uint16_t)(groups * 8U),
    };

    int ret = display_write(dev, x1, y1, &desc, vtbuf);
    if (ret) printk("display_write() failed: %d\n", ret);
    lv_display_flush_ready(disp);
}
```

### The `rounder` callback
```c
/* Align flush areas to 8px boundaries (I1 byte alignment) */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 &= ~0x7;
    a->x2 |= 0x7;
    if (a->x2 >= 256) a->x2 = 255;
    /* y alignment not required for I1 */
}
```

## Basic Hello World `main.c`
```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

#define PANEL_HOR_RES 256U
#define PANEL_VER_RES 128U
#define DRAW_BUF_SIZE (((PANEL_HOR_RES * PANEL_VER_RES) / 8U) + 8U)
static uint8_t draw_buf[DRAW_BUF_SIZE];

/* (use the flush/rounder above) */
static uint32_t my_tick_get(void) { return (uint32_t)k_uptime_get(); }

int main(void)
{
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display device not ready\n");
        return 0;
    }

    lv_init();
    lv_tick_set_cb(my_tick_get);

    lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
    if (!disp) { printk("Failed to create LVGL display\n"); return 0; }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    lv_display_set_user_data(disp, (void *)display_dev);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, epd_flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, disp);

    display_blanking_off(display_dev);

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello from LVGL!");
    lv_obj_center(label);

    while (1) { lv_timer_handler(); k_msleep(50); }
}
```

## Build & Flash
Use nRF Connect for VS Code → **Add Build Configuration**. Add your overlay under “Extra Devicetree Overlays”. Build, then **Flash**. You should see **“Hello from LVGL!”** on the panel.

## Data Flow Diagram
```mermaid
flowchart LR
  A[LVGL draw buffer (I1)] --> B[flush_cb: convert to vertical tiling]
  B --> C[display_write() via Zephyr]
  C --> D[SSD1680 (SSD16xx) RAM]
  D --> E[E‑paper glass 250×122]
```

## Adding Images

### Convert a PNG to C with LVGL’s script
```bash
py -3 -m pip install pypng lz4

py -3 .\LVGLImage.py \
  --ofmt C \
  --cf I1 \
  --output .\out \
  --name Battery_Resized \
  .\Battery_Resized.png
```
Move the generated C file into your `src/` folder.

### Showing the image in `main.c`
```c
#include "Battery_Resized.c"

extern const lv_image_dsc_t Battery_Resized;
lv_obj_t *img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &Battery_Resized);
lv_obj_center(img);
```
