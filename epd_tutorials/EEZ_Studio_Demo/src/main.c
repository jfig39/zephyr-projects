#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
// Sensor and I2C headers
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <ui.h>

/* Logical LVGL resolution (must be multiple of 8 for I1) */
#define PANEL_HOR_RES 256U
#define PANEL_VER_RES 128U

/* LVGL draw buffer: +8 bytes palette for I1 */
#define DRAW_BUF_SIZE (((PANEL_HOR_RES * PANEL_VER_RES) / 8U) + 8U)
static uint8_t draw_buf[DRAW_BUF_SIZE];

/* BH1750 node (from your overlay: aliases { light0 = &lux_sensor; }; ) */
#define I2C_NODE DT_ALIAS(light0)

/* Poll every 30 seconds */
#define LUX_PERIOD K_SECONDS(30)

/* --- BH1750 device + work item --- */
#if !DT_NODE_HAS_STATUS(I2C_NODE, okay)
#error "BH1750 (alias 'light0') not found/enabled in devicetree"
#endif
static const struct device *const bh1750 = DEVICE_DT_GET(I2C_NODE);
static struct k_work_delayable lux_work;

/* Latest lux sample; <0 means 'no new sample' */
static double last_lux = -1.0;

/* ------- BH1750 work handler: read and stash value (no LVGL here) ------- */
static void lux_work_handler(struct k_work *work)
{
        ARG_UNUSED(work);

        if (!device_is_ready(bh1750))
        {
                printk("[LUX] device not ready\n");
                (void)k_work_reschedule(&lux_work, LUX_PERIOD);
                return;
        }

        struct sensor_value lux;
        int err = sensor_sample_fetch(bh1750);
        if (!err)
        {
                err = sensor_channel_get(bh1750, SENSOR_CHAN_LIGHT, &lux);
        }
        if (!err)
        {
                double lx = sensor_value_to_double(&lux);
                last_lux = lx; /* consumed in main loop */
                printk("[LUX] %.3f lx\n", lx);
        }
        else
        {
                printk("[LUX] read failed: %d\n", err);
        }

        (void)k_work_reschedule(&lux_work, LUX_PERIOD);
}

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

        // /* Simple demo UI */
        // lv_obj_t *label = lv_label_create(lv_screen_active());
        // lv_label_set_text(label, "Hello from LVGL!");
        // lv_obj_center(label);

        ui_init(); // Initialize your UI

        if (!objects.lux_value_label)
        {
                printk("EEZ label pointer is NULL (check EEZ export / screen init)\n");
        }

        /* Start periodic lux polling (first read after 1 s) */
        k_work_init_delayable(&lux_work, lux_work_handler);
        k_work_schedule(&lux_work, K_SECONDS(1));

        /* LVGL tick/handler loop */
        while (1)
        {
                lv_timer_handler();

                if (last_lux >= 0.0)
                {
                        char buf[32];
                        snprintk(buf, sizeof(buf), "%.1f lx", last_lux);
                        if (objects.lux_value_label)
                        {
                                lv_label_set_text(objects.lux_value_label, buf);
                                /* Render now; LVGL will only flush the label's area */
                                lv_refr_now(lv_disp_get_default());
                        }
                        last_lux = -1.0; /* mark consumed */
                }

                k_msleep(50);
        }
}
