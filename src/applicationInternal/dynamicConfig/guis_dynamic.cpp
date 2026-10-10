/*
 * Omote Dynamic GUI Module
 *
 * This file implements the loading and creation of dynamic GUI tabs.
 *
 * It performs the following:
 *  - Reads the master guis.json file (from the config storage) which contains
 *    a list of GUIs (each with "name" and "guiname").
 *  - Only the GUI names are kept in RAM. The individual JSON file ("gui_[guiname].json")
 *    that defines the widgets is parsed when its tab is created, and the parsed copy is
 *    dropped again as soon as the GUI is no longer one of the previous/current/next tabs
 *    (see dynamic_guis_trimCache()). Slider and switch positions of dropped GUIs are
 *    kept in a few bytes so they survive.
 *  - Draws all widgets of a GUI with a single LVGL object on a 12-unit grid (using x, y, w, h
 *    from the JSON), instead of one or two LVGL objects per widget. Look and animations follow
 *    the LVGL default theme. Touches are mapped to the widgets and call executeCommand.
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
  // drawing state, only valid while the GUI has a tab
  uint8_t pressAnim;    // 0..255, pressed look of buttons
  uint8_t switchAnim;   // 0..255, knob position of switches
  lv_obj_t* canvas;     // the widget that draws this GUI, for invalidating on animation
};

struct DynamicGui {
  std::vector<DynamicWidget> widgets;
  int rows;              // widget rows, without the title row
};

// Cache of parsed GUIs, only the ones that currently have a tab (previous, current, next).
// key: registered (display) name. std::map keeps the addresses of its elements stable,
// so widgets can be handed to LVGL as event user data.
static std::map<std::string, DynamicGui> dynamicGuis;
// key: registered (display) name, value: internal guiname from guis.json (the file is "gui_<guiname>.json")
static std::map<std::string, std::string> guiFileNames;
// key: internal guiname from guis.json, value: registered (display) name
static std::map<std::string, std::string> guiDisplayNames;
// Slider/switch values of GUIs that were dropped from the cache. Only stored for GUIs whose
// values were changed, one int16 per slider/switch in file order.
static std::map<std::string, std::vector<int16_t>> savedValues;

std::string dynamicGuiDisplayName(const std::string& guiname) {
  auto it = guiDisplayNames.find(guiname);
  return (it != guiDisplayNames.end()) ? it->second : guiname;
}

// ---------------------------------------------------------------------------
// Drawing. All widgets of a GUI are drawn by one LVGL object (the "canvas") instead of one
// or two objects per widget. That saves most of the LVGL pool a GUI used to need.
// Look and animations follow the LVGL default theme: sizes and colors are measured once
// from real LVGL widgets.
// ---------------------------------------------------------------------------
struct ThemeLook {
  bool measured = false;
  lv_coord_t padRow, padColumn;
  lv_coord_t buttonHeight, buttonRadius;
  lv_color_t buttonColor;
  lv_coord_t sliderHeight, knobPad;
  lv_color_t sliderMainColor, sliderIndicatorColor, knobColor;
  lv_coord_t switchWidth, switchHeight, switchKnobPad;
  lv_color_t switchOffColor, switchOnColor;
  const lv_font_t* font;
  lv_color_t textColor;
};
static ThemeLook look;

static void measureThemeLook(lv_obj_t* tab) {
  if (look.measured) return;
  look.measured = true;
  look.padRow    = lv_obj_get_style_pad_row(tab, LV_PART_MAIN);
  look.padColumn = lv_obj_get_style_pad_column(tab, LV_PART_MAIN);
  look.font      = lv_obj_get_style_text_font(tab, LV_PART_MAIN);
  look.textColor = lv_obj_get_style_text_color(tab, LV_PART_MAIN);

  lv_obj_t* btn = lv_btn_create(tab);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, "Ag");
  lv_obj_update_layout(btn);
  look.buttonHeight = lv_obj_get_height(btn);
  look.buttonRadius = lv_obj_get_style_radius(btn, LV_PART_MAIN);
  look.buttonColor  = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
  lv_obj_del(btn);

  lv_obj_t* slider = lv_slider_create(tab);
  lv_obj_update_layout(slider);
  look.sliderHeight = lv_obj_get_height(slider);
  look.knobPad = lv_obj_get_style_pad_top(slider, LV_PART_KNOB);
  lv_obj_del(slider);
  look.knobColor = lv_color_white();
  // same colors as the LVGL sliders had in this GUI
  look.sliderMainColor = lv_color_lighten(color_primary, 50);
  look.sliderIndicatorColor = lv_color_lighten(lv_palette_main(LV_PALETTE_AMBER), 180);

  lv_obj_t* sw = lv_switch_create(tab);
  lv_obj_update_layout(sw);
  look.switchWidth  = lv_obj_get_width(sw);
  look.switchHeight = lv_obj_get_height(sw);
  look.switchKnobPad = lv_obj_get_style_pad_top(sw, LV_PART_KNOB);
  look.switchOffColor = lv_obj_get_style_bg_color(sw, LV_PART_MAIN);
  // the checked color of the default theme
  look.switchOnColor = lv_theme_get_color_primary(sw);
  lv_obj_del(sw);
}

// grid geometry of a canvas
static lv_coord_t columnWidth(lv_obj_t* canvas) {
  return (lv_obj_get_content_width(canvas) - (GRID_COLUMNS - 1) * look.padColumn) / GRID_COLUMNS;
}

// Cell of a widget in canvas coordinates. Row 0 is the title.
static void widgetCell(lv_obj_t* canvas, const DynamicWidget& w, lv_area_t* cell) {
  lv_coord_t colW = columnWidth(canvas);
  lv_area_t coords;
  lv_obj_get_content_coords(canvas, &coords);
  cell->x1 = coords.x1 + w.x * (colW + look.padColumn);
  cell->x2 = cell->x1 + w.w * colW + (w.w - 1) * look.padColumn - 1;
  cell->y1 = coords.y1 + (w.y + 1) * (GRID_ROW_HEIGHT + look.padRow);
  cell->y2 = cell->y1 + w.h * GRID_ROW_HEIGHT + (w.h - 1) * look.padRow - 1;
}

// Area that is drawn and touchable for a widget: stretched horizontally, centered vertically in its cell
static void widgetArea(lv_obj_t* canvas, const DynamicWidget& w, lv_area_t* area) {
  widgetCell(canvas, w, area);
  lv_coord_t height;
  switch (w.type) {
    case WIDGET_BUTTON: height = look.buttonHeight; break;
    case WIDGET_SLIDER: height = look.sliderHeight + 2 * look.knobPad; break;
    case WIDGET_SWITCH: height = look.switchHeight; break;
    default:            height = lv_font_get_line_height(look.font); break;
  }
  lv_coord_t center = (area->y1 + area->y2) / 2;
  area->y1 = center - height / 2;
  area->y2 = area->y1 + height - 1;
}

static void drawWidget(lv_draw_ctx_t* draw_ctx, lv_obj_t* canvas, const DynamicWidget& w) {
  lv_area_t area;
  widgetArea(canvas, w, &area);
  if (!_lv_area_is_on(&area, draw_ctx->clip_area)) return;

  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.font = look.font;
  label.color = look.textColor;

  switch (w.type) {
    case WIDGET_TEXT: {
      lv_draw_label(draw_ctx, &label, &area, w.text.c_str(), NULL);
      break;
    }
    case WIDGET_BUTTON: {
      // pressed: darker, like the default theme
      rect.radius = look.buttonRadius;
      rect.bg_color = lv_color_mix(lv_color_darken(look.buttonColor, LV_OPA_30), look.buttonColor, w.pressAnim);
      lv_draw_rect(draw_ctx, &rect, &area);
      lv_point_t size;
      lv_txt_get_size(&size, w.text.c_str(), label.font, 0, 0, lv_area_get_width(&area), LV_TEXT_FLAG_NONE);
      lv_area_t textArea = area;
      textArea.y1 += (lv_area_get_height(&area) - size.y) / 2;
      label.align = LV_TEXT_ALIGN_CENTER;
      label.color = lv_color_white();
      lv_draw_label(draw_ctx, &label, &textArea, w.text.c_str(), NULL);
      break;
    }
    case WIDGET_SLIDER: {
      // track, the indicator up to the value, and the knob
      // like LVGL: the track uses the full width, the knob may stick out at the ends
      lv_area_t track = area;
      track.y1 += look.knobPad;
      track.y2 -= look.knobPad;
      rect.radius = LV_RADIUS_CIRCLE;
      rect.bg_color = look.sliderMainColor;
      lv_draw_rect(draw_ctx, &rect, &track);
      lv_coord_t knobX = track.x1 + (lv_area_get_width(&track) - 1) * w.value / 100;
      lv_area_t indicator = track;
      indicator.x2 = knobX;
      rect.bg_color = look.sliderIndicatorColor;
      lv_draw_rect(draw_ctx, &rect, &indicator);
      lv_coord_t knobRadius = lv_area_get_height(&area) / 2;
      lv_area_t knob = {(lv_coord_t)(knobX - knobRadius), area.y1, (lv_coord_t)(knobX + knobRadius), area.y2};
      rect.bg_color = look.knobColor;
      lv_draw_rect(draw_ctx, &rect, &knob);
      break;
    }
    case WIDGET_SWITCH: {
      // the switch keeps its LVGL size, left aligned in its cell
      lv_area_t track = area;
      track.x2 = LV_MIN(area.x2, area.x1 + look.switchWidth - 1);
      rect.radius = LV_RADIUS_CIRCLE;
      rect.bg_color = lv_color_mix(look.switchOnColor, look.switchOffColor, w.switchAnim);
      lv_draw_rect(draw_ctx, &rect, &track);
      // like lv_switch: a square knob of the track height that travels along the track,
      // grown by the knob padding (the default theme uses a negative one, so it sits inside)
      lv_coord_t base = lv_area_get_height(&track);
      lv_area_t knob;
      knob.x1 = track.x1 + (lv_area_get_width(&track) - base) * w.switchAnim / 255;
      knob.y1 = track.y1;
      knob.x2 = knob.x1 + base - 1;
      knob.y2 = knob.y1 + base - 1;
      lv_area_increase(&knob, look.switchKnobPad, look.switchKnobPad);
      rect.bg_color = lv_color_white();
      lv_draw_rect(draw_ctx, &rect, &knob);
      break;
    }
  }
}

static void canvas_draw_event_cb(lv_event_t* e) {
  DynamicGui* gui = static_cast<DynamicGui*>(lv_event_get_user_data(e));
  lv_obj_t* canvas = lv_event_get_target(e);
  lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
  for (const DynamicWidget& w : gui->widgets) {
    drawWidget(draw_ctx, canvas, w);
  }
}

static void invalidateWidget(const DynamicWidget& w) {
  if (w.canvas == NULL) return;
  lv_area_t area;
  widgetArea(w.canvas, w, &area);
  // the slider knob sticks out at the ends of the track
  lv_area_increase(&area, lv_area_get_height(&area) / 2 + 2, 2);
  lv_obj_invalidate_area(w.canvas, &area);
}

// --- animations ---------------------------------------------------------------
static void pressAnim_cb(void* var, int32_t value) {
  DynamicWidget* w = static_cast<DynamicWidget*>(var);
  w->pressAnim = value;
  invalidateWidget(*w);
}

static void switchAnim_cb(void* var, int32_t value) {
  DynamicWidget* w = static_cast<DynamicWidget*>(var);
  w->switchAnim = value;
  invalidateWidget(*w);
}

static void animate(DynamicWidget& w, lv_anim_exec_xcb_t exec_cb, int32_t from, int32_t to, uint32_t time) {
  lv_anim_del(&w, exec_cb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &w);
  lv_anim_set_exec_cb(&a, exec_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, time);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

// --- input ------------------------------------------------------------------------
static void sendSliderValue(DynamicWidget& w, bool released) {
  uint32_t now = millis();
  if (w.value == w.sentValue) return;
  if (!released && (now - w.lastSentMs < SLIDER_SEND_INTERVAL_MS)) return;
  w.sentValue = w.value;
  w.lastSentMs = now;
  if (w.command != DYNAMIC_COMMAND_NOT_FOUND) {
    executeCommand(w.command, std::to_string(w.value));
  }
  omote_log_v("Slider value: %d, executed command: %u\r\n", w.value, w.command);
}

static DynamicWidget* widgetAtPoint(DynamicGui* gui, lv_obj_t* canvas, const lv_point_t& point) {
  for (DynamicWidget& w : gui->widgets) {
    if (w.type == WIDGET_TEXT) continue;
    lv_area_t area;
    widgetArea(canvas, w, &area);
    if (w.type == WIDGET_SWITCH) area.x2 = LV_MIN(area.x2, area.x1 + look.switchWidth - 1);
    if (_lv_area_is_point_on(&area, &point, 0)) return &w;
  }
  return NULL;
}

static void setSliderFromPoint(DynamicWidget& w, lv_obj_t* canvas, const lv_point_t& point) {
  lv_area_t area;
  widgetArea(canvas, w, &area);
  lv_coord_t x1 = area.x1;
  lv_coord_t width = lv_area_get_width(&area);
  int value = (width > 1) ? (point.x - x1) * 100 / (width - 1) : 0;
  value = value < 0 ? 0 : (value > 100 ? 100 : value);
  if (value != w.value) {
    w.value = value;
    invalidateWidget(w);
  }
}

static DynamicWidget* activeWidget = NULL;

static void canvas_input_event_cb(lv_event_t* e) {
  DynamicGui* gui = static_cast<DynamicGui*>(lv_event_get_user_data(e));
  lv_obj_t* canvas = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t point = {0, 0};
  if (indev != NULL) lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_PRESSED) {
    activeWidget = widgetAtPoint(gui, canvas, point);
    if (activeWidget == NULL) return;
    if (activeWidget->type == WIDGET_BUTTON) {
      animate(*activeWidget, pressAnim_cb, activeWidget->pressAnim, 255, 80);
    } else if (activeWidget->type == WIDGET_SLIDER) {
      // like an LVGL slider: while it is dragged, neither the tab nor the tabview scroll
      lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLL_CHAIN);
      setSliderFromPoint(*activeWidget, canvas, point);
      sendSliderValue(*activeWidget, false);
    }
  } else if (code == LV_EVENT_PRESSING) {
    if ((activeWidget != NULL) && (activeWidget->type == WIDGET_SLIDER)) {
      setSliderFromPoint(*activeWidget, canvas, point);
      sendSliderValue(*activeWidget, false);
    }
  } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
    if (activeWidget == NULL) return;
    if (activeWidget->type == WIDGET_BUTTON) {
      animate(*activeWidget, pressAnim_cb, activeWidget->pressAnim, 0, 150);
    } else if (activeWidget->type == WIDGET_SLIDER) {
      sendSliderValue(*activeWidget, true);
      lv_obj_add_flag(canvas, LV_OBJ_FLAG_SCROLL_CHAIN);
    }
    if (code == LV_EVENT_PRESS_LOST) activeWidget = NULL;
  } else if (code == LV_EVENT_CLICKED) {
    DynamicWidget* w = activeWidget;
    activeWidget = NULL;
    // a swipe that started on a widget is not a click
    if ((w == NULL) || ((indev != NULL) && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE))) return;
    if (widgetAtPoint(gui, canvas, point) != w) return;
    if (w->type == WIDGET_BUTTON) {
      if (w->command != DYNAMIC_COMMAND_NOT_FOUND) {
        executeCommand(w->command);
      }
      omote_log_v("Button executed command: %u\r\n", w->command);
    } else if (w->type == WIDGET_SWITCH) {
      w->value = w->value ? 0 : 1;
      animate(*w, switchAnim_cb, w->switchAnim, w->value ? 255 : 0, 150);
      if (w->command != DYNAMIC_COMMAND_NOT_FOUND) {
        executeCommand(w->command, w->value ? "true" : "false");
      }
      omote_log_v("Switch state: %d, executed command: %u\r\n", w->value, w->command);
    }
  } else if (code == LV_EVENT_DELETE) {
    // stop animations, they point to the widgets
    for (DynamicWidget& w : gui->widgets) {
      lv_anim_del(&w, NULL);
      w.canvas = NULL;
    }
    activeWidget = NULL;
  }
}

// ---------------------------------------------------------------------------
// Cache handling
// ---------------------------------------------------------------------------
static void load_gui_definition(const std::string& filePath, DynamicGui& gui);

static bool widgetHoldsValue(const DynamicWidget& w) { return w.type == WIDGET_SLIDER || w.type == WIDGET_SWITCH; }
static int16_t defaultValue(const DynamicWidget& w) { return (w.type == WIDGET_SLIDER) ? 50 : 0; }

static DynamicGui* get_dynamic_gui(const std::string& name) {
  auto cached = dynamicGuis.find(name);
  if (cached != dynamicGuis.end()) {
    return &cached->second;
  }
  auto file = guiFileNames.find(name);
  if (file == guiFileNames.end()) {
    return NULL;
  }

  DynamicGui& gui = dynamicGuis[name];
  load_gui_definition("gui_" + file->second + ".json", gui);

  auto saved = savedValues.find(name);
  if (saved != savedValues.end()) {
    size_t i = 0;
    for (DynamicWidget& w : gui.widgets) {
      if (!widgetHoldsValue(w)) continue;
      if (i < saved->second.size()) {
        w.value = saved->second[i];
        w.sentValue = w.value;
      }
      i++;
    }
    savedValues.erase(saved);
  }
  omote_log_d("Loaded GUI '%s' (%u widgets), %u GUIs cached\r\n", name.c_str(), gui.widgets.size(), dynamicGuis.size());
  return &gui;
}

void dynamic_guis_trimCache() {
  for (auto it = dynamicGuis.begin(); it != dynamicGuis.end();) {
    if (gui_memoryOptimizer_isGUInameInMemory(it->first)) {
      ++it;
      continue;
    }

    bool changed = false;
    std::vector<int16_t> values;
    for (const DynamicWidget& w : it->second.widgets) {
      if (!widgetHoldsValue(w)) continue;
      values.push_back(w.value);
      changed = changed || (w.value != defaultValue(w));
    }
    if (changed) {
      savedValues[it->first] = std::move(values);
    } else {
      savedValues.erase(it->first);
    }
    omote_log_d("Dropped GUI '%s' from the cache\r\n", it->first.c_str());
    it = dynamicGuis.erase(it);
  }
}

// ---------------------------------------------------------------------------
// Create the content of a dynamic tab from the parsed GUI definition.
// ---------------------------------------------------------------------------
static void create_tab_content_dynamic(lv_obj_t* tab) {
  std::string guiName = getGuiNameByTab(tab);
  DynamicGui* guiPtr = get_dynamic_gui(guiName);
  if (guiPtr == NULL) {
    omote_log_e("Dynamic GUI '%s' not found\r\n", guiName.c_str());
    return;
  }
  DynamicGui& gui = *guiPtr;

  lv_obj_set_width(tab, SCR_WIDTH);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);
  measureThemeLook(tab);

  // Title in its own row, so that it does not overlap the widget placed at (0,0).
  lv_obj_t* menuLabel = lv_label_create(tab);
  lv_label_set_text(menuLabel, guiName.c_str());
  lv_obj_set_pos(menuLabel, 0, (GRID_ROW_HEIGHT - lv_font_get_line_height(look.font)) / 2);

  // one object draws all widgets
  lv_obj_t* canvas = lv_obj_create(tab);
  lv_obj_remove_style_all(canvas);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_set_size(canvas, lv_pct(100), (gui.rows + 1) * (GRID_ROW_HEIGHT + look.padRow));
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  // the slider knobs stick out of the grid cells a bit
  lv_obj_set_style_pad_all(canvas, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(canvas, canvas_draw_event_cb, LV_EVENT_DRAW_MAIN, &gui);
  lv_obj_add_event_cb(canvas, canvas_input_event_cb, LV_EVENT_ALL, &gui);

  for (DynamicWidget& widget : gui.widgets) {
    widget.canvas = canvas;
    widget.pressAnim = 0;
    widget.switchAnim = (widget.type == WIDGET_SWITCH && widget.value > 0) ? 255 : 0;
  }
}

// The parsed definition stays cached until dynamic_guis_trimCache() finds it is not needed any more.
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
      widget.pressAnim = 0;
      widget.switchAnim = 0;
      widget.canvas = NULL;

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

  gui.rows = rows;
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

    guiFileNames[name] = guiName;
    guiDisplayNames[guiName] = name;

    register_gui(name, create_tab_content_dynamic, notify_tab_before_delete_dynamic);
    omote_log_i("Registered GUI '%s' (loaded on demand)\r\n", name.c_str());
  }
}
