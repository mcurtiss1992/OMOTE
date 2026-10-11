#pragma once
#include <lvgl.h>

// Brand icons for the home screen and app tiles (48x48, 4-bit alpha, use with img_recolor)
extern const lv_img_dsc_t icon_apple;
extern const lv_img_dsc_t icon_linux;
extern const lv_img_dsc_t icon_playstation;
extern const lv_img_dsc_t icon_plex;
extern const lv_img_dsc_t icon_jellyfin;
extern const lv_img_dsc_t icon_spotify;
extern const lv_img_dsc_t icon_primevideo;

// The icon for a scene or app name ("Apple TV", "HTPC", "PS5", "Plex", ...), NULL if there is none
const lv_img_dsc_t* icon_forName(const char* name);
