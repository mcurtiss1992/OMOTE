#include <ArduinoJson.h>
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

static void requestPage(int offset) {
  requestedOffset = offset;
  std::string payload = "{\"action\":\"get_tiles\",\"offset\":" + std::to_string(offset) + ",\"count\":" + std::to_string(HTPC_TILES_PER_PAGE) + "}";
  omote_log_i("htpc: request tiles at offset %d\r\n", offset);
  publishMQTTMessage(HTPC_TOPIC_CMD, payload.c_str());
}

bool htpc_nextPage() {
  if (!htpc_hasNextPage()) {return false;}
  requestPage(pageOffset + HTPC_TILES_PER_PAGE);
  return true;
}

bool htpc_previousPage() {
  if (pageOffset == 0) {return false;}
  int offset = pageOffset - HTPC_TILES_PER_PAGE;
  if (offset <= 0) {
    // the first page is always known from the retained topic
    requestedOffset = -1;
    pageOffset = 0;
    tiles = firstPage;
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
  if ((offset != requestedOffset) || (offset <= 0)) {return false;} // not the page we asked for
  requestedOffset = -1;
  total = doc["total"] | -1;
  std::vector<htpcTile> items = parseTileArray(doc["items"].as<JsonArray>());
  if (items.empty()) {
    // we were at the end. Remember that, so that the GUI stops offering a next page
    total = pageOffset + (int)tiles.size();
    return true;
  }
  pageOffset = offset;
  tiles = items;
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
