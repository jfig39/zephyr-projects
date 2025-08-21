# Adding Dynamic Data

This next Section will cover how to update labels with data from a sensor

For this example I will be using a BH1750 Ambient Light Sensor, although the specific sensor is not that important as we will be focusing on how to update a label with data.

### Note on Finding Sensor/I2C Info

If you need to find information such as the compatable tag and sensor functions you can look up the bindings file for the specifc sensor which is contained in `zephyr/dts/bindings/sensor`. The default I2C pins can be found in `zephyr/boards/<board mfg>/<board.dtsi>`

Here is the additional test code I used to test the sensor. 

I added the I2C and sensor config symbols to the prj.conf

```.conf
# ─────────────────────────────
# Low-level peripheral support#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

/* Logical LVGL resolution (must be multiple of 8 for I1) */
#define PANEL_HOR_RES 256U
#define PANEL_VER_RES 128U

/* LVGL draw buffer: +8 bytes palette for I1 */
#define DRAW_BUF_SIZE (((PANEL_HOR_RES * PANEL_VER_RES) / 8U) + 8U)
static uint8_t draw_buf[DRAW_BUF_SIZE];

/* ------- EPD flush: LVGL I1 -> SSD16xx vertical-tiling ------- */
static void epd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
        const struct device *dev = (const struct device *)lv_display_get_user_data(disp);

        /* Skip 8-byte palette for I1 */
        px_map += 8;

        /* Clip to physical (250x122) */
        lv_coord_t x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
        if (x2 >= 250)
                x2 = 249;
        if (y2 >= 122)
                y2 = 121;
        uint16_t w = (uint16_t)(x2 - x1 + 1);
        uint16_t h = (uint16_t)(y2 - y1 + 1);

        /* Convert to vertical-tiling */
        static uint8_t vtbuf[PANEL_HOR_RES * ((PANEL_VER_RES + 7) / 8)];
        uint16_t groups = (h + 7U) >> 3;

        for (uint16_t gx = 0; gx < w; gx++)
        {
                for (uint16_t gy = 0; gy < groups; gy++)
                {
                        uint8_t out_byte = 0;
                        for (uint8_t bit = 0; bit < 8; bit++)
                        {
                                uint16_t row = gy * 8U + bit;
                                uint8_t bit_val = 0;
                                if (row < h)
                                {
                                        lv_coord_t px = x1 + gx;
                                        lv_coord_t py = y1 + row;
                                        uint32_t idx = (uint32_t)py * PANEL_HOR_RES + px;
                                        uint32_t byte_index = idx >> 3;
                                        uint8_t bit_off = idx & 0x7;
                                        bit_val = (px_map[byte_index] >> (7 - bit_off)) & 1U;
                                }
                                if (bit_val)
                                        out_byte |= (1U << (7 - bit));
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
        if (ret)
        {
                printk("display_write() failed: %d\n", ret);
        }

        lv_display_flush_ready(disp);
}

/* Align flush areas to 8px boundaries (I1 byte alignment) */
static void rounder_cb(lv_event_t *e)
{
        lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
        a->x1 &= ~0x7;
        a->x2 |= 0x7;
        if (a->x2 >= PANEL_HOR_RES)
                a->x2 = PANEL_HOR_RES - 1;
        /* y alignment not required for I1; keep as-is to minimize area */
}

/* LVGL tick from Zephyr uptime (ms) */
static uint32_t my_tick_get(void) { return (uint32_t)k_uptime_get(); }

int main(void)
{
        /* LVGL init */
        lv_init();

        const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
        if (!device_is_ready(display_dev))
        {
                printk("Display device not ready\n");
                return;
        }

        lv_tick_set_cb(my_tick_get);

        /* LVGL display setup: PARTIAL render mode so only invalidated areas flush */
        lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
        if (!disp)
        {
                printk("Failed to create LVGL display\n");
                return;
        }
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
        lv_display_set_user_data(disp, (void *)display_dev);
        lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                               LV_DISPLAY_RENDER_MODE_PARTIAL); /* <-- key */

        lv_display_set_flush_cb(disp, epd_flush_cb);
        lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, disp);

        display_blanking_off(display_dev);

        /* Simple demo UI */
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Hello from LVGL!");
        lv_obj_center(label);

        /* LVGL tick/handler loop */
        while (true)
        {
                lv_timer_handler();
                k_msleep(50);
        }
        return 0;
}
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
# CONFIG_LV_Z_USE_DISPLAY is not present in this Zephyr; remove it.
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
CONFIG_LV_USE_IMAGE=y             # v9 name (was LV_USE_IMG)

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

# ─────────────────────────────
# I2C for reading lux sensor
# ─────────────────────────────

CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_BH1750=y
```

Next I updated the device dree overlay
```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
    chosen {
        zephyr,display = &epd;
    };

    aliases {
        light0 = &lux_sensor;
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

&i2c0 {
    status = "okay";
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";

    lux_sensor: bh1750@23 {
        compatible = "rohm,bh1750";
        reg = <0x23>;              /* or 0x5C if ADDR pin is pulled high */
        label = "BH1750";
        status = "okay";
        // power-gpios = <&gpio1 16 GPIO_ACTIVE_HIGH>; /* optional */
    };
};

&pinctrl {
    /omit-if-no-ref/ i2c0_default: i2c0_default {
        group1 {
            psels = <NRF_PSEL(TWIM_SCL, 0, 26)>,  /* SCL = P0.26 */
                    <NRF_PSEL(TWIM_SDA, 0, 27)>;  /* SDA = P0.27 */
        };
    };

    /omit-if-no-ref/ i2c0_sleep: i2c0_sleep {
        group1 {
            psels = <NRF_PSEL(TWIM_SCL, 0, 26)>,
                    <NRF_PSEL(TWIM_SDA, 0, 27)>;
            low-power-enable;
        };
    };
};
```

And I made a test main.c to check if the sensor was working as expected
```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Alias from your overlay: aliases { light0 = &lux_sensor; }; */
#define BH1750_NODE DT_ALIAS(light0)

void main(void)
{
    const struct device *dev = DEVICE_DT_GET(BH1750_NODE);
    if (!device_is_ready(dev)) {
        LOG_ERR("BH1750 device not ready");
        return;
    }

    /* If you power the sensor via a GPIO, consider a 10–20 ms delay here */

    while (1) {
        int rc = sensor_sample_fetch(dev);
        if (rc) {
            LOG_ERR("sample_fetch failed (%d)", rc);
        } else {
            struct sensor_value lux;
            rc = sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &lux);
            if (!rc) {
                LOG_INF("Illuminance: %.2f lux", sensor_value_to_double(&lux));
            } else {
                LOG_ERR("channel_get failed (%d)", rc);
            }
        }
        k_sleep(K_MSEC(500));
    }
}

```

After building and flashing the serial output should look like this 

![Lux sensor output](./images/Lux_sensor_output.png)

Next we will incorperate the Lux sensor output into our existing main code by creating a label to contain the lux sensor data and update it every minute. 

## Incoperating a Dynamic Label Into the Epaper Display

### Design choises
- We will use a LVGL timer to update the UI - this ensures the UI calls run in the same contect as `lv_timer_handler()`, keeping LVGL thread-safe without extra locking.
- The timer callback
    - reads BH1750 (`sensor_sample_fetch + sensor_channel_get`)
    - formats lux text
    - updates the label (`lv_label_set_text`)

### Integration steps
1. Include the sensor headers
At the top of main.c
```c
#include <zephyr/drivers/sensor.h>
```

2. Get the BH1750 device

Right after you init the display/LVGL (before creating the timer):
```c
#define BH1750_NODE DT_ALIAS(light0)
    const struct device *bh1750 = DEVICE_DT_GET(BH1750_NODE);
    if (!device_is_ready(bh1750)){
        printk("BH1750 device not ready\n");
        return 0;
    }
```

3. Create the label and a 60s LVGL timer
- Create a label after you show the battery image
- Create an LVGL timer that runs every 60,000ms with `bh1750` passed via `user_data`

Initalize `lux_label` and make callback to update label with sensor data
```c
static lv_obj_t *lux_label;

static void lux_timer_cb(lv_timer_t *t){
    const struct device *dev = (const struct device *)lv_timer_get_user_data(t);

    if (sensor_sample_fetch(dev) == 0){
        struct sensor_value lux;
        if (sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &lux) == 0){
            char buf[32];
            /* sensor_value)to_double() gives lux as double*/
            snprintk(buf, siezeof(buf), "Lux: %.2f", sensor_value_to_double(lux));
            lv_label_set_text(lux_label, buf);
            /*Re-align if the label size changes*/
            lv_obj_align(lux_label, LV_ALIGN_Bottom_MID, 0, -8);
        }
    }
}

After we create and center the battery image we register our lable in our main void

```c
    lux_label = lv_label_create(lv_screen_active());
    lv_label_set_text(lux_label, "Lux: --.-");
    lv_obj_align(lux_label, LV_ALIGN_Bottom_MID, 0, -8);
```

We then register the LVGL timer

```c
lv_timer_t *lux_timer = lv_timer_create(lux_timer_cb, 60000, (void *)bh1750);
```

And imidiatley update the screen so we dont wait a minute for the update








