#include <lvgl.h>
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/gui/guiMemoryOptimizer.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/keys.h"
#include "scenes/scene__default.h"
#include "guis/gui_icons.h"
#include "guis/tileGrid.h"
#include "devices/mediaPlayer/device_hub/device_hub.h"
#include "devices/mediaPlayer/device_hub/gui_browse.h"

std::map<char, repeatModes> key_repeatModes_browse = {};
std::map<char, uint16_t> key_commands_short_browse = {};
std::map<char, uint16_t> key_commands_long_browse = {};
uint16_t HUB_BACK;

#if (ENABLE_WIFI_AND_MQTT == 1)

static const unsigned long retryInterval_ms = 5000;
static const lv_coord_t choiceHeight = 56;

static lv_obj_t* browseTab = NULL;
static lv_obj_t* titleLabel = NULL;
static lv_obj_t* statusLabel = NULL;
static lv_obj_t* grid = NULL;
static lv_timer_t* retryTimer = NULL;
static std::string shownPageId;
static std::string shownLayout;

static std::string activeScene() {
  return gui_memoryOptimizer_getActiveSceneName();
}

static void tile_cb(int index, bool longPress) {
  const hubPage& page = hub_getPage();
  if ((index < 0) || (index >= (int)page.items.size())) {return;}
  hub_select(page.items[index].id);
}

static void title_event_cb(lv_event_t* e) {
  hub_back();
}

static void update() {
  if (browseTab == NULL) {return;}
  std::string scene = activeScene();

  if (!hub_hasPageForScene(scene)) {
    lv_label_set_text(titleLabel, scene.c_str());
    lv_label_set_text(statusLabel, hub_isOnline() ? "Loading..." : "The cinema hub is offline");
    lv_obj_clear_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const hubPage& page = hub_getPage();
  std::string title = page.back ? std::string(LV_SYMBOL_LEFT " ") + page.title : page.title;
  lv_label_set_text(titleLabel, title.c_str());
  if (page.back) {
    lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_flag(titleLabel, LV_OBJ_FLAG_CLICKABLE);
  }
  if (!hub_isOnline()) {
    lv_label_set_text(statusLabel, "The cinema hub is offline");
    lv_obj_clear_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  }

  // rebuild the tiles only if the page changed, so that a pressed tile is not redrawn needlessly
  if ((page.id == shownPageId) && (page.layout == shownLayout)) {return;}
  shownPageId = page.id;
  shownLayout = page.layout;

  bool choices = (page.layout == "choices");
  lv_color_t pageColor = tileGrid_colorForName(page.title.c_str());
  std::vector<tileGridItem> items;
  for (const hubItem& item : page.items) {
    const std::string& iconName = item.icon.empty() ? item.label : item.icon;
    if (choices) {
      items.push_back({item.label, NULL, pageColor});
    } else {
      items.push_back({item.label, icon_forName(iconName.c_str()), tileGrid_colorForName(iconName.c_str())});
    }
  }
  // a new grid for a new page: the number of columns can change
  lv_obj_del(grid);
  grid = tileGrid_create(browseTab, choices ? 1 : 2, tile_cb);
  if (choices) {tileGrid_setTileHeight(grid, choiceHeight);}
  tileGrid_setItems(grid, items);
  lv_obj_scroll_to_y(browseTab, 0, LV_ANIM_OFF);
}

// MQTT messages arrive outside of the GUI, so the update is done from a timer
static void update_timer_cb(lv_timer_t* timer) {
  update();
}

void browse_gui_update(void) {
  if (browseTab == NULL) {return;}
  lv_timer_t* timer = lv_timer_create(update_timer_cb, 0, NULL);
  lv_timer_set_repeat_count(timer, 1);
}

// ask for the scene's menu until it arrives
static void retry_timer_cb(lv_timer_t* timer) {
  if (!hub_hasPageForScene(activeScene())) {hub_open(activeScene());}
}

void create_tab_content_browse(lv_obj_t* tab) {
  browseTab = tab;
  shownPageId.clear();
  shownLayout.clear();
  lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);

  titleLabel = lv_label_create(tab);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_width(titleLabel, lv_pct(100));
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_ext_click_area(titleLabel, 8);
  lv_obj_add_event_cb(titleLabel, title_event_cb, LV_EVENT_CLICKED, NULL);

  statusLabel = lv_label_create(tab);
  lv_obj_set_width(statusLabel, lv_pct(100));
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);

  grid = tileGrid_create(tab, 2, tile_cb);

  if (!hub_hasPageForScene(activeScene())) {hub_open(activeScene());}
  retryTimer = lv_timer_create(retry_timer_cb, retryInterval_ms, NULL);
  update();
}

void notify_tab_before_delete_browse(void) {
  browseTab = NULL;
  titleLabel = NULL;
  statusLabel = NULL;
  grid = NULL;
  if (retryTimer != NULL) {
    lv_timer_del(retryTimer);
    retryTimer = NULL;
  }
}

// Back: to the parent page, from the root page to the scene selection
static void hubBack_cb(void) {
  if (!hub_back()) {executeCommand(SCENE_SELECTION);}
}

void gui_setKeys_browse() {
  key_repeatModes_browse = {
    {KEY_BACK, SHORT},
  };
  key_commands_short_browse = {
    {KEY_BACK, HUB_BACK},
  };
}

#else

void browse_gui_update(void) {}

void create_tab_content_browse(lv_obj_t* tab) {
  lv_obj_t* label = lv_label_create(tab);
  lv_label_set_text(label, "Needs WiFi and MQTT");
}

void notify_tab_before_delete_browse(void) {}

static void hubBack_cb(void) {}

void gui_setKeys_browse() {}

#endif

void register_gui_browse(void){
  register_callbackCommand(&HUB_BACK, hubBack_cb);

  register_gui(
    std::string(tabName_browse),
    & create_tab_content_browse,
    & notify_tab_before_delete_browse,
    & gui_setKeys_browse,
    & key_repeatModes_browse,
    & key_commands_short_browse,
    & key_commands_long_browse
    );
}
