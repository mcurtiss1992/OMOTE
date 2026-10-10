#include <map>
#include <list>
#include <algorithm>
#include <string.h>
#include <lvgl.h>
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "devices/mediaPlayer/device_htpc/htpc_images.h"

#if (ENABLE_WIFI_AND_MQTT == 1)

// keep this much heap free for WiFi, MQTT and the web server. Below that, tiles show their title instead of the poster.
static const unsigned long minFreeHeap = 16 * 1024;
// the bridge creates posters on demand, so the first download of a new page can fail
static const int maxDownloadAttempts = 3;
static const unsigned long retryDelay_ms = 5000;

static const size_t headerSize = sizeof(lv_img_header_t);
static const size_t paletteSize = 16 * sizeof(lv_color32_t);

struct htpcImage {
  std::string data;              // the downloaded file: header, palette and pixels
  bool failed = false;           // invalid file or all downloads failed, don't try again
  int downloadAttempts = 0;
  unsigned long lastAttempt = 0;
  lv_img_dsc_t dsc;
};

static std::map<std::string, htpcImage> images;
static std::list<std::string> downloadQueue;

static bool isQueued(const std::string& url) {
  return std::find(downloadQueue.begin(), downloadQueue.end(), url) != downloadQueue.end();
}

static bool enoughHeap() {
  unsigned long heapSize, freeHeap, maxAllocHeap, minFree;
  get_heapUsage(&heapSize, &freeHeap, &maxAllocHeap, &minFree);
  // the simulator does not know its heap
  return (heapSize == 0) || (freeHeap > minFreeHeap);
}

// Checks the downloaded file and sets up the image descriptor pointing into it
static bool prepare(htpcImage& image) {
  if (image.data.size() < headerSize + paletteSize) {return false;}
  lv_img_header_t header;
  memcpy(&header, image.data.data(), headerSize);
  size_t expectedSize = headerSize + paletteSize + ((header.w + 1) / 2) * header.h;
  if ((header.cf != LV_IMG_CF_INDEXED_4BIT) || (header.w == 0) || (header.w > HTPC_POSTER_WIDTH) || (header.h == 0) || (header.h > HTPC_POSTER_HEIGHT)
      || (image.data.size() != expectedSize)) {
    omote_log_w("htpc: poster has an unexpected format (cf %d, %dx%d, %u bytes)\r\n", header.cf, header.w, header.h, (unsigned int)image.data.size());
    return false;
  }
  memset(&image.dsc, 0, sizeof(image.dsc));
  image.dsc.header = header;
  image.dsc.data_size = image.data.size() - headerSize;
  image.dsc.data = (const uint8_t*)image.data.data() + headerSize;
  return true;
}

// --- public -----------------------------------------------------------------
const lv_img_dsc_t* htpc_images_get(const std::string& url) {
  if (url.empty()) {return NULL;}

  if (images.count(url) == 0) {
    images[url] = htpcImage();
    downloadQueue.push_back(url);
    return NULL;
  }

  htpcImage& image = images.at(url);
  if (image.failed || image.data.empty()) {return NULL;}
  return &image.dsc;
}

bool htpc_images_loop() {
  // queue failed downloads again after a while
  for (auto& entry : images) {
    htpcImage& image = entry.second;
    if (!image.failed && image.data.empty() && (image.downloadAttempts > 0) && (millis() - image.lastAttempt > retryDelay_ms) && !isQueued(entry.first)) {
      downloadQueue.push_back(entry.first);
    }
  }

  while (!downloadQueue.empty()) {
    std::string url = downloadQueue.front();
    downloadQueue.pop_front();
    if (images.count(url) == 0) {continue;} // forgotten in the meantime
    if (!enoughHeap()) {
      // try again later, maybe another page needs less
      downloadQueue.push_back(url);
      return false;
    }

    htpcImage& image = images.at(url);
    image.downloadAttempts++;
    image.lastAttempt = millis();
    if (!httpDownload(url.c_str(), &image.data)) {
      image.data.clear();
      image.data.shrink_to_fit();
      if (image.downloadAttempts >= maxDownloadAttempts) {image.failed = true;}
      return false;
    }
    if (!prepare(image)) {
      image.data.clear();
      image.data.shrink_to_fit();
      image.failed = true;
      return false;
    }
    // no reallocation from now on, the descriptor points into the string
    image.data.shrink_to_fit();
    image.dsc.data = (const uint8_t*)image.data.data() + headerSize;
    return true;
  }
  return false;
}

void htpc_images_retainOnly(const std::set<std::string>& urls) {
  for (auto it = images.begin(); it != images.end(); ) {
    if (urls.count(it->first) == 0) {
      if (!it->second.data.empty()) {
        lv_img_cache_invalidate_src(&it->second.dsc);
      }
      it = images.erase(it);
    } else {
      ++it;
    }
  }
}

#endif
