#pragma once

#include <string>
#include <vector>
#include <stdint.h>

/*
  htpc bridge (Plex on htpc), talked to via MQTT. All payloads are JSON.

  OMOTE subscribes to
    htpc/movies/tiles   (retained) first page, array of up to HTPC_TILES_PER_PAGE movies, newest first: [{"ratingKey","title","year","released","thumb4_url"}, ...]
    htpc/movies/page    (not retained) answer to get_tiles: {"offset":N,"total":T,"items":[same as above]}. "total" is optional.
    htpc/player/state   (retained) {"playing":false} or {"playing":true,"state":"playing"|"paused","title","grandparent","position_ms","duration_ms","thumb4_url",...}
    "thumb4_url" is optional, see htpc_images.h
    htpc/bridge/online  (retained) "1" / "0" (LWT of the bridge)
  OMOTE publishes to
    htpc/omote/cmd      {"action":"play_tile","index":N,"ratingKey":"..."}  N is the index in the whole list (offset + position on the page)
                        {"action":"get_tiles","offset":N,"count":6}
                        {"action":"play_pause"}, {"action":"stop"}, {"action":"seek","seconds":-10}, ...
*/

#define HTPC_TOPIC_TILES        "htpc/movies/tiles"
#define HTPC_TOPIC_PLAYER_STATE "htpc/player/state"
#define HTPC_TOPIC_PAGE         "htpc/movies/page"
#define HTPC_TOPIC_BRIDGE       "htpc/bridge/online"
#define HTPC_TOPIC_CMD          "htpc/omote/cmd"

#define HTPC_TILES_PER_PAGE     6

struct htpcTile {
  std::string ratingKey;
  std::string title;
  int year;
  std::string thumbUrl;
};

struct htpcPlayerState {
  bool playing = false;
  bool paused = false;
  std::string title;
  std::string grandparent;        // show title for episodes, empty for movies
  std::string thumbUrl;
  bool hasPosition = false;
  int64_t position_ms = 0;        // as reported by the bridge
  int64_t duration_ms = 0;
  unsigned long receivedAt = 0;   // millis() when position_ms was received
};

#if (ENABLE_WIFI_AND_MQTT == 1)

extern uint16_t HTPC_PLAY;
extern uint16_t HTPC_PAUSE;
extern uint16_t HTPC_PLAY_PAUSE;
extern uint16_t HTPC_STOP;
extern uint16_t HTPC_SEEK_BACK;     // -10 s
extern uint16_t HTPC_SEEK_FORWARD;  // +30 s
// cinema lights, payload is the brightness 0..100 as "%.2f"
extern uint16_t HTPC_LIGHT_STAIRS_BRIGHTNESS;
extern uint16_t HTPC_LIGHT_MAIN_BRIGHTNESS;

void register_device_htpc();

// used by commandHandler.cpp. Returns true if the topic belongs to the htpc bridge.
bool htpc_handleMQTTmessage(const std::string& topic, const std::string& payload);

// used by gui_htpc.cpp
// index is the position on the current page
void htpc_playTile(int index);
// the tiles of the current page
const std::vector<htpcTile>& htpc_getTiles();
int htpc_getPageOffset();
// true if there is (probably) a next page
bool htpc_hasNextPage();
// number of movies in the whole list, -1 if the bridge did not tell
int htpc_getTotal();
// request the next/previous page. Returns false if there is none. The GUI is updated when the page arrived.
bool htpc_nextPage();
bool htpc_previousPage();
const htpcPlayerState& htpc_getPlayerState();
bool htpc_getBridgeOnline();
// position extrapolated locally while playing
int64_t htpc_getCurrentPosition_ms();

#endif
