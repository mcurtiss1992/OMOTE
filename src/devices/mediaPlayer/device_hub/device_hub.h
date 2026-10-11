#pragma once

#include <string>
#include <vector>

/*
  Menus of the cinema hub (omote-hub on htpc), talked to via MQTT. All payloads are JSON.
  The hub knows what each scene offers (Apple TV apps, Big Launcher entries, Plex/Jellyfin lists, ...),
  the remote only shows one page at a time.

  OMOTE publishes to
    omote/ui/cmd    {"action":"open","scene":"Apple TV"}             the root page of a scene
                    {"action":"request_focused"}                     request the movie highlighted on the TV (a key, see device_Hub.json)
                    {"action":"select","page":"atv/plex","item":"movies"}
                    {"action":"back","page":"atv/plex"}
  OMOTE subscribes to
    omote/ui/page   (not retained) the page to show:
                    {"id":"atv","scene":"Apple TV","title":"Apple TV","layout":"tiles"|"choices","back":false,
                     "items":[{"id":"plex","label":"Plex","icon":"plex"}, ...]}
                    or a poster list, shown by the Movies GUI:
                    {"id":"atv/plex/movies","scene":"Apple TV","title":"Plex movies","layout":"posters",
                     "source":"plex:movies","sort":"released","target":"appletv"}
                    "text" (optional) is shown under the title, e.g. the details of a movie
                    "theme":"plex" (optional) colors the page and its poster list (brand color, see tileGrid_colorForName)
                    "show":true switches to the Browse tab (e.g. the result of a key press while another tab is shown)
                    {"layout":"none"} after an item that only does something (e.g. launches an app): nothing changes on screen
    omote/ui/online (retained) "1" / "0" (LWT of the hub)
*/

#define HUB_TOPIC_PAGE   "omote/ui/page"
#define HUB_TOPIC_CMD    "omote/ui/cmd"
#define HUB_TOPIC_ONLINE "omote/ui/online"

struct hubItem {
  std::string id;
  std::string label;
  std::string icon;     // name for icon_forName(), empty: the label is used
};

struct hubPage {
  std::string id;
  std::string scene;
  std::string title;
  std::string layout;   // "tiles" or "choices"
  std::string text;     // shown under the title, can be empty
  std::string theme;    // brand whose color the page uses, empty: none
  bool back = false;    // the page has a parent page
  std::vector<hubItem> items;
};

#if (ENABLE_WIFI_AND_MQTT == 1)

// used by commandHandler.cpp. Returns true if the topic belongs to the hub.
bool hub_handleMQTTmessage(const std::string& topic, const std::string& payload);

// used by gui_browse.cpp
// the page shown last, check hub_hasPageForScene() first
const hubPage& hub_getPage();
bool hub_hasPageForScene(const std::string& scene);
bool hub_isOnline();
// ask for the root page of a scene
void hub_open(const std::string& scene);
void hub_select(const std::string& itemId);
// used by device_htpc.cpp: an item of a poster list whose target is the hub was tapped
void hub_selectInList(const std::string& source, const std::string& itemId);
// returns false if the page has no parent page
bool hub_back();

#endif
