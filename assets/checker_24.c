#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_CHECKER
#define LV_ATTRIBUTE_IMG_CHECKER
#endif

/* 24x24 transparency checkerboard, 12px tiles, hand-generated rather than
 * exported: one bit per pixel, MSB first, rows padded to whole bytes.
 *
 * 1-bit alpha, so the two greys come from the style at draw time -- bg_color
 * shows through the 0 bits and bg_img_recolor paints the 1 bits (which needs
 * bg_img_recolor_opa at LV_OPA_COVER). Tiled as a background it replaces the
 * grid of child objects an empty spool used to be built from. */
const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_CHECKER uint8_t checker_map[] = {
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0xff, 0xf0, 0x00,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
  0x00, 0x0f, 0xff,
};

const lv_img_dsc_t checker = {
  .header.cf = LV_IMG_CF_ALPHA_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 24,
  .header.h = 24,
  .data_size = sizeof(checker_map),
  .data = checker_map,
};
