/*
 * LVGL Setup for EVSE
 */

#include "lvgl/lvgl.h"
#include "evseMainApp.h"  // For SPI functions, assume

#define DISP_HOR_RES 320
#define DISP_VER_RES 240

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[DISP_HOR_RES * DISP_VER_RES / 10];  // Buffer size

void my_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    // Implement SPI write to ILI9341
    // Use your SPI LCD functions to write pixels
    // For example:
    // spi_lcd_set_window(area->x1, area->y1, area->x2, area->y2);
    // spi_lcd_write_pixels(color_p, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

    lv_disp_flush_ready(disp_drv);
}

void lvgl_setup(void)
{
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_HOR_RES * DISP_VER_RES / 10);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.flush_cb = my_flush_cb;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);
}

void lvgl_task(void *pvParameters)
{
    while(1) {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}