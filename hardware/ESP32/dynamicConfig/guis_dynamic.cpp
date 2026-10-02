/*
 * Omote Dynamic GUI Module
 *
 * This file implements the loading and creation of dynamic GUI tabs.
 *
 * It performs the following:
 *  - Reads the master guis.json file (located at the SPIFFS root) which contains
 *    a list of GUIs (each with "name" and "guiname").
 *  - For each GUI, loads the individual JSON file (named "gui_[guiname].json")
 *    which defines a list of widgets.
 *  - Creates the widgets in a 12-unit grid layout (using x, y, w, h from the JSON)
 *    and attaches an event callback that calls executeCommand.
 *
 * Widgets supported in this example:
 *    - slider: A slider that executes its command continuously as the slider is moved,
 *              passing the slider value as a string parameter.
 *    - switch: A switch that executes its command on toggle (passing "1" or "0").
 *    - text:   A label widget that displays text (non-interactive).
 *    - button: A clickable button with a label that executes its command when clicked.
 *
 * Memory cleanup is handled via the notify_tab_before_delete callback,
 * which recursively frees any allocated WidgetUserData.
 */

 #include <Arduino.h>
 #include <FS.h>
 #include <SPIFFS.h>
 #include <ArduinoJson.h>
 #include <lvgl.h>
 #include <string>
 #include <vector>
 #include <map>
 #include <cstdlib>    // for std::stoi
 
 #include "applicationInternal/commandHandler.h" // Provides executeCommand()
 #include "applicationInternal/gui/guiBase.h"
 #include "applicationInternal/gui/guiRegistry.h"
 #include "applicationInternal/omote_log.h"
 #include "applicationInternal/gui/guiMemoryOptimizer.h"
 #include "applicationInternal/scenes/sceneRegistry.h"
 #include "scenes_dynamic.h"
 
 // ---------------------------------------------------------------------------
 // Grid configuration – a 12-unit grid is used.
 // ---------------------------------------------------------------------------
 #define GRID_TOTAL_UNITS 12
 #define DEFAULT_UNIT_HEIGHT 20  // pixel height per grid unit; adjust as needed
 
 // ---------------------------------------------------------------------------
 // Data structures for widget configuration and user data.
 // ---------------------------------------------------------------------------
 struct WidgetConfig {
     std::string type;
     std::string label;
     std::string id;
     std::string device;
     std::string command;  // In the JSON this is a string; we convert it to a number.
     int x;
     int y;
     int w;
     int h;
     std::string content; // For text widgets (optional)
 };
 
 // Now store the command as a number.
 struct WidgetUserData {
     uint16_t command;
     std::string widgetType; // e.g., "slider", "switch", "button"
 };
 
 // ---------------------------------------------------------------------------
 // File reading helper (using SPIFFS).
 // ---------------------------------------------------------------------------
 String readGUIFileContent(const char* path) {
     File file = SPIFFS.open(path, "r");
     if (!file) {
         omote_log_e("Failed to open file: %s\r\n", path);
         return "";
     }
     String content = file.readString();
     file.close();
     return content;
 }
 
 // ---------------------------------------------------------------------------
 // LVGL event callbacks for widgets.
 // ---------------------------------------------------------------------------
 
 // Slider event: fires continuously as the slider is moved.
 static void slider_event_cb(lv_event_t* e) {
     lv_obj_t* slider = lv_event_get_target(e);
     WidgetUserData* data = static_cast<WidgetUserData*>(lv_event_get_user_data(e));
     if (!data) return;
     
     int value = lv_slider_get_value(slider);
     // Execute the command with the slider value as a parameter.
     executeCommand(data->command, std::to_string(value));
     omote_log_v(F("Slider value: %d, executed command: %u\r\n"), value, data->command);
 }
 
 // Switch event: triggered when the switch toggles.
 static void switch_event_cb(lv_event_t* e) {
     lv_obj_t* sw = lv_event_get_target(e);
     WidgetUserData* data = static_cast<WidgetUserData*>(lv_event_get_user_data(e));
     if (!data) return;
     
     bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
     executeCommand(data->command, state ? "true" : "false");
     omote_log_v(F("Switch state: %d, executed command: %u\r\n"), state, data->command);
 }
 
 // Generic event callback for clickable buttons.
 static void generic_event_cb(lv_event_t* e) {
     WidgetUserData* data = static_cast<WidgetUserData*>(lv_event_get_user_data(e));
     if (!data) return;
     
     executeCommand(data->command);
     omote_log_v(F("Button executed command: %u\r\n"), data->command);
 }
 
 // ---------------------------------------------------------------------------
 // Widget creation helper: creates a widget based on WidgetConfig and places it
 // in the given parent container using a grid layout.
 // ---------------------------------------------------------------------------
 static void create_widget(lv_obj_t* parent, const WidgetConfig& cfg) {
     // Calculate pixel positions and sizes based on parent's width and grid units.
     
     lv_obj_t* obj = NULL;
     const char* device = cfg.device.c_str();
     const char* comd = cfg.command.c_str();
     
     // Convert the command string (if provided) to a uint16_t.
     uint16_t cmd = getCommandValue(device, comd);
     if (strcmp(device, "BLE") == 0) {
         cmd = getBLECommandValue(comd);
     }
     
     if (cmd == 0) {
         // Note: In production you might want to add error checking.
         // cmd = static_cast<uint16_t>(std::stoi(cfg.command));
     }
     
     if (cfg.type == "slider") {
         obj = lv_slider_create(parent);
         lv_slider_set_range(obj, 0, 100);
         lv_obj_set_style_bg_color(obj, lv_color_lighten(lv_palette_main(LV_PALETTE_AMBER), 180), LV_PART_INDICATOR);
         lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_KNOB);
         lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN);
         lv_obj_set_style_bg_color(obj, lv_color_lighten(color_primary, 50), LV_PART_MAIN);
         lv_slider_set_value(obj, 50, LV_ANIM_OFF);
         WidgetUserData* data = new WidgetUserData{ cmd, cfg.type };
         lv_obj_set_user_data(obj, data);
         lv_obj_add_event_cb(obj, slider_event_cb, LV_EVENT_VALUE_CHANGED, data);
     }
     else if (cfg.type == "switch") {
         obj = lv_switch_create(parent);
         WidgetUserData* data = new WidgetUserData{ cmd, cfg.type };
         lv_obj_set_user_data(obj, data);
         lv_obj_add_event_cb(obj, switch_event_cb, LV_EVENT_VALUE_CHANGED, data);
     }
     else if (cfg.type == "button") {
         obj = lv_btn_create(parent);
         // Create a label on the button to display its text.
         lv_obj_t* btnLabel = lv_label_create(obj);
         lv_label_set_text(btnLabel, cfg.label.c_str());
         lv_obj_center(btnLabel);
         WidgetUserData* data = new WidgetUserData{ cmd, cfg.type };
         lv_obj_set_user_data(obj, data);
         lv_obj_add_event_cb(obj, generic_event_cb, LV_EVENT_CLICKED, data);
     }
     else if (cfg.type == "text") {
         // For non-interactive text, create a label to display content (or label).
         obj = lv_label_create(parent);
         const char* txt = cfg.content.empty() ? cfg.label.c_str() : cfg.content.c_str();
         lv_label_set_text(obj, txt);
     }
     else {
         omote_log_e("Unknown widget type: %s\r\n", cfg.type.c_str());
         return;
     }
     
     lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, cfg.x, cfg.w, LV_GRID_ALIGN_CENTER, cfg.y, cfg.h);
 }
 
 // ---------------------------------------------------------------------------
 // Create dynamic tab content by reading the individual GUI JSON file.
 // ---------------------------------------------------------------------------
 static void create_tab_content_dynamic(lv_obj_t* tab, const std::string& guiName) {
     // Construct file path: "/gui_[guiName].json"
     std::string filePath = "/gui_" + guiName + ".json";
     String content = readGUIFileContent(filePath.c_str());
     if (content == "") {
         omote_log_e("GUI file not found or empty: %s\r\n", filePath.c_str());
         return;
     }
     
     // Suppress deprecation warnings for JsonDocument.
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
     JsonDocument doc;
 #pragma GCC diagnostic pop
     
     DeserializationError error = deserializeJson(doc, content);
     if (error) {
         omote_log_e("Failed to parse GUI JSON (%s): %s\r\n", filePath.c_str(), error.f_str());
         return;
     }
     
     JsonArray widgets = doc["widgets"].as<JsonArray>();
     if (widgets.isNull()) {
         omote_log_e("No widgets found in GUI JSON: %s\r\n", filePath.c_str());
         return;
     }
     
     // Store grid descriptors in flash.
     static const lv_coord_t col_dsc[] PROGMEM = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
     };
     
     static const lv_coord_t row_dsc[] PROGMEM = {20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, LV_GRID_TEMPLATE_LAST};
     
     // Add content to the dynamic tab.
     lv_obj_set_width(tab, SCR_WIDTH);
     lv_obj_set_layout(tab, LV_LAYOUT_GRID);
     lv_obj_set_grid_dsc_array(tab, col_dsc, row_dsc);
     lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);
     
     // Add a label, then a box for the light controls.
     lv_obj_t* menuLabel = lv_label_create(tab);
     lv_label_set_text(menuLabel, guiName.c_str());
     
     // Create each widget defined in the JSON.
     for (JsonObject widgetObj : widgets) {
         WidgetConfig cfg;
         cfg.type    = widgetObj["type"]   | "";
         cfg.label   = widgetObj["label"]  | "";
         cfg.id      = widgetObj["id"]     | "";
         cfg.device  = widgetObj["device"] | "";
         cfg.command = widgetObj["command"]| "";
         cfg.x       = widgetObj["x"]      | 0;
         cfg.y       = widgetObj["y"]      | 0;
         cfg.w       = widgetObj["w"]      | 1;
         cfg.h       = widgetObj["h"]      | 1;
         cfg.content = widgetObj["content"]| "";
         
         create_widget(tab, cfg);
     }
     widgets.clear();
     content = "";
 }
 
 // ---------------------------------------------------------------------------
 // Recursive helper to free user data stored in LVGL objects.
 // ---------------------------------------------------------------------------
 static void free_user_data_recursive(lv_obj_t* obj) {
     if (!obj) return;
     
     // Free user data if present.
     void* ud = lv_obj_get_user_data(obj);
     if (ud != NULL) {
         delete static_cast<WidgetUserData*>(ud);
         lv_obj_set_user_data(obj, NULL);
     }
     
     // Iterate over children using child count.
     uint32_t child_cnt = lv_obj_get_child_cnt(obj);
     for (uint32_t i = 0; i < child_cnt; i++) {
         lv_obj_t* child = lv_obj_get_child(obj, i);
         free_user_data_recursive(child);
     }
 }
 
 // ---------------------------------------------------------------------------
 // Global pointer to the current dynamic GUI tab being deleted.
 // This should be set appropriately in your GUI manager before deletion.
 // ---------------------------------------------------------------------------
 lv_obj_t* currentDynamicTab = nullptr;
 
 void set_current_dynamic_tab(lv_obj_t* curDynTab) {
     currentDynamicTab = curDynTab;
 }
 
 // ---------------------------------------------------------------------------
 // Cleanup callback to be called before a dynamic GUI tab is deleted.
 // This version has no parameters to match the expected signature.
 // It uses the global pointer 'currentDynamicTab' to perform cleanup.
 // ---------------------------------------------------------------------------
 void notify_tab_before_delete_dynamic() {
     if (currentDynamicTab != nullptr) {
         free_user_data_recursive(currentDynamicTab);
     }
 }
 
 // ---------------------------------------------------------------------------
 // Global mapping from registered GUI name to its dynamic file name.
 // This is used by the non-capturing callback below.
 // ---------------------------------------------------------------------------
 static std::map<std::string, std::string> dynamicGuiFileMap;
 
 // ---------------------------------------------------------------------------
 // Static create_tab_content callback for dynamic GUIs.
 // This function does not capture any variables. It expects that the GUI tab’s
 // user data has been set to a pointer to a std::string containing the registered GUI name.
 // Using that name, it looks up the associated dynamic file name in dynamicGuiFileMap
 // and then calls create_tab_content_dynamic.
 // ---------------------------------------------------------------------------
 static void create_tab_content_dynamic_wrapper(lv_obj_t* tab) {
     // Suppose you have a way (for example, a custom property or an associated id)
     // to determine the registered name for this tab.
     // For example, if the tab’s “id” (or some property) is set to the registered name:
     std::string guiName = "";
     
     guiName = getGuiNameByTab(tab);
     
     if (guiName != "") {
         std::string fileName = dynamicGuiFileMap[guiName];
         create_tab_content_dynamic(tab, fileName);
     } else {
         omote_log_e("Tab Not Found");
     }
 }
 
 // ---------------------------------------------------------------------------
 // Register dynamic GUIs by reading the master guis.json file from SPIFFS.
 // Each entry in the master file should have "name" and "guiname".
 // ---------------------------------------------------------------------------
 void register_dynamic_guis() {
     const char* masterFilePath = "/guis.json";
     String masterContent = readGUIFileContent(masterFilePath);
     if (masterContent == "") {
         omote_log_e("Master GUI file not found or empty: %s\r\n", masterFilePath);
         return;
     }
     
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
     JsonDocument doc;
 #pragma GCC diagnostic pop
     
     DeserializationError error = deserializeJson(doc, masterContent);
     if (error) {
         omote_log_e("Failed to parse master GUI JSON: %s\r\n", error.f_str());
         return;
     }
     
     JsonArray guis = doc.as<JsonArray>();
     if (guis.isNull()) {
         omote_log_e("Invalid master GUI JSON format. Expected an array.\r\n");
         return;
     }
     
     // For each GUI entry, register it so that its tab content is created dynamically.
     for (JsonObject guiEntry : guis) {
         std::string name    = guiEntry["name"]   | "";
         std::string guiName = guiEntry["guiname"]| "";
         if (name.empty() || guiName.empty()) {
             omote_log_e("Invalid GUI entry in master file. Missing 'name' or 'guiname'.\r\n");
             continue;
         }
         
         // Store the mapping from the registered name to the dynamic file name.
         dynamicGuiFileMap[name] = guiName;
         
         // IMPORTANT: The create_tab_content callback below is non-capturing.
         // It expects that when the tab is created the tab's user data has been set to
         // a pointer to a std::string containing the registered GUI name.
         register_gui(name, create_tab_content_dynamic_wrapper, notify_tab_before_delete_dynamic,
                      nullptr, nullptr, nullptr, nullptr);
     }
     guis.clear();
     doc.clear();
 }
 