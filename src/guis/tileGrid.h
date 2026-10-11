#pragma once
#include <string>
#include <vector>
#include <lvgl.h>

// A grid of square tiles with an icon and a label, drawn by a single LVGL object (no object per tile).
// Used for the home screen (scenes) and for app menus.

struct tileGridItem {
  std::string label;
  const lv_img_dsc_t* icon;  // 4-bit alpha icon drawn in white, NULL: the first letter of the label is drawn
  lv_color_t color;          // tile background
};

// index of the tapped tile; longPress is true if the tile was held
typedef void (*tileGrid_cb)(int index, bool longPress);

lv_obj_t* tileGrid_create(lv_obj_t* parent, int columns, tileGrid_cb cb);
// tiles are square by default. With a height, they are as wide as their column and show only their label (for choices like "Movies" / "TV").
void tileGrid_setTileHeight(lv_obj_t* grid, lv_coord_t height);
// the grid gets the height its rows need
void tileGrid_setItems(lv_obj_t* grid, const std::vector<tileGridItem>& items);

// a tile color that fits the name (brand colors for known scenes and apps)
lv_color_t tileGrid_colorForName(const char* name);
