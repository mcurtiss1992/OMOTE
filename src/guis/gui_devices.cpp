#include <string>
#include <vector>
#include <lvgl.h>
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/gui/guiMemoryOptimizer.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "guis/gui_devices.h"
#include "guis/gui_icons.h"
#include "guis/tileGrid.h"

uint16_t GUI_DEVICES_ACTIVATE;

// what the tiles stand for: the device modes
static std::vector<std::string> tileScenes;
// the scene the "Back to ..." bar returns to
static std::string backScene;

// a symbol for devices without a brand icon
static const char* symbolFor(const std::string& name) {
  std::string n = name;
  for (char& c : n) {c = tolower((unsigned char)c);}
  if (n.find("projector") != std::string::npos) {return LV_SYMBOL_IMAGE;}
  if ((n.find("receiver") != std::string::npos) || (n.find("sound") != std::string::npos)) {return LV_SYMBOL_VOLUME_MAX;}
  if (n.find("light") != std::string::npos) {return LV_SYMBOL_CHARGE;}
  return NULL;
}

static void activate(const std::string& scene) {
  uint16_t command = get_activate_scene_command(scene);
  if (command == 0) {
    omote_log_w("Devices: no command for %s\r\n", scene.c_str());
    return;
  }
  executeCommand(command);
}

static void back_cb(int index, bool longPress) {
  activate(backScene);
}

static void deviceTile_cb(int index, bool longPress) {
  if ((index < 0) || (index >= (int)tileScenes.size())) {return;}
  uint16_t command = get_activate_scene_command(tileScenes[index]);
  if (command == 0) {
    omote_log_w("Devices: no command for %s\r\n", tileScenes[index].c_str());
    return;
  }
  executeCommand(command);
}

void create_tab_content_devices(lv_obj_t* tab) {
  lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);

  lv_obj_t* title = lv_label_create(tab);
  lv_label_set_text(title, "Devices");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);

  tileScenes.clear();
  std::vector<tileGridItem> items;
  // a bar back to the scene that runs, if a device mode is active now
  backScene = get_runningScene();
  if (isDeviceMode(gui_memoryOptimizer_getActiveSceneName()) && sceneExists(backScene)) {
    lv_obj_t* backBar = tileGrid_create(tab, 1, back_cb);
    tileGrid_setTileHeight(backBar, 44);
    tileGrid_setItems(backBar, {{std::string(LV_SYMBOL_LEFT "  Back to ") + backScene, NULL, tileGrid_colorForName(backScene.c_str()), NULL}});
  }
  for (const deviceMode& mode : get_deviceModes()) {
    tileScenes.push_back(mode.name);
    const std::string& colorName = mode.theme.empty() ? mode.label : mode.theme;
    const lv_img_dsc_t* icon = icon_forName(mode.label.c_str());
    if (icon == NULL) {icon = icon_forName(mode.theme.c_str());}
    items.push_back({mode.label, icon, tileGrid_colorForName(colorName.c_str()), symbolFor(mode.label + " " + mode.theme)});
  }
  if (items.empty()) {
    lv_obj_t* label = lv_label_create(tab);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, "No devices. Add them in devicemodes.json.");
    return;
  }
  lv_obj_t* grid = tileGrid_create(tab, 2, deviceTile_cb);
  tileGrid_setItems(grid, items);
}

void notify_tab_before_delete_devices(void) {}

void register_gui_devices(void) {
  register_gui(std::string(tabName_devices), & create_tab_content_devices, & notify_tab_before_delete_devices);
  register_command(&GUI_DEVICES_ACTIVATE, makeCommandData(GUI, {std::to_string(MAIN_GUI_LIST), std::string(tabName_devices)}));
}
