/*
 * Omote Dynamic GUI Module
 *
 * This file implements the loading and creation of dynamic GUI tabs.
 *
 * It performs the following:
 *  - Reads the master guis.json file (from the config storage) which contains
 *    a list of GUIs (each with "name" and "guiname").
 *  - For each GUI, loads the individual JSON file (named "gui_[guiname].json")
 *    which defines a list of widgets, and keeps a compact parsed copy in RAM.
 *    Tabs are created and deleted every time the user swipes, so reading and
 *    parsing the file there made every swipe slow.
 *  - Creates the widgets in a 12-unit grid layout (using x, y, w, h from the JSON)
 *    and attaches an event callback that calls executeCommand.
 *
 * Widgets supported:
 *    - slider: A slider that executes its command while it is moved (at most every
 *              SLIDER_SEND_INTERVAL_MS, plus the final value on release), passing the
 *              slider value as a string parameter.
 *    - switch: A switch that executes its command on toggle (passing "true" or "false").
 *    - text:   A label widget that displays text (non-interactive).
 *    - button: A clickable button with a label that executes its command when clicked.
 */

#include <ArduinoJson.h>
#include <lvgl.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#include "applicationInternal/commandHandler.h" // Provides executeCommand()
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/gui/guiMemoryOptimizer.h"
#include "applicationInternal/hardware/hardwarePresenter.h" // millis()
#include "applicationInternal/omote_log.h"
#include "devices_dynamic.h"
#include "guis_dynamic.h"

// ---------------------------------------------------------------------------
// Grid configuration – a 12-unit grid is used. Row 0 holds the title of the GUI,
// widget row y of the config file is placed in grid row y+1.
// ---------------------------------------------------------------------------
#define GRID_COLUMNS 12
#define GRID_ROW_HEIGHT 20  // pixel height per grid unit
// Sliders send IR/MQTT while being dragged. Sending on every pixel of movement floods the
// receiver and stalls the GUI (an IR frame alone takes ~70 ms), so changes are rate limited.
#define SLIDER_SEND_INTERVAL_MS 150

static const lv_coord_t col_dsc[] = {
  LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
  LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
  LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
  LV_GRID_TEMPLATE_LAST
};

// ---------------------------------------------------------------------------
// Parsed GUI definitions
// ---------------------------------------------------------------------------
enum DynamicWidgetType : uint8_t { WIDGET_SLIDER, WIDGET_SWITCH, WIDGET_BUTTON, WIDGET_TEXT };

struct DynamicWidget {
  DynamicWidgetType type;
  uint8_t x, y, w, h;
  uint16_t command;     // DYNAMIC_COMMAND_NOT_FOUND for text widgets or unknown commands
  int16_t value;        // slider position / switch state, kept when the tab is recreated
  int16_t sentValue;    // last slider value that was sent
  uint32_t lastSentMs;
  std::string text;     // button label or text content
};

struct DynamicGui {
  std::vector<DynamicWidget> widgets;
  std::vector<lv_coord_t> rowDsc; // LVGL keeps a pointer to this, so it has to live as long as the GUI
};

// key: registered (display) name. std::map keeps the addresses of its elements stable,
// so widgets can be handed to LVGL as event user data.
static std::map<std::string, DynamicGui> dynamicGuis;
// key: internal guiname from guis.json, value: registered (display) name
static std::map<std::string, std::string> guiDisplayNames;

std::string dynamicGuiDisplayName(const std::string& guiname) {
  auto it = guiDisplayNames.find(guiname);
  return (it != guiDisplayNames.end()) ? it->second : guiname;
}

// ---------------------------------------------------------------------------
// LVGL event callbacks for widgets.
// ---------------------------------------------------------------------------

// Slider event: fires continuously as the slider is moved, and once when it is released.
static void slider_event_cb(lv_event_t* e) {
  DynamicWidget* widget = static_cast<DynamicWidget*>(lv_event_get_user_data(e));
  if (!widget) return;

  int value = lv_slider_get_value(lv_event_get_target(e));
  widget->value = value;
  bool released = (lv_event_get_code(e) == LV_EVENT_RELEASED);
  uint32_t now = millis();
  if (value == widget->sentValue) return;
  if (!released && (now - widget->lastSentMs < SLIDER_SEND_INTERVAL_MS)) return;

  widget->sentValue = value;
  widget->lastSentMs = now;
  if (widget->command != DYNAMIC_COMMAND_NOT_FOUND) {
    executeCommand(widget->command, std::to_string(value));
  }
  omote_log_v("Slider value: %d, executed command: %u\r\n", value, widget->command);
}

// Switch event: triggered when the switch toggles.
static void switch_event_cb(lv_event_t* e) {
  DynamicWidget* widget = static_cast<DynamicWidget*>(lv_event_get_user_data(e));
  if (!widget) return;

  bool state = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  widget->value = state ? 1 : 0;
  if (widget->command != DYNAMIC_COMMAND_NOT_FOUND) {
    executeCommand(widget->command, state ? "true" : "false");
  }
  omote_log_v("Switch state: %d, executed command: %u\r\n", state, widget->command);
}

// Generic event callback for clickable buttons.
static void button_event_cb(lv_event_t* e) {
  DynamicWidget* widget = static_cast<DynamicWidget*>(lv_event_get_user_data(e));
  if (!widget || widget->command == DYNAMIC_COMMAND_NOT_FOUND) return;

  executeCommand(widget->command);
  omote_log_v("Button executed command: %u\r\n", widget->command);
}

// ---------------------------------------------------------------------------
// Widget creation helper: creates a widget and places it in the grid of the tab.
// ---------------------------------------------------------------------------
static void create_widget(lv_obj_t* parent, DynamicWidget& widget) {
  lv_obj_t* obj = NULL;

  switch (widget.type) {
    case WIDGET_SLIDER: {
      obj = lv_slider_create(parent);
      lv_slider_set_range(obj, 0, 100);
      lv_obj_set_style_bg_color(obj, lv_color_lighten(lv_palette_main(LV_PALETTE_AMBER), 180), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_KNOB);
      lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN);
      lv_obj_set_style_bg_color(obj, lv_color_lighten(color_primary, 50), LV_PART_MAIN);
      lv_slider_set_value(obj, widget.value, LV_ANIM_OFF);
      widget.sentValue = widget.value;
      lv_obj_add_event_cb(obj, slider_event_cb, LV_EVENT_VALUE_CHANGED, &widget);
      lv_obj_add_event_cb(obj, slider_event_cb, LV_EVENT_RELEASED, &widget);
      break;
    }
    case WIDGET_SWITCH: {
      obj = lv_switch_create(parent);
      if (widget.value > 0) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
      }
      lv_obj_add_event_cb(obj, switch_event_cb, LV_EVENT_VALUE_CHANGED, &widget);
      break;
    }
    case WIDGET_BUTTON: {
      obj = lv_btn_create(parent);
      // Create a label on the button to display its text.
      lv_obj_t* btnLabel = lv_label_create(obj);
      lv_label_set_text(btnLabel, widget.text.c_str());
      lv_obj_center(btnLabel);
      lv_obj_add_event_cb(obj, button_event_cb, LV_EVENT_CLICKED, &widget);
      break;
    }
    case WIDGET_TEXT: {
      // For non-interactive text, create a label to display content (or label).
      obj = lv_label_create(parent);
      lv_label_set_text(obj, widget.text.c_str());
      break;
    }
  }

  lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, widget.x, widget.w, LV_GRID_ALIGN_CENTER, widget.y + 1, widget.h);
}

// ---------------------------------------------------------------------------
// Create the content of a dynamic tab from the parsed GUI definition.
// ---------------------------------------------------------------------------
static void create_tab_content_dynamic(lv_obj_t* tab) {
  std::string guiName = getGuiNameByTab(tab);
  auto it = dynamicGuis.find(guiName);
  if (it == dynamicGuis.end()) {
    omote_log_e("Dynamic GUI '%s' not found\r\n", guiName.c_str());
    return;
  }
  DynamicGui& gui = it->second;

  lv_obj_set_width(tab, SCR_WIDTH);
  lv_obj_set_layout(tab, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(tab, col_dsc, gui.rowDsc.data());
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);

  // Title in its own row, so that it does not overlap the widget placed at (0,0).
  lv_obj_t* menuLabel = lv_label_create(tab);
  lv_label_set_text(menuLabel, guiName.c_str());
  lv_obj_set_grid_cell(menuLabel, LV_GRID_ALIGN_START, 0, GRID_COLUMNS, LV_GRID_ALIGN_CENTER, 0, 1);

  for (DynamicWidget& widget : gui.widgets) {
    create_widget(tab, widget);
  }
}

// Widgets only point to the parsed GUI definition, there is nothing to free.
static void notify_tab_before_delete_dynamic(void) {}

// ---------------------------------------------------------------------------
// Parse "gui_<guiname>.json" into a DynamicGui.
// ---------------------------------------------------------------------------
static int clampInt(int value, int minValue, int maxValue) {
  return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

static void load_gui_definition(const std::string& filePath, DynamicGui& gui) {
  int rows = 0;

  JsonDocument doc;
  if (loadJsonFile(filePath, doc)) {
    JsonArrayConst widgets = doc["widgets"].as<JsonArrayConst>();
    if (widgets.isNull()) {
      omote_log_w("No widgets found in GUI JSON: %s\r\n", filePath.c_str());
    }
    for (JsonObjectConst widgetObj : widgets) {
      const char* type = widgetObj["type"] | "";
      DynamicWidget widget;
      if      (strcmp(type, "slider") == 0) { widget.type = WIDGET_SLIDER; }
      else if (strcmp(type, "switch") == 0) { widget.type = WIDGET_SWITCH; }
      else if (strcmp(type, "button") == 0) { widget.type = WIDGET_BUTTON; }
      else if (strcmp(type, "text")   == 0) { widget.type = WIDGET_TEXT; }
      else {
        omote_log_w("%s: unsupported widget type '%s', skipped\r\n", filePath.c_str(), type);
        continue;
      }

      widget.x = clampInt(widgetObj["x"] | 0, 0, GRID_COLUMNS - 1);
      widget.w = clampInt(widgetObj["w"] | 1, 1, GRID_COLUMNS - widget.x);
      widget.y = clampInt(widgetObj["y"] | 0, 0, 200);
      widget.h = clampInt(widgetObj["h"] | 1, 1, 50);
      widget.value = (widget.type == WIDGET_SLIDER) ? 50 : 0;
      widget.sentValue = widget.value;
      widget.lastSentMs = 0;

      const char* label = widgetObj["label"] | "";
      const char* content = widgetObj["content"] | "";
      widget.text = (widget.type == WIDGET_TEXT && content[0] != '\0') ? content : label;

      widget.command = DYNAMIC_COMMAND_NOT_FOUND;
      if (widget.type != WIDGET_TEXT) {
        const char* device = widgetObj["device"] | "";
        const char* command = widgetObj["command"] | "";
        widget.command = resolveDynamicCommand(device, command);
        if (widget.command == DYNAMIC_COMMAND_NOT_FOUND) {
          omote_log_w("%s: command %s_%s not found, widget will do nothing\r\n", filePath.c_str(), device, command);
        }
      }

      rows = std::max(rows, widget.y + widget.h);
      gui.widgets.push_back(widget);
    }
  }

  // one row for the title, then the widget rows
  gui.rowDsc.assign(rows + 1, GRID_ROW_HEIGHT);
  gui.rowDsc.push_back(LV_GRID_TEMPLATE_LAST);
}

// ---------------------------------------------------------------------------
// Register dynamic GUIs by reading the master guis.json file.
// Each entry in the master file should have "name" and "guiname".
// ---------------------------------------------------------------------------
void register_dynamic_guis() {
  JsonDocument doc;
  if (!loadJsonFile("guis.json", doc)) {
    return;
  }

  JsonArrayConst guis = doc.as<JsonArrayConst>();
  if (guis.isNull()) {
    omote_log_e("Invalid master GUI JSON format. Expected an array.\r\n");
    return;
  }

  for (JsonObjectConst guiEntry : guis) {
    std::string name    = guiEntry["name"]    | "";
    std::string guiName = guiEntry["guiname"] | "";
    if (name.empty() || guiName.empty()) {
      omote_log_e("Invalid GUI entry in master file. Missing 'name' or 'guiname'.\r\n");
      continue;
    }
    if (registered_guis_byName_map.count(name) > 0) {
      omote_log_e("A GUI named '%s' already exists, skipped\r\n", name.c_str());
      continue;
    }

    DynamicGui& gui = dynamicGuis[name];
    load_gui_definition("gui_" + guiName + ".json", gui);
    guiDisplayNames[guiName] = name;

    register_gui(name, create_tab_content_dynamic, notify_tab_before_delete_dynamic);
    omote_log_i("Registered GUI '%s' with %u widgets\r\n", name.c_str(), gui.widgets.size());
  }
}
