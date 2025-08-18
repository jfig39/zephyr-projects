# Setting up the SSD1680 e-paper with LVGL and Zephyr

## Table of Contents

- [Target Stack](#target-stack)
- [Overview](#overview)
- [Project Objectives](#project-objectives)
- [Hardware & wiring](#hardware-wiring)
- [How the pieces fit](#how-the-pieces-fit)
- [Project Structure](#project-structure)
- [Creating the Overlay](#creating-the-overlay)
  - [Selecting the Chosen Display](#selecting-the-chosen-display)
  - [MIPI DBI](#mipi-dbi)
  - [Defining the ePaper's Frame Buffer](#defining-the-epapers-frame-buffer)
  - [Key Zephyr Files for SSD16xx Displays](#key-zephyr-files-for-ssd16xx-displays)
  - [SPI Bus and Pin Configuration](#spi-bus-and-pin-configuration)
  - [Full Overlay Code](#full-overlay-code)
- [Setting Up the Configuration](#setting-up-the-configuration)
  - [`prj.conf` Configuration Summary](#prjconf-configuration-summary)
  - [Full `prj.conf` file](#full-prjconf-file)
  - [Common Questions / gotchas](#common-questions-gotchas)
- [`main.c`](#mainc)
  - [How LVGL -> Zephyr -> SSD1680 are connected](#how-lvgl-zephyr-ssd1680-are-connected)
  - [The `flush` callback: what it does and why](#the-flush-callback-what-it-does-and-why)
  - [Implementation of `epd_flush_cb`](#implementation-of-epdflushcb)
  - [`flush_cb` notes](#flushcb-notes)
  - [`rounder_cb` Description](#roundercb-description)
  - [How the callbacks tie back to the overlay](#how-the-callbacks-tie-back-to-the-overlay)
  - [Basic Hello World `main.c`](#basic-hello-world-mainc)
- [Building Application and Flashing Board](#building-application-and-flashing-board)

---


## Target Stack

- Development Board: nRF52840DK (compatible with other nordic dev boards with changes to pin assignments)
- Toolchain: Zephyr/NCS v3.0.2 + LVGL v9
- e-paper display: Adafruit 2.13" ePaper Display https://www.adafruit.com/product/4197

## Overview
This tutorial walks through connecting a ssd1680 ePaper device to a nordic NRF dev board

## Project Objectives

- A minimal LVGL v9 app in 1-bit mode
- A custom flush callback that converts LVGL's horizontal 1-bpp data into the SSD16xx vertical-tiled format
- A rounder callback so LVGL only draws byte-aligned areas
- A label and a logo bitmap, with examples of partial and full refresh cycles

## Hardware & wiring

**EPD (SSD1680) on SPI1:**

| Signal | nRF52840DK pin |
|--------|----------------|
| SCK    | P1.10          |
| MOSI   | P1.11          |
| CS     | P1.12          |
| D/C    | P1.13          |
| RST    | P1.14          |
| BUSY   | P1.15          |

The panel is physically 250×122, but the Zephyr SSD16xx driver uses 256×128 internal alignment (bytes/tiles). We’ll declare a logical 256×128 to keep everything byte aligned and clip the part that overhangs the physical glass.

## How the pieces fit

LVGL v9 draws into a buffer in I1 (1-bit) horizontal format, MSB-first, with a fixed stride equal to the logical width. For I1, LVGL reserves 8 bytes at the start of the buffer for a palette.

Zephyr’s display API calls our `flush_cb(disp, area, px_map)` whenever LVGL wants pixels sent to the panel.

SSD16xx (SSD1680) RAM stores pixels 8 vertically per byte (vertical tiling). We convert LVGL’s horizontal bits to that vertical format in the flush callback before calling `display_write()`.

Partial vs full updates: if you flush a sub-rectangle, the driver uses the partial waveform; if you flush the entire screen, it uses the full waveform (the overlay provides both).

## Project Structure

In VS code create a new blank project and add a `boards` folder in the top directory. In the boards folder create the overlay for your specific board.

The starting project should have this structure.

```text
ssd1680_display
├── CMakeLists.txt
├── boards
│   └── nrf52840dk_nrf52840.overlay
├── prj.conf
└── src
    └── main.c
```
## Creating the Overlay

### Selecting the Chosen Display

We start off my selecting our `epd` as the chosen node which tells Zephyr to selcet our ePaper device as the default for our display subsystem.

```dts
/ {
    chosen {
        zephyr,display = &epd;
    };
```

### MIPI DBI

In order for the host device to communicate with the display controller, we use the **Mobile Industry Processor Interface (MIPI) Display Bus Interface (DBI)**. MIPI DBI is a protocol layer on top of our SPI bus that defines how bytes are interpreted by the display controller. It specifies how to differentiate between command and data bytes, how display registers are addressed, and the required sequence of commands to initialize and control the display.

```dts
mipi_dbi_epd: mipi_dbi_epd {
        compatible = "zephyr,mipi-dbi-spi";
        spi-dev = <&spi1>;
        dc-gpios = <&gpio1 13 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpio1 14 GPIO_ACTIVE_LOW>;
        #address-cells = <1>;
        #size-cells = <0>;
```

### Defining the ePaper's Frame Buffer

To add our e-paper display driver in the overlay, we define a `framebuffer` device for the panel. The `SSD1680` controller is part of the `SSD16xx` family in Zephyr’s core drivers, which is why the device is named `ssd16xxfb` rather than `ssd1680fb`. In Zephyr, the SSD16xx framebuffer driver supports multiple controller variants (SSD1606, SSD1675, SSD1680, etc.), so the name reflects the family, not the specific model.

The compatible field follows the syntax `<vendor>,<device>`, giving us `solomon,ssd1680` for this controller.

We also specify the display’s logical `width` and `height`, rounded up to the nearest multiple of 8 so that the `framebuffer` data is byte-aligned.

The `tssv` property corresponds to a register in the `SSD1680` that selects which waveform table to use from the controller’s internal memory. E-paper displays require a specific sequence of voltage pulses to rearrange the charged pigment particles in the panel for the desired image. These sequences; called waveforms, control the transition behavior, ghosting reduction, and update speed.

In addition, we define border waveforms for full and partial refreshes. Full refresh waveforms clear and redraw the entire panel, while partial refresh waveforms are optimized for updating smaller regions more quickly. Together, the tssv setting and the selected border waveforms determine exactly how the panel updates.



```dts
   epd: ssd16xxfb@0 {
            compatible = "solomon,ssd1680";
            status = "okay";
            reg = <0>;
            label = "EPD0";

            /* REQUIRED / USED BY DRIVER */
            width = <256>;
            height = <128>;
            rotation = <0>;
            busy-gpios = <&gpio1 15 GPIO_ACTIVE_HIGH>;
            mipi-max-frequency = <4000000>;

            /* Optional SSD16xx specifics */
            tssv = <0x80>;
            full    { border-waveform = <0x05>; };
            partial { border-waveform = <0x3c>; };
        };
```

### Key Zephyr Files for SSD16xx Displays
More information on the SSD16xx family can be found in two places in NCS v3.0.2:

1. Driver source code -> located in
`ncs/v3.0.2/zephyr/drivers/display/ssd16xx.c` (and related files).
This contains the C implementation of the driver logic, including SPI transactions, waveform handling, and integration with Zephyr’s display API.

2. Devicetree bindings -> located in the `ncs/v3.0.2/zephyr/dts/bindings/display` directory:

- `solomon,ssd16xx-common.yaml` -> Defines properties that are shared across all SSD16xx-based displays (e.g., `tssv`, `border-waveform`, `busy-gpios`).

- `solomon,ssd16xxfb.yaml` → Extends the common binding with properties specific to the framebuffer driver (`ssd16xxfb`), such as `width`, `height`, and `mipi-max-frequency`.

Zephyr uses solomon,`ssd16xxfb.yaml` as the actual compatible target in your overlay, but it includes the definitions from `solomon,ssd16xx-common.yaml` so that shared properties don’t have to be redefined in every variant.

### SPI Bus and Pin Configuration

The following section enables spi1 for use with our e-paper display and configures its chip select line and pin assignments:

```dts
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

- status = "okay"; — Activates the spi1 peripheral in the devicetree.
- cs-gpios — Defines the GPIO used for chip select (here, GPIO1 pin 12, active low).
- pinctrl-0 / pinctrl-1 — Assign the pin control groups for default operation and low-power (sleep) mode.
- pinctrl-names — Names the two configurations "default" and "sleep" so the driver can switch between them.

### Full Overlay Code

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
    chosen {
        zephyr,display = &epd;
    };

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

            /* REQUIRED / USED BY DRIVER */
            width = <256>;
            height = <128>;
            rotation = <0>;
            busy-gpios = <&gpio1 15 GPIO_ACTIVE_HIGH>;
            mipi-max-frequency = <4000000>;

            /* Optional SSD16xx specifics */
            tssv = <0x80>;
            full    { border-waveform = <0x05>; };
            partial { border-waveform = <0x3c>; };
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

## Setting Up the Configuration

The `prj.conf` file defines the Kconfig options used when building this project. These options control which Zephyr subsystems and LVGL features are included, how much memory is allocated, and how peripherals like SPI and GPIO are configured for the SSD1680 e-paper display.

**Compatibility note:** Some configuration options available in newer versions of Zephyr’s LVGL are not present in NCS v3.0.2. The settings shown here are the ones that worked for me while developing on NCS v3.0.2.

### `prj.conf` Configuration Summary

| **Config Option(s)**                                                                                                                                               | **Purpose**                                                                                                                 |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------- |
| `CONFIG_SPI`, `CONFIG_SPI_NRFX`, `CONFIG_GPIO`                                                                                                                     | Enables core SPI and GPIO drivers. `CONFIG_SPI_NRFX` uses the Nordic-specific SPI implementation for performance.           |
| `CONFIG_DISPLAY`, `CONFIG_SSD16XX`                                                                                                                                 | Activates Zephyr’s display subsystem and the SSD16xx framebuffer driver for the SSD1680 controller.                         |
| `CONFIG_LVGL`                                                                                                                                                      | Enables the LVGL graphics library.                                                                                          |
| `CONFIG_LV_Z_AUTO_INIT=n`                                                                                                                                          | Disables LVGL’s automatic initialization so the display can be registered manually, avoiding race conditions in NCS v3.0.2. |
| `CONFIG_LV_Z_USE_DISPLAY`                                                                                                                                          | Keeps Zephyr’s LVGL display glue layer for tick helpers and color/BPP handling.                                             |
| `CONFIG_LV_Z_BITS_PER_PIXEL=1`                                                                                                                                     | Uses 1-bit-per-pixel mode for monochrome e-paper, minimizing memory use and transfer size.                                  |
| `CONFIG_LV_Z_MEM_POOL_SIZE=24576`                                                                                                                                  | Allocates memory for LVGL’s internal draw buffers and objects.                                                              |
| `CONFIG_LV_USE_LABEL`, `CONFIG_LV_LABEL_TEXT_SELECTION`, `CONFIG_LV_LABEL_LONG_TXT_HINT`, `CONFIG_LV_USE_LINE`, `CONFIG_LV_USE_THEME_DEFAULT`, `CONFIG_LV_USE_IMG` | Enables specific LVGL widgets and features needed for the UI.                                                               |
| `CONFIG_LOG`, `CONFIG_LV_USE_LOG`, `CONFIG_LV_LOG_LEVEL_INFO`, `CONFIG_DISPLAY_LOG_LEVEL_DBG`                                                                      | Enables logging for Zephyr, LVGL, and the display driver to help with debugging.                                            |
| `CONFIG_ASSERT`, `CONFIG_HW_STACK_PROTECTION`                                                                                                                      | Improves system robustness by catching programming errors and stack overflows early.                                        |
| `CONFIG_MAIN_STACK_SIZE=8192`, `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=3072`                                                                                           | Allocates stack memory for the main thread and system workqueue.                                                            |
| `CONFIG_NEWLIB_LIBC`, `CONFIG_NEWLIB_LIBC_NANO`, `CONFIG_NEWLIB_LIBC_FLOAT_PRINTF`                                                                                 | Uses the newlib C library (nano variant) with floating-point `printf` support.                                              |
| `CONFIG_CBPRINTF_FP_SUPPORT`                                                                                                                                       | Enables floating-point formatting for `cbprintf` and related functions.                                                     |

### Full `prj.conf` file

```.conf
# ─────────────────────────────
# Low-level peripheral support
# ─────────────────────────────
CONFIG_SPI=y
CONFIG_SPI_NRFX=y
CONFIG_GPIO=y

# ─────────────────────────────
# Display driver
# ─────────────────────────────
CONFIG_DISPLAY=y
CONFIG_SSD16XX=y                

# ─────────────────────────────
# LVGL (manual init; you call lv_init / register display yourself)
# ─────────────────────────────
CONFIG_LVGL=y
CONFIG_LV_Z_AUTO_INIT=n
CONFIG_LV_Z_USE_DISPLAY=y       
CONFIG_LV_Z_BITS_PER_PIXEL=1    
CONFIG_LV_Z_MEM_POOL_SIZE=24576   

# ─────────────────────────────
# LVGL widgets / features (enable what you actually use)
# ─────────────────────────────
CONFIG_LV_USE_LABEL=y
CONFIG_LV_LABEL_TEXT_SELECTION=y
CONFIG_LV_LABEL_LONG_TXT_HINT=y
CONFIG_LV_USE_LINE=y
CONFIG_LV_USE_THEME_DEFAULT=y
CONFIG_LV_USE_IMG=y

# ─────────────────────────────
# Logging
# ─────────────────────────────
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_DISPLAY_LOG_LEVEL_DBG=y
CONFIG_LV_USE_LOG=y
CONFIG_LV_LOG_LEVEL_INFO=y

# ─────────────────────────────
# Robustness / debug
# ─────────────────────────────
CONFIG_ASSERT=y
CONFIG_HW_STACK_PROTECTION=y

# ─────────────────────────────
# Memory / stacks
# ─────────────────────────────
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=3072

# ─────────────────────────────
# C library / printf
# ─────────────────────────────
CONFIG_NEWLIB_LIBC=y
CONFIG_NEWLIB_LIBC_NANO=y
CONFIG_NEWLIB_LIBC_FLOAT_PRINTF=y
CONFIG_CBPRINTF_FP_SUPPORT=y

```

### Common Questions / gotchas

*“Why 1-bit color?”*

- E-paper is monochrome; 1bpp reduces RAM and transfer time. LVGL supports it and will dither/threshold if needed.

*“No MISO in pinctrl?”*

- SSD1680 over DBI Type-C is effectively write-only. It’s normal to set only SCK and MOSI.

*“Partial refresh”*

- Handled by the SSD16xx driver when you flush a sub-rectangle. Your overlay’s partial/full nodes + tssv pick the waveforms.

*“My build can’t find LVGL font or object symbols”*

- Names vary slightly across SDK versions. If a symbol doesn’t exist, open menuconfig (west build -t menuconfig) → Modules → LVGL and enable any small font (e.g., a Montserrat or unscii font) and at least one object (label is a good start). The exact Kconfig names shown there will be correct for your tree.

## `main.c`

### How LVGL -> Zephyr -> SSD1680 are connected

1. From the Overlay to a Zephyr Device

In the overlay we declared:

```dts
/ { chosen { zephyr,display = &epd; }; };

&spi1 { /* ... */ };

epd: ssd16xxfb@0 {
    compatible = "solomon,ssd1680";
    /* width/height, tssv, waveforms, busy-gpios, etc. */
};
```

At boot, Zephyr binds the `ssd16xxfb` driver to the `epd` node and exposes it as a `struct device` this is usefull since we can abtract away the device driver implmentation from our source code allowing us to simply modify our overlay if we are using a different e-paper without having to modify our `main.c`

In `main.c` we get the device struct with:

```c
const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
```

This returns an instance of the driver containing our configurations defined in the overlay file

2. From Zephyr device to LVGL display object

We create a display which requires our display's resolution, color format, and buffer information

```c
lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                       LV_DISPLAY_RENDER_MODE_FULL);
```
The Zephyr device pointer is placed inside the LVGL display object so the flush callback can reach it

```c
lv_display_set_user_data(disp, (void *)display_dev);
```

So now we can assign the display to our flush and event callbacks

```c
lv_display_set_flush_cb(disp, epd_flush_cb);
lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, disp);

```

### The `flush` callback: what it does and why

Purpose: This function takes the rectangle of pixels LVGL renders into `draw_buf` and pushes only that region to the hardware, respecting the controller's memory layout and the panel's quirks.

**Key Points**

- We configured LVGL to render 1-bit pixes using `LV_COLOR_FORMAT_I1`. These pixels are horizontally packed, MSB-first, with a full-width stride of of `PANEL_HOR_RES` (256)
- The SSD16xx controllers store RAM in vertical tiles (`SCREEN_INFO_MONO_VTILTED`), meaning each byte = 8 vertical pixels (MSB is top)
- The physical glass is 250x122, but 256x128 are exposed to LVGL for byte alingment, so we must clip to 250x122 when sending

### Implementation of `epd_flush_cb`

```c
static void epd_flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map)
{
    const struct device *dev = (const struct device *)lv_display_get_user_data(disp);
    /* Skip the 8‑byte palette */
    px_map += 8;

    /* Clip the coordinates to the physical panel size */
    lv_coord_t x1 = area->x1;
    lv_coord_t y1 = area->y1;
    lv_coord_t x2 = area->x2;
    lv_coord_t y2 = area->y2;
    if (x2 >= 250) {
        x2 = 249;
    }
    if (y2 >= 122) {
        y2 = 121;
    }
    uint16_t w = (uint16_t)(x2 - x1 + 1);
    uint16_t h = (uint16_t)(y2 - y1 + 1);

    /* The SSD16xx driver uses vertical tiling: each byte contains 8
     * vertical pixels.  We convert the LVGL buffer into this format.
     * Use a static buffer sized for the worst case: width × ceil(height/8).
     */
    static uint8_t vtbuf[PANEL_HOR_RES * ((PANEL_VER_RES + 7) / 8)];
    uint16_t groups = (h + 7U) >> 3; /* number of 8‑row groups */
    for (uint16_t gx = 0; gx < w; gx++) {
        for (uint16_t gy = 0; gy < groups; gy++) {
            uint8_t out_byte = 0;
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t row = gy * 8U + bit;
                uint8_t bit_val = 0;
                if (row < h) {
                    /* Compute global coordinates */
                    lv_coord_t px = x1 + gx;
                    lv_coord_t py = y1 + row;
                    /* Compute index into source buffer (horizontal format) */
                    uint32_t pixel_index = (uint32_t)py * PANEL_HOR_RES + px;
                    uint32_t byte_index = pixel_index >> 3;
                    uint8_t bit_offset = pixel_index & 0x7;
                    /* For I1 format, MSB corresponds to leftmost pixel */
                    bit_val = (px_map[byte_index] >> (7 - bit_offset)) & 0x1U;
                }
                /* For MSB‑first displays, set bits from MSB to LSB */
                if (bit_val) {
                    out_byte |= (1U << (7 - bit));
                }
            }
            /* Store vertical tile */
            vtbuf[gy * w + gx] = out_byte;
        }
    }

    /* Prepare descriptor: width and pitch both equal the number of
     * columns (w).  Height remains the number of pixel rows (h).
     * Buffer size is w × groups bytes. */
    struct display_buffer_descriptor desc = {
        .buf_size = w * groups,
        .width    = w,
        .pitch    = w,
        /* The SSD16xx driver requires the height to be a multiple of 8
         * when the screen is vertically tiled.  Use groups*8 rather than
         * the clipped height to satisfy this constraint. */
        .height   = (uint16_t)(groups * 8U),
    };
    int ret = display_write(dev, x1, y1, &desc, vtbuf);
    if (ret) {
        printk("display_write() failed: %d\n", ret);
    }
    lv_display_flush_ready(disp);
}

```

### `flush_cb` notes

- Palette skip (8 bytes): LVGL reserves a small header for I1; if it is not skipped, your top row get corrupted
- Clipping: Prevents accidental writes beyond 250x122 (physical panel size) even though LVGL believes 256x128 exits
- Vertical-tiling convert: Matches the controller’s memory format; without this, content appears scrambled or interleaved.
- Descriptor `width/pitch/height`: Must correspond to the converted buffer layout; for VTILED displays, `height` is rounded up to a multiple of 8
- For partial refresh: LVGL passes only the area that changed

### `rounder_cb` Description

This function Makes LVGL's invalidate/flush rectangles line up with the hardware's memory granularity so the 1-bit VTILED assumptions hold and we prevent "bit smearing"

```c

static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = (a->x1 & ~0x7);
    a->x2 = (a->x2 | 0x7);
    if (a->x2 >= PANEL_HOR_RES) {
        a->x2 = PANEL_HOR_RES - 1;
    }
}

```

This ensures the following
- Horizontal byte alingment: Since LVGL's source buffer `px_map` is horizontal 1bpp (8 pixels per byte), forcing `x1` to a multiple of 8 and `x2` to somewhere between 0 and 7 ensures the region you read from is composed of full bytes. This:
-- Simplifies bit extraction
--Avoids reading partial bytes at the edges
-- Prevents corruption when LVGL asks to flush a narrow, odd-aligned rectangle
- Vertical alingment: handled in `flush_cb`

### How the callbacks tie back to the overlay
1. Overlay sets bus/pins/waveforms and names the display via `label = "epd"`; `chosen { zephyr,display = &epd; } makes it the default
2. Zephyr binds the `ssd16xxfb` driver to that node and exposes it as a `struct device *(display_dev)`
3. In the application code
- This creates an LVGL display `(lv_display_create(...))` with logical `256x128` and `I1` format
- Stashes `display_dev` as user_data on the LVGL display
- Registers `rounder_cb` to align LVGLs rectangles and `epd_flush_cb` to do the conversion and call `display_write()` with the proper buffer descriptor
- The SSD16xx driver chooses partial/full waveforms (defined in the overlay) depending on the region and update, and toggles the `busy` pin appropriately

### Basic Hello World `main.c`

```c
int main(void)
{
    /* Bind the chosen display from Devicetree (your overlay points to ssd16xx). */
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display device not ready\n");
        return 0;
    }
    printk("Display device: %p\n", display_dev);

    /* Pick a supported monochrome pixel format. */
    struct display_capabilities cap;
    display_get_capabilities(display_dev, &cap);
    printk("Display caps: formats=0x%x, x_res=%u y_res=%u\n",
           cap.supported_pixel_formats, cap.x_resolution, cap.y_resolution);

    int err = 0;
    if (cap.supported_pixel_formats & PIXEL_FORMAT_MONO01) {
        err = display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01);
    } else if (cap.supported_pixel_formats & PIXEL_FORMAT_MONO10) {
        err = display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10);
    } else {
        printk("No supported MONO pixel format\n");
    }
    if (err) {
        printk("display_set_pixel_format failed: %d\n", err);
    }

    /* LVGL init + display setup */
    lv_init();

    lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
    if (!disp) {
        printk("lv_display_create failed\n");
        return 0;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    lv_display_set_user_data(disp, (void *)display_dev);

    /* Single full-screen I1 buffer; LVGL will flush sub-areas. */
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);

    /* Register flush + rounder (rounder via v9 event). */
    lv_display_set_flush_cb(disp, epd_flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    /* Simple demo UI */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello from LVGL!");
    lv_obj_center(label);

    /* Power up the panel */
    display_blanking_off(display_dev);

    /* LVGL tick/handler loop */
    while (true) {
        lv_timer_handler();
        k_msleep(50);
    }
}
```

## Building Application and Flashing Board

To build the application use the **Add build configuration** option under the **APPLICATIONS** window in the nRF-Connect extention

![Add Build Configuration](./images/01-Add_Build_Configuration.png)

In the build configuration wizard select the board you are using and add your overlay file to the **Extra Devicetree Overlays** dropdown list

Select **Generate and Build** at the bottom of the wizard

After the build completes you can flash the board by selecting **Flash** under the **Actions** window in the nRF-Connect extention

The display will show "Hello from LVGL!" if the programming was sucsessful.

![Hello](./images/Hello.png)



