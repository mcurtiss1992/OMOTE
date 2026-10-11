#include <string.h>
#include "applicationInternal/gui/guiBase.h"
#include "guis/tileGrid.h"

static const lv_coord_t gap = 10;
static const lv_coord_t radius = 14;
static const lv_coord_t iconSize = 48;
static const lv_coord_t iconLabelGap = 6;
static const lv_coord_t pressInset = 4;        // a pressed tile shrinks by this much on each side
static const uint32_t pressAnimTime_ms = 80;

struct TileGridState {
  int columns;
  tileGrid_cb cb;
  std::vector<tileGridItem> items;
  lv_coord_t tileHeight = 0;  // 0: square
  int pressed = -1;
  int32_t pressAmount = 0;   // 0..256, animated
  bool longPressFired = false;
};

static TileGridState* stateOf(lv_obj_t* grid) {
  return (TileGridState*)lv_obj_get_user_data(grid);
}

static lv_coord_t tileSize(lv_obj_t* grid, TileGridState* state) {
  lv_coord_t width = lv_obj_get_content_width(grid);
  return (width - (state->columns - 1) * gap) / state->columns;
}

static lv_coord_t tileHeight(lv_obj_t* grid, TileGridState* state) {
  return (state->tileHeight > 0) ? state->tileHeight : tileSize(grid, state);
}

static void tileArea(lv_obj_t* grid, TileGridState* state, int index, lv_area_t* area) {
  lv_area_t coords;
  lv_obj_get_content_coords(grid, &coords);
  lv_coord_t width = tileSize(grid, state);
  lv_coord_t height = tileHeight(grid, state);
  int row = index / state->columns;
  int col = index % state->columns;
  area->x1 = coords.x1 + col * (width + gap);
  area->y1 = coords.y1 + row * (height + gap);
  area->x2 = area->x1 + width - 1;
  area->y2 = area->y1 + height - 1;
}

static int tileAtPoint(lv_obj_t* grid, TileGridState* state) {
  lv_indev_t* indev = lv_indev_get_act();
  if (indev == NULL) {return -1;}
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  for (int i=0; i<(int)state->items.size(); i++) {
    lv_area_t area;
    tileArea(grid, state, i, &area);
    if (_lv_area_is_point_on(&area, &point, 0)) {return i;}
  }
  return -1;
}

static void updateHeight(lv_obj_t* grid, TileGridState* state) {
  int rows = (state->items.size() + state->columns - 1) / state->columns;
  lv_coord_t size = tileHeight(grid, state);
  lv_coord_t height = (rows > 0) ? rows * size + (rows - 1) * gap : 0;
  if (lv_obj_get_content_height(grid) != height) {lv_obj_set_content_height(grid, height);}
}

static void draw_cb(lv_event_t* e) {
  lv_obj_t* grid = lv_event_get_target(e);
  TileGridState* state = stateOf(grid);
  lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);

  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.radius = radius;
  lv_draw_img_dsc_t img;
  lv_draw_img_dsc_init(&img);
  img.recolor = lv_color_white();
  img.recolor_opa = LV_OPA_COVER;
  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.color = lv_color_white();
  label.align = LV_TEXT_ALIGN_CENTER;

  for (int i=0; i<(int)state->items.size(); i++) {
    const tileGridItem& item = state->items[i];
    lv_area_t area;
    tileArea(grid, state, i, &area);
    bool pressed = (i == state->pressed);
    if (pressed) {
      lv_coord_t inset = pressInset * state->pressAmount / 256;
      lv_area_increase(&area, -inset, -inset);
    }
    rect.bg_color = pressed ? lv_color_mix(lv_color_white(), item.color, state->pressAmount / 6) : item.color;
    lv_draw_rect(draw_ctx, &rect, &area);

    if (state->tileHeight > 0) {
      // label only, centered
      label.font = &lv_font_montserrat_16;
      lv_coord_t h = lv_font_get_line_height(label.font);
      lv_area_t labelArea = {(lv_coord_t)(area.x1 + 8), (lv_coord_t)((area.y1 + area.y2 - h) / 2), (lv_coord_t)(area.x2 - 8), (lv_coord_t)((area.y1 + area.y2 + h) / 2)};
      lv_draw_label(draw_ctx, &label, &labelArea, item.label.c_str(), NULL);
      continue;
    }

    // icon (or the first letter) and the label below it, centered in the tile
    label.font = LV_FONT_DEFAULT;
    lv_coord_t labelHeight = lv_font_get_line_height(label.font);
    lv_coord_t blockHeight = iconSize + iconLabelGap + labelHeight;
    lv_coord_t top = area.y1 + (lv_area_get_height(&area) - blockHeight) / 2;
    lv_coord_t centerX = (area.x1 + area.x2) / 2;
    lv_area_t iconArea = {(lv_coord_t)(centerX - iconSize / 2), top, (lv_coord_t)(centerX - iconSize / 2 + iconSize - 1), (lv_coord_t)(top + iconSize - 1)};
    if (item.icon != NULL) {
      lv_draw_img(draw_ctx, &img, &iconArea, item.icon);
    } else if (!item.label.empty()) {
      char letter[2] = {item.label[0], 0};
      lv_draw_label_dsc_t big = label;
      big.font = &lv_font_montserrat_24;
      lv_coord_t h = lv_font_get_line_height(big.font);
      lv_area_t letterArea = {iconArea.x1, (lv_coord_t)(iconArea.y1 + (iconSize - h) / 2), iconArea.x2, (lv_coord_t)(iconArea.y1 + (iconSize + h) / 2)};
      lv_draw_label(draw_ctx, &big, &letterArea, letter, NULL);
    }
    lv_area_t labelArea = {(lv_coord_t)(area.x1 + 4), (lv_coord_t)(iconArea.y2 + 1 + iconLabelGap), (lv_coord_t)(area.x2 - 4), (lv_coord_t)(iconArea.y2 + iconLabelGap + labelHeight)};
    lv_draw_label(draw_ctx, &label, &labelArea, item.label.c_str(), NULL);
  }
}

static void pressAnim_cb(void* var, int32_t value) {
  lv_obj_t* grid = (lv_obj_t*)var;
  stateOf(grid)->pressAmount = value;
  lv_obj_invalidate(grid);
}

static void animatePress(lv_obj_t* grid, bool down) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, grid);
  lv_anim_set_exec_cb(&a, pressAnim_cb);
  lv_anim_set_values(&a, stateOf(grid)->pressAmount, down ? 256 : 0);
  lv_anim_set_time(&a, pressAnimTime_ms);
  lv_anim_start(&a);
}

// The callback may switch scenes, which deletes this grid. So it is called from a timer, after the event.
static struct {tileGrid_cb cb; int index; bool longPress;} pendingTap;
static void pendingTap_timer_cb(lv_timer_t* timer) {
  if (pendingTap.cb != NULL) {pendingTap.cb(pendingTap.index, pendingTap.longPress);}
  pendingTap.cb = NULL;
}
static void tap(TileGridState* state, int index, bool longPress) {
  if ((index < 0) || (state->cb == NULL)) {return;}
  pendingTap = {state->cb, index, longPress};
  lv_timer_t* timer = lv_timer_create(pendingTap_timer_cb, 30, NULL);
  lv_timer_set_repeat_count(timer, 1);
}

static void input_cb(lv_event_t* e) {
  lv_obj_t* grid = lv_event_get_target(e);
  TileGridState* state = stateOf(grid);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    state->pressed = tileAtPoint(grid, state);
    state->longPressFired = false;
    if (state->pressed >= 0) {animatePress(grid, true);}
  } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
    animatePress(grid, false);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    int index = tileAtPoint(grid, state);
    if ((index >= 0) && (index == state->pressed)) {
      state->longPressFired = true;
      tap(state, index, true);
    }
  } else if (code == LV_EVENT_CLICKED) {
    // LVGL still sends CLICKED after a swipe or a long press
    lv_indev_t* indev = lv_indev_get_act();
    if ((indev == NULL) || (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) || state->longPressFired) {return;}
    int index = tileAtPoint(grid, state);
    if (index == state->pressed) {tap(state, index, false);}
  } else if (code == LV_EVENT_SIZE_CHANGED) {
    updateHeight(grid, state);
  } else if (code == LV_EVENT_DELETE) {
    lv_anim_del(grid, NULL);
    delete state;
    lv_obj_set_user_data(grid, NULL);
  }
}

lv_obj_t* tileGrid_create(lv_obj_t* parent, int columns, tileGrid_cb cb) {
  lv_obj_t* grid = lv_obj_create(parent);
  lv_obj_remove_style_all(grid);
  lv_obj_set_width(grid, lv_pct(100));
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_CLICKABLE);
  TileGridState* state = new TileGridState();
  state->columns = (columns > 0) ? columns : 1;
  state->cb = cb;
  lv_obj_set_user_data(grid, state);
  lv_obj_add_event_cb(grid, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(grid, input_cb, LV_EVENT_ALL, NULL);
  return grid;
}

void tileGrid_setTileHeight(lv_obj_t* grid, lv_coord_t height) {
  TileGridState* state = stateOf(grid);
  if (state == NULL) {return;}
  state->tileHeight = height;
  updateHeight(grid, state);
  lv_obj_invalidate(grid);
}

void tileGrid_setItems(lv_obj_t* grid, const std::vector<tileGridItem>& items) {
  TileGridState* state = stateOf(grid);
  if (state == NULL) {return;}
  state->items = items;
  state->pressed = -1;
  lv_obj_update_layout(grid);
  updateHeight(grid, state);
  lv_obj_invalidate(grid);
}

lv_color_t tileGrid_colorForName(const char* name) {
  std::string n = name;
  for (char& c : n) {c = tolower((unsigned char)c);}
  auto has = [&n](const char* part) {return n.find(part) != std::string::npos;};
  if (has("apple"))                                   {return lv_color_hex(0x3a3a3c);}
  if (has("htpc") || has("linux"))                    {return lv_color_hex(0x8a5a00);}
  if (has("ps5") || has("ps4") || has("playstation")) {return lv_color_hex(0x0050c8);}
  if (has("plexamp"))                                 {return lv_color_hex(0x7a5600);}
  if (has("plex"))                                    {return lv_color_hex(0x1f1f1f);}
  if (has("jellyfin"))                                {return lv_color_hex(0x5a2d82);}
  if (has("spotify"))                                 {return lv_color_hex(0x13853e);}
  if (has("prime") || has("amazon"))                  {return lv_color_hex(0x0a5f8a);}
  if (has("kanopy"))                                  {return lv_color_hex(0xb3261e);}
  if (has("overseerr") || has("request"))             {return lv_color_hex(0x4338ca);}
  return lv_color_hex(0x303030);
}
