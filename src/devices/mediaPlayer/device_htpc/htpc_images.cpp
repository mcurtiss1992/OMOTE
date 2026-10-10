#include <map>
#include <list>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "devices/mediaPlayer/device_htpc/htpc_images.h"

#if (ENABLE_WIFI_AND_MQTT == 1)

/*
  Memory: the posters live in ONE block with a fixed number of slots. It is allocated the first time a poster
  is needed and then kept: freeing and allocating a block this big again later fails on the small, fragmented
  ESP32 heap (measured: after a few times the largest free block was too small and only titles were shown).
  Posters are downloaded straight into a slot, so loading and replacing them never fragments the heap.
  If all slots are used, the poster that was drawn longest ago gives its slot away (the grid and the now
  playing view are never shown at the same time).
*/

static const size_t headerSize = sizeof(lv_img_header_t);
static const size_t paletteSize = 16 * sizeof(lv_color32_t);
static const size_t slotSize = headerSize + paletteSize + ((HTPC_POSTER_WIDTH + 1) / 2) * HTPC_POSTER_HEIGHT;
static const int slotCount = 6;
// the slots are only allocated if this much is left in one piece afterwards, otherwise tiles show their titles
static const unsigned long minLargestBlockAfterSlots = 6 * 1024;
// the bridge creates posters on demand, so the first download of a new page can fail
static const int maxDownloadAttempts = 3;
static const unsigned long retryDelay_ms = 5000;

struct htpcImage {
  int slot = -1;                 // -1: not loaded
  bool failed = false;           // invalid file or all downloads failed, don't try again
  int downloadAttempts = 0;
  unsigned long lastAttempt = 0;
  uint32_t lastUsed = 0;         // for giving away the slot that was used longest ago
  lv_img_dsc_t dsc;
};

static std::map<std::string, htpcImage> images;
static std::list<std::string> downloadQueue;
static uint8_t* slots = NULL;
static std::string slotOwner[slotCount];
static uint32_t useCounter = 0;
static bool slotsUnavailable = false;   // allocating failed, don't try again until the tab is created again

static bool isQueued(const std::string& url) {
  return std::find(downloadQueue.begin(), downloadQueue.end(), url) != downloadQueue.end();
}

static bool allocateSlots() {
  if (slots != NULL) {return true;}
  if (slotsUnavailable) {return false;}
  unsigned long heapSize, freeHeap, maxAllocHeap, minFree;
  get_heapUsage(&heapSize, &freeHeap, &maxAllocHeap, &minFree);
  // the simulator does not know the largest block (reports 0)
  if ((maxAllocHeap > 0) && (maxAllocHeap < slotCount * slotSize + minLargestBlockAfterSlots)) {
    omote_log_w("htpc: not enough heap in one piece for the posters (%lu bytes), showing titles\r\n", maxAllocHeap);
    slotsUnavailable = true;
    return false;
  }
  slots = (uint8_t*)malloc(slotCount * slotSize);
  if (slots == NULL) {
    slotsUnavailable = true;
    return false;
  }
  for (int i=0; i<slotCount; i++) {slotOwner[i].clear();}
  return true;
}

// the block itself is kept, only the slots are marked as free
static void clearSlots() {
  slotsUnavailable = false;
  for (int i=0; i<slotCount; i++) {slotOwner[i].clear();}
}

static void releaseSlot(htpcImage& image) {
  if (image.slot >= 0) {
    lv_img_cache_invalidate_src(&image.dsc);
    slotOwner[image.slot].clear();
    image.slot = -1;
  }
}

// a free slot, or the one of the image that was used longest ago
static int takeSlot() {
  for (int i=0; i<slotCount; i++) {
    if (slotOwner[i].empty()) {return i;}
  }
  int oldest = -1;
  uint32_t oldestUse = UINT32_MAX;
  for (int i=0; i<slotCount; i++) {
    auto owner = images.find(slotOwner[i]);
    if ((owner != images.end()) && (owner->second.lastUsed < oldestUse)) {
      oldestUse = owner->second.lastUsed;
      oldest = i;
    }
  }
  if (oldest >= 0) {
    htpcImage& previous = images.at(slotOwner[oldest]);
    releaseSlot(previous);
    // it is downloaded again if it is shown again
    previous.downloadAttempts = 0;
  }
  return oldest;
}

// Checks the file downloaded into a slot and sets up the image descriptor
static bool accept(htpcImage& image, int slot, size_t size) {
  uint8_t* buffer = slots + slot * slotSize;
  if (size < headerSize + paletteSize) {return false;}
  lv_img_header_t header;
  memcpy(&header, buffer, headerSize);
  size_t expectedSize = headerSize + paletteSize + ((header.w + 1) / 2) * header.h;
  if ((header.cf != LV_IMG_CF_INDEXED_4BIT) || (header.w == 0) || (header.w > HTPC_POSTER_WIDTH) || (header.h == 0) || (header.h > HTPC_POSTER_HEIGHT)
      || (size != expectedSize)) {
    omote_log_w("htpc: poster has an unexpected format (cf %d, %dx%d, %u bytes)\r\n", header.cf, header.w, header.h, (unsigned int)size);
    return false;
  }
  image.slot = slot;
  memset(&image.dsc, 0, sizeof(image.dsc));
  image.dsc.header = header;
  image.dsc.data_size = size - headerSize;
  image.dsc.data = buffer + headerSize;
  return true;
}

// --- public -----------------------------------------------------------------
const lv_img_dsc_t* htpc_images_get(const std::string& url) {
  if (url.empty()) {return NULL;}

  auto found = images.find(url);
  if (found == images.end()) {
    images[url] = htpcImage();
    downloadQueue.push_back(url);
    return NULL;
  }

  htpcImage& image = found->second;
  image.lastUsed = ++useCounter;
  if (image.slot < 0) {
    // lost its slot to another poster: load it again
    if (!image.failed && (image.downloadAttempts == 0) && !isQueued(url)) {downloadQueue.push_back(url);}
    return NULL;
  }
  return &image.dsc;
}

bool htpc_images_loop() {
  // queue failed downloads again after a while
  for (auto& entry : images) {
    htpcImage& image = entry.second;
    if (!image.failed && (image.slot < 0) && (image.downloadAttempts > 0) && (millis() - image.lastAttempt > retryDelay_ms) && !isQueued(entry.first)) {
      downloadQueue.push_back(entry.first);
    }
  }

  while (!downloadQueue.empty()) {
    std::string url = downloadQueue.front();
    downloadQueue.pop_front();
    auto found = images.find(url);
    if ((found == images.end()) || (found->second.slot >= 0)) {continue;} // forgotten or loaded in the meantime
    if (!allocateSlots()) {return false;}

    htpcImage& image = found->second;
    image.downloadAttempts++;
    image.lastAttempt = millis();
    // download straight into a slot, no temporary copy on the heap
    int slot = takeSlot();
    if (slot < 0) {return false;}
    size_t size = 0;
    bool ok = httpDownload(url.c_str(), slots + slot * slotSize, slotSize, &size);
    if (ok && accept(image, slot, size)) {
      slotOwner[slot] = url;
      return true;
    }
    if (ok || (image.downloadAttempts >= maxDownloadAttempts)) {image.failed = true;}
    return false;
  }
  return false;
}

void htpc_images_retainOnly(const std::set<std::string>& urls) {
  for (auto it = images.begin(); it != images.end(); ) {
    if (urls.count(it->first) == 0) {
      releaseSlot(it->second);
      it = images.erase(it);
    } else {
      ++it;
    }
  }
  // the Movies tab was deleted
  if (urls.empty()) {
    downloadQueue.clear();
    clearSlots();
  }
}

#endif
