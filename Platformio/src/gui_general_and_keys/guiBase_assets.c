#include <lvgl.h>


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_GRADIENTLEFT
#define LV_ATTRIBUTE_IMG_GRADIENTLEFT
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_GRADIENTLEFT uint8_t gradientLeft_map[] = {
  0xfa, 0xf2, 0xea, 0xe2, 0xda, 0xd1, 0xc7, 0xbe, 0xb7, 0xae, 0xa6, 0x9e, 0x95, 0x8d, 0x84, 0x7d, 0x74, 0x6c, 0x62, 0x5a, 0x51, 0x48, 0x41, 0x38, 0x2f, 0x28, 0x1f, 0x17, 0x0f, 0x07, 
};

const lv_img_dsc_t gradientLeft = {
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 30,
  .header.h = 1,
  .data_size = 30,
  .data = gradientLeft_map,
};


#ifndef LV_ATTRIBUTE_IMG_GRADIENTRIGHT
#define LV_ATTRIBUTE_IMG_GRADIENTRIGHT
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_GRADIENTRIGHT uint8_t gradientRight_map[] = {
  0x07, 0x0f, 0x17, 0x1f, 0x28, 0x2f, 0x38, 0x41, 0x48, 0x51, 0x5a, 0x62, 0x6c, 0x74, 0x7d, 0x84, 0x8d, 0x95, 0x9e, 0xa6, 0xae, 0xb7, 0xbe, 0xc7, 0xd1, 0xda, 0xe2, 0xea, 0xf2, 0xfa, 
};

const lv_img_dsc_t gradientRight = {
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 30,
  .header.h = 1,
  .data_size = 30,
  .data = gradientRight_map,
};


#ifndef LV_ATTRIBUTE_IMG_HIGH_BRIGHTNESS
#define LV_ATTRIBUTE_IMG_HIGH_BRIGHTNESS
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_HIGH_BRIGHTNESS uint8_t high_brightness_map[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc1, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0xd4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x5c, 0x8e, 0x04, 0x00, 0x00, 0x00, 0xc1, 0xc1, 0x00, 0x00, 0x00, 0x04, 0x8e, 0x5c, 0x00, 0x00, 
  0x00, 0x00, 0x8c, 0xff, 0xa8, 0x01, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00, 0x01, 0xa8, 0xff, 0x8c, 0x00, 0x00, 
  0x00, 0x00, 0x04, 0xa9, 0xf5, 0x0d, 0x00, 0x00, 0x11, 0x11, 0x00, 0x00, 0x0d, 0xf5, 0xa9, 0x04, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x42, 0xd4, 0xff, 0xff, 0xd4, 0x41, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xfd, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0xd5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd3, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0xbd, 0xcc, 0xbd, 0x0d, 0x11, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x12, 0x0d, 0xbd, 0xcc, 0xbd, 
  0xbd, 0xcc, 0xbd, 0x0d, 0x11, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x10, 0x0d, 0xbd, 0xcc, 0xbd, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd2, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x40, 0xd3, 0xff, 0xfe, 0xd2, 0x3f, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x04, 0xa9, 0xf5, 0x0d, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x0d, 0xf5, 0xa9, 0x04, 0x00, 0x00, 
  0x00, 0x00, 0x8c, 0xff, 0xa8, 0x01, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00, 0x01, 0xa8, 0xff, 0x8c, 0x00, 0x00, 
  0x00, 0x00, 0x5c, 0x8e, 0x04, 0x00, 0x00, 0x00, 0xc1, 0xc1, 0x00, 0x00, 0x00, 0x04, 0x8e, 0x5c, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0xd4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc1, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

const lv_img_dsc_t high_brightness = {
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 18,
  .header.h = 18,
  .data_size = 324,
  .data = high_brightness_map,
};


#ifndef LV_ATTRIBUTE_IMG_LOW_BRIGHTNESS
#define LV_ATTRIBUTE_IMG_LOW_BRIGHTNESS
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_LOW_BRIGHTNESS uint8_t low_brightness_map[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbd, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x27, 0x72, 0x01, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00, 0x01, 0x72, 0x28, 0x00, 0x00, 
  0x00, 0x00, 0x71, 0xf5, 0x0f, 0x00, 0x00, 0x11, 0x11, 0x00, 0x00, 0x0d, 0xf5, 0x73, 0x00, 0x00, 
  0x00, 0x00, 0x01, 0x0b, 0x00, 0x42, 0xd4, 0xff, 0xff, 0xd4, 0x41, 0x00, 0x0a, 0x01, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x42, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xfd, 0x40, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0xd5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd3, 0x00, 0x00, 0x00, 0x00, 
  0x43, 0xbd, 0x0d, 0x11, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x12, 0x0d, 0xbc, 0x44, 
  0x43, 0xbd, 0x0d, 0x11, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x11, 0x0d, 0xbc, 0x44, 
  0x00, 0x00, 0x00, 0x00, 0xd4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd2, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x41, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x01, 0x0b, 0x00, 0x40, 0xd3, 0xfe, 0xff, 0xd2, 0x3f, 0x00, 0x0a, 0x01, 0x00, 0x00, 
  0x00, 0x00, 0x71, 0xf5, 0x0f, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x0d, 0xf5, 0x73, 0x00, 0x00, 
  0x00, 0x00, 0x27, 0x72, 0x01, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00, 0x01, 0x72, 0x28, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbd, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

const lv_img_dsc_t low_brightness = {
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 16,
  .header.h = 16,
  .data_size = 256,
  .data = low_brightness_map,
};
