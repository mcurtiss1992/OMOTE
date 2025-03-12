#ifndef GUIS_DYNAMIC_H
#define GUIS_DYNAMIC_H

#include <lvgl.h>

/**
 * @brief Registers dynamic GUIs by reading the master guis.json file from SPIFFS.
 *
 * The master file should list all GUIs with "name" and "guiname", and each individual GUI
 * is loaded from a file named "gui_[guiname].json". The GUI tab content is created dynamically
 * based on the JSON configuration.
 */
void register_dynamic_guis();

void set_current_dynamic_tab(lv_obj_t* curDynTab);
/**
 * @brief Callback to clean up allocated widget user data before a dynamic GUI tab is deleted.
 *
 * This function recursively frees any memory allocated for widget event data associated
 * with the tab. It is intended to be registered as the notify_tab_before_delete callback
 * for dynamic GUIs.
 *
 * @param tab_ptr Pointer to the LVGL object representing the tab.
 */
void notify_tab_before_delete_dynamic(void* tab_ptr);

#endif // GUIS_DYNAMIC_H
