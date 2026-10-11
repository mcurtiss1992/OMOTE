#include <ArduinoJson.h>
#include <lvgl.h>
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/scenes/sceneHandler.h"
#include "applicationInternal/omote_log.h"
#include "devices/mediaPlayer/device_htpc/device_htpc.h"
#include "devices/mediaPlayer/device_htpc/gui_htpc.h"
#include "devices/mediaPlayer/device_hub/device_hub.h"
#include "devices/mediaPlayer/device_hub/gui_browse.h"

#if (ENABLE_WIFI_AND_MQTT == 1)

static hubPage page;
static bool hasPage = false;
// assume online until the hub tells otherwise, so that we don't show "offline" before the retained message arrived
static bool hubOnline = true;

static void publish(JsonDocument& doc) {
  std::string payload;
  serializeJson(doc, payload);
  publishMQTTMessage(HUB_TOPIC_CMD, payload.c_str());
}

void hub_open(const std::string& scene) {
  JsonDocument doc;
  doc["action"] = "open";
  doc["scene"] = scene;
  omote_log_i("hub: open %s\r\n", scene.c_str());
  publish(doc);
}

void hub_select(const std::string& itemId) {
  JsonDocument doc;
  doc["action"] = "select";
  doc["page"] = page.id;
  doc["item"] = itemId;
  omote_log_i("hub: select %s on %s\r\n", itemId.c_str(), page.id.c_str());
  publish(doc);
}

bool hub_back() {
  if (!hasPage || !page.back) {return false;}
  JsonDocument doc;
  doc["action"] = "back";
  doc["page"] = page.id;
  publish(doc);
  return true;
}

const hubPage& hub_getPage() {
  return page;
}

bool hub_hasPageForScene(const std::string& scene) {
  return hasPage && (page.scene == scene);
}

bool hub_isOnline() {
  return hubOnline;
}

// A poster list is shown by the Movies GUI. Setting its query publishes a request and switching GUIs is not safe
// from the MQTT callback (publishing there deadlocks MQTT-C in the simulator), so both are done from a timer.
static htpcQuery pendingQuery;
static void showMovies_timer_cb(lv_timer_t* timer) {
  htpc_setQuery(pendingQuery);
  showGUIbyName(tabName_htpc);
}

static void parsePage(const std::string& payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    omote_log_w("hub: could not parse page: %s\r\n", error.c_str());
    return;
  }
  std::string layout = doc["layout"] | "tiles";
  if (layout == "none") {return;}

  if (layout == "posters") {
    htpcQuery query;
    query.source = doc["source"] | "";
    query.sort   = doc["sort"] | "added";
    query.target = doc["target"] | "";
    query.title  = doc["title"] | "";
    pendingQuery = query;
    lv_timer_t* timer = lv_timer_create(showMovies_timer_cb, 0, NULL);
    lv_timer_set_repeat_count(timer, 1);
    return;
  }

  hubPage newPage;
  newPage.id     = doc["id"] | "";
  newPage.scene  = doc["scene"] | "";
  newPage.title  = doc["title"] | "";
  newPage.layout = layout;
  newPage.back   = doc["back"] | false;
  for (JsonObject item : doc["items"].as<JsonArray>()) {
    hubItem anItem;
    anItem.id    = item["id"] | "";
    anItem.label = item["label"] | "";
    anItem.icon  = item["icon"] | "";
    if (!anItem.id.empty()) {newPage.items.push_back(anItem);}
  }
  page = newPage;
  hasPage = true;
  browse_gui_update();
}

bool hub_handleMQTTmessage(const std::string& topic, const std::string& payload) {
  if (topic == HUB_TOPIC_PAGE) {
    parsePage(payload);
    return true;
  } else if (topic == HUB_TOPIC_ONLINE) {
    hubOnline = (payload == "1");
    browse_gui_update();
    return true;
  }
  return false;
}

#endif
