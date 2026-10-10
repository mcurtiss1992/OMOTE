#include <ArduinoJson.h>
#include <lvgl.h>
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/arduinoLayer.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "devices/mediaPlayer/device_htpc/device_htpc.h"
#include "devices/mediaPlayer/device_htpc/gui_htpc.h"
#include "devices/mediaPlayer/device_htpc/htpc_images.h"

#if (ENABLE_WIFI_AND_MQTT == 1)

uint16_t HTPC_PLAY;
uint16_t HTPC_PAUSE;
uint16_t HTPC_PLAY_PAUSE;
uint16_t HTPC_STOP;
uint16_t HTPC_SEEK_BACK;
uint16_t HTPC_SEEK_FORWARD;
uint16_t HTPC_LIGHT_STAIRS_BRIGHTNESS;
uint16_t HTPC_LIGHT_MAIN_BRIGHTNESS;

// first page, from the retained topic
static std::vector<htpcTile> firstPage;
// page currently shown
static std::vector<htpcTile> tiles;
static int pageOffset = 0;
static int total = -1;          // -1 if the bridge didn't tell
static int requestedOffset = -1;
// To make swiping instant, the next page is preloaded (titles and urls only, about 1 kB),
// and the page before is kept when going forward. Posters are only loaded for the visible page.
struct htpcCachedPage {
  int offset = -1;
  std::vector<htpcTile> tiles;
};
static htpcCachedPage nextPage;
static htpcCachedPage previousPage;
static int prefetchOffset = -1;
static htpcPlayerState playerState;
// assume online until the bridge tells otherwise, so that we don't show "offline" before the retained message arrived
static bool bridgeOnline = true;

void register_device_htpc() {
  register_command(&HTPC_PLAY         , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"play\"}"}));
  register_command(&HTPC_PAUSE        , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"pause\"}"}));
  register_command(&HTPC_PLAY_PAUSE   , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"play_pause\"}"}));
  register_command(&HTPC_STOP         , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"stop\"}"}));
  register_command(&HTPC_SEEK_BACK    , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"seek\",\"seconds\":-10}"}));
  register_command(&HTPC_SEEK_FORWARD , makeCommandData(MQTT, {HTPC_TOPIC_CMD, "{\"action\":\"seek\",\"seconds\":30}"}));
  // same topics as device_smarthome, payload must be set when calling commandHandler
  register_command(&HTPC_LIGHT_STAIRS_BRIGHTNESS, makeCommandData(MQTT, {"cinema_stair_setbrightness"}));
  register_command(&HTPC_LIGHT_MAIN_BRIGHTNESS  , makeCommandData(MQTT, {"cinema_main_setbrightness"}));
}

void htpc_playTile(int index) {
  if ((index < 0) || (index >= (int)tiles.size())) {return;}
  JsonDocument doc;
  doc["action"] = "play_tile";
  doc["index"] = pageOffset + index;
  doc["ratingKey"] = tiles[index].ratingKey;
  std::string payload;
  serializeJson(doc, payload);
  omote_log_i("htpc: play tile %d\r\n", pageOffset + index);
  publishMQTTMessage(HTPC_TOPIC_CMD, payload.c_str());
}

const std::vector<htpcTile>& htpc_getTiles() {
  return tiles;
}

int htpc_getPageOffset() {
  return pageOffset;
}

bool htpc_hasNextPage() {
  if (total >= 0) {
    return pageOffset + (int)tiles.size() < total;
  }
  return (int)tiles.size() == HTPC_TILES_PER_PAGE;
}

static void publishGetTiles(int offset) {
  std::string payload = "{\"action\":\"get_tiles\",\"offset\":" + std::to_string(offset) + ",\"count\":" + std::to_string(HTPC_TILES_PER_PAGE) + "}";
  publishMQTTMessage(HTPC_TOPIC_CMD, payload.c_str());
}

static void requestPage(int offset) {
  requestedOffset = offset;
  omote_log_i("htpc: request tiles at offset %d\r\n", offset);
  publishGetTiles(offset);
}

// preload the page after the visible one, if there is one and it is not already there
static void prefetchNow(lv_timer_t* timer) {
  int offset = pageOffset + HTPC_TILES_PER_PAGE;
  if (!htpc_hasNextPage() || (nextPage.offset == offset) || (prefetchOffset == offset)) {return;}
  prefetchOffset = offset;
  publishGetTiles(offset);
}

// Most calls come from the MQTT receive callback, where publishing can deadlock the MQTT client
// (MQTT-C in the simulator). So the request is sent from the GUI loop.
static void prefetchNextPage() {
  lv_timer_t* timer = lv_timer_create(prefetchNow, 0, NULL);
  lv_timer_set_repeat_count(timer, 1);
}

static void showPage(int offset, const std::vector<htpcTile>& items) {
  if (offset > pageOffset) {
    // going forward: keep the page we leave for going back
    previousPage.offset = pageOffset;
    previousPage.tiles = tiles;
  }
  pageOffset = offset;
  tiles = items;
  if (nextPage.offset != pageOffset + HTPC_TILES_PER_PAGE) {
    nextPage.offset = -1;
    nextPage.tiles.clear();
  }
}

int htpc_getTotal() {
  return total;
}

bool htpc_nextPage() {
  // while a page is still being loaded (e.g. a key is held), go on from the page that was asked for
  if (requestedOffset > pageOffset) {
    int offset = requestedOffset + HTPC_TILES_PER_PAGE;
    if ((total >= 0) && (offset >= total)) {return false;}
    requestPage(offset);
    return true;
  }
  if (!htpc_hasNextPage()) {return false;}
  int offset = pageOffset + HTPC_TILES_PER_PAGE;
  if (nextPage.offset == offset) {
    // preloaded: show it right away and preload the one after
    std::vector<htpcTile> items = nextPage.tiles;
    nextPage.offset = -1;
    nextPage.tiles.clear();
    showPage(offset, items);
    htpc_gui_update();
    prefetchNextPage();
    return true;
  }
  requestPage(offset);
  return true;
}

bool htpc_previousPage() {
  // while a page is still being loaded (e.g. a key is held), go on from the page that was asked for
  if ((requestedOffset >= 0) && (requestedOffset < pageOffset)) {
    int offset = requestedOffset - HTPC_TILES_PER_PAGE;
    if (offset < 0) {return false;}
    if (offset == 0) {
      requestedOffset = -1;
      pageOffset = 0;
      tiles = firstPage;
      htpc_gui_update();
      return true;
    }
    requestPage(offset);
    return true;
  }
  if (pageOffset == 0) {return false;}
  int offset = pageOffset - HTPC_TILES_PER_PAGE;
  if (offset < 0) {offset = 0;}
  if ((offset == 0) || (previousPage.offset == offset)) {
    // the first page is always known from the retained topic, the one before was kept
    std::vector<htpcTile> items = (offset == 0) ? firstPage : previousPage.tiles;
    // the page we leave is the next one now
    nextPage.offset = pageOffset;
    nextPage.tiles = tiles;
    requestedOffset = -1;
    pageOffset = offset;
    tiles = items;
    previousPage.offset = -1;
    previousPage.tiles.clear();
    htpc_gui_update();
    return true;
  }
  requestPage(offset);
  return true;
}

const htpcPlayerState& htpc_getPlayerState() {
  return playerState;
}

bool htpc_getBridgeOnline() {
  return bridgeOnline;
}

int64_t htpc_getCurrentPosition_ms() {
  if (!playerState.hasPosition) {return 0;}
  int64_t position = playerState.position_ms;
  if (playerState.playing && !playerState.paused) {
    position += (int64_t)(millis() - playerState.receivedAt);
  }
  if ((playerState.duration_ms > 0) && (position > playerState.duration_ms)) {
    position = playerState.duration_ms;
  }
  return position;
}

static std::vector<htpcTile> parseTileArray(JsonArray array) {
  std::vector<htpcTile> result;
  for (JsonObject tile : array) {
    htpcTile aTile;
    aTile.ratingKey = tile["ratingKey"] | "";
    aTile.title     = tile["title"] | "";
    aTile.year      = tile["year"] | 0;
    aTile.thumbUrl  = tile["thumb4_url"] | "";
    result.push_back(aTile);
  }
  return result;
}

static bool parseFirstPage(const std::string& payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    omote_log_w("htpc: could not parse tiles: %s\r\n", error.c_str());
    return false;
  }
  firstPage = parseTileArray(doc.as<JsonArray>());
  if (pageOffset == 0) {
    tiles = firstPage;
    // the list changed, preload again
    nextPage.offset = -1;
    nextPage.tiles.clear();
    prefetchNextPage();
  }
  return true;
}

static bool parsePage(const std::string& payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    omote_log_w("htpc: could not parse page: %s\r\n", error.c_str());
    return false;
  }
  int offset = doc["offset"] | -1;
  if (offset <= 0) {return false;}
  bool requested = (offset == requestedOffset);
  bool prefetched = (offset == prefetchOffset);
  if (!requested && !prefetched) {return false;} // not a page we asked for
  if (!doc["total"].isNull()) {total = doc["total"];}
  std::vector<htpcTile> items = parseTileArray(doc["items"].as<JsonArray>());

  if (prefetched) {
    prefetchOffset = -1;
    if (items.empty()) {
      // the visible page is the last one
      total = pageOffset + (int)tiles.size();
    } else if (!requested) {
      nextPage.offset = offset;
      nextPage.tiles = items;
      return true; // nothing changes on screen except maybe the position
    }
  }
  if (requested) {
    requestedOffset = -1;
    if (items.empty()) {
      // we were at the end. Remember that, so that the GUI stops offering a next page
      total = pageOffset + (int)tiles.size();
      return true;
    }
    showPage(offset, items);
    prefetchNextPage();
  }
  return true;
}

static void parsePlayerState(const std::string& payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    omote_log_w("htpc: could not parse player state: %s\r\n", error.c_str());
    return;
  }
  htpcPlayerState newState;
  newState.playing     = doc["playing"] | false;
  newState.paused      = (std::string(doc["state"] | "") == "paused");
  newState.title       = doc["title"] | "";
  newState.grandparent = doc["grandparent"] | "";
  newState.thumbUrl    = doc["thumb4_url"] | "";
  newState.hasPosition = !doc["position_ms"].isNull();
  newState.position_ms = doc["position_ms"] | (int64_t)0;
  newState.duration_ms = doc["duration_ms"] | (int64_t)0;
  newState.receivedAt  = millis();
  playerState = newState;
}

// drop images that are neither a tile nor the one playing. Must be called after the GUI stopped using them.
static void pruneImages() {
  std::set<std::string> urls;
  for (const htpcTile& tile : tiles) {
    urls.insert(tile.thumbUrl);
  }
  urls.insert(playerState.thumbUrl);
  htpc_images_retainOnly(urls);
}

bool htpc_handleMQTTmessage(const std::string& topic, const std::string& payload) {
  if (topic == HTPC_TOPIC_TILES) {
    if (parseFirstPage(payload)) {
      htpc_gui_update();
      pruneImages();
    }
    return true;
  } else if (topic == HTPC_TOPIC_PAGE) {
    if (parsePage(payload)) {
      htpc_gui_update();
      pruneImages();
    }
    return true;
  } else if (topic == HTPC_TOPIC_PLAYER_STATE) {
    bool wasPlaying = playerState.playing;
    parsePlayerState(payload);
    htpc_gui_update();
    if (playerState.playing != wasPlaying) {
      htpc_gui_playingChanged(playerState.playing);
    }
    pruneImages();
    return true;
  } else if (topic == HTPC_TOPIC_BRIDGE) {
    bridgeOnline = (payload == "1");
    htpc_gui_update();
    return true;
  }
  return false;
}

#endif
