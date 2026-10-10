#pragma once

#include <string>
#include <set>
#include <lvgl.h>

/*
  Poster thumbnails for the htpc GUI.
  The bridge sends a "thumb4_url" (plain http) to a raw LVGL image: 4-byte lv_img_header_t, 16-color BGRA palette,
  then 4 bit per pixel (left pixel in the high nibble). 64x96 is 3140 bytes.
  The downloaded bytes are shown as they are, there is no decoding and no second copy in RAM.
  LVGL cannot zoom indexed images, so the bridge sends them in display size.
*/

#define HTPC_POSTER_WIDTH   64
#define HTPC_POSTER_HEIGHT  96

#if (ENABLE_WIFI_AND_MQTT == 1)

// Returns the image, or NULL if it is not available (yet). An unknown url is queued for download.
const lv_img_dsc_t* htpc_images_get(const std::string& url);
// Downloads at most one queued image. Returns true if an image was loaded, so that the GUI can refresh.
bool htpc_images_loop();
// Forget all images whose url is not in this set. Must only be called when no widget shows them anymore.
void htpc_images_retainOnly(const std::set<std::string>& urls);

#endif
