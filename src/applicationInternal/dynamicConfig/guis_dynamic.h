#ifndef GUIS_DYNAMIC_H
#define GUIS_DYNAMIC_H

#include <string>

/**
 * @brief Registers dynamic GUIs by reading the master guis.json file.
 *
 * The master file should list all GUIs with "name" and "guiname", and each individual GUI
 * is loaded from a file named "gui_[guiname].json" when its tab is created. Only the GUIs
 * of the previous, current and next tab are kept parsed in RAM.
 * Has to be called after the dynamic devices have been registered.
 */
void register_dynamic_guis();

/**
 * @brief Returns the name a dynamic GUI was registered with (its display name) for the
 * internal "guiname" used in the config files. Other names are returned unchanged.
 */
std::string dynamicGuiDisplayName(const std::string& guiname);

/**
 * @brief Frees the parsed definitions of all dynamic GUIs that no longer have a tab.
 * Called after the tabs have been (re)created.
 */
void dynamic_guis_trimCache();

#endif // GUIS_DYNAMIC_H
