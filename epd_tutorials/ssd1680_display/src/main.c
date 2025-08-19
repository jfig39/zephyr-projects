// main.c — nRF52840 + Zephyr + LVGL v9 + SSD1680 (ssd16xx)

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "Battery_Resized.c" // LVGL image asset

/* Logical canvas for LVGL (byte-aligned). The SSD1680’s physical area is
 * 250x122, so we clip to that inside the flush. */
#define PANEL_HOR_RES 256U
#define PANEL_VER_RES 128U

/* LVGL I1 draw buffer (add 8 bytes for the palette header LVGL prepends). */
#define DRAW_BUF_SIZE (((PANEL_HOR_RES * PANEL_VER_RES) / 8U) + 8U)
static uint8_t draw_buf[DRAW_BUF_SIZE];

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
    if (x2 >= 250)
    {
        x2 = 249;
    }
    if (y2 >= 122)
    {
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
                if (bit_val)
                {
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
        .width = w,
        .pitch = w,
        /* The SSD16xx driver requires the height to be a multiple of 8
         * when the screen is vertically tiled.  Use groups*8 rather than
         * the clipped height to satisfy this constraint. */
        .height = (uint16_t)(groups * 8U),
    };
    int ret = display_write(dev, x1, y1, &desc, vtbuf);
    if (ret)
    {
        printk("display_write() failed: %d\n", ret);
    }
    lv_display_flush_ready(disp);
}

/* Rounder callback: ensures that LVGL only flushes areas starting and
 * ending on 8‑pixel boundaries.  This avoids partial bytes in the
 * pixel data and keeps the flush callback aligned. */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = (a->x1 & ~0x7);
    a->x2 = (a->x2 | 0x7);
    if (a->x2 >= PANEL_HOR_RES)
    {
        a->x2 = PANEL_HOR_RES - 1;
    }
}

int main(void)
{
    /* Bind the chosen display from Devicetree (your overlay points to ssd16xx). */
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev))
    {
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
    if (cap.supported_pixel_formats & PIXEL_FORMAT_MONO01)
    {
        err = display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01);
    }
    else if (cap.supported_pixel_formats & PIXEL_FORMAT_MONO10)
    {
        err = display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10);
    }
    else
    {
        printk("No supported MONO pixel format\n");
    }
    if (err)
    {
        printk("display_set_pixel_format failed: %d\n", err);
    }

    /* LVGL init + display setup */
    lv_init();

    lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
    if (!disp)
    {
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

    display_blanking_off(display_dev);

    /* Simple demo UI */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello from LVGL!");
    lv_obj_center(label);

    /* Force redraw the invalidated areas */
       lv_refr_now(lv_disp_get_default());

    k_sleep(K_SECONDS(5));
    extern const lv_image_dsc_t Battery_Resized;
    lv_obj_t *img = lv_image_create(lv_screen_active());
    lv_image_set_src(img, &Battery_Resized);
    lv_obj_center(img);

    /* LVGL tick/handler loop */
    while (true)
    {
        lv_timer_handler();
        k_msleep(50);
    }
}
