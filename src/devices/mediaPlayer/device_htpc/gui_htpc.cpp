#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/gui/guiMemoryOptimizer.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "devices/mediaPlayer/device_htpc/device_htpc.h"
#include "devices/mediaPlayer/device_htpc/gui_htpc.h"
#include "devices/mediaPlayer/device_htpc/htpc_images.h"
#include "scenes/scene__default.h"

uint16_t GUI_HTPC_ACTIVATE;

std::map<char, repeatModes> key_repeatModes_htpc = {};
std::map<char, uint16_t> key_commands_short_htpc = {};
std::map<char, uint16_t> key_commands_long_htpc = {};

#if (ENABLE_WIFI_AND_MQTT == 1)

/*
  Memory: on ESP32 the LVGL pool (32 kB) is shared by all tabs in memory, and the heap is small. So this GUI
  - draws the whole movie grid (tiles, posters, titles and the blurred "peek" rows) in one widget instead of ~30
  - builds only the view that is visible (movie grid or now playing) and rebuilds when switching
  - uses shared styles instead of local style properties, which would be allocated per widget
  - frees the posters when the tab is deleted
*/

static const int maxTiles = HTPC_TILES_PER_PAGE;
// 3 x 2 tiles fill the tab. Blurred "peek" rows above (previous page) and below (next page) hint that you can swipe.
// They share the remaining height, so on pages after the first one the tiles are vertically centered.
// Tile size is calculated from the size of the tab when it is created.
static const int tileColumns = 3;
static const lv_coord_t tileGap = 4;
static const lv_coord_t rowGap = 6;
static const lv_coord_t tileRadius = 8;
static lv_coord_t tileWidth  = 69;
static lv_coord_t tileHeight = 104;
static lv_coord_t tabContentWidth = 0;
static lv_coord_t tabContentHeight = 0;
static const int peekBlocks = 3;
static const lv_coord_t posterWidth  = HTPC_POSTER_WIDTH;
static const lv_coord_t posterHeight = HTPC_POSTER_HEIGHT;
static const lv_coord_t buttonHeight = 36;
// A view needs a few kB of the LVGL pool. If the pool has less, show a message instead of
// running out of memory, which LVGL does not survive.
static const uint32_t minLvglFreeForView = 6 * 1024;

enum htpcView {VIEW_NONE, VIEW_TILES, VIEW_NOW_PLAYING};
// true: the user wants the now playing view (it is only shown while something is playing)
static bool showNowPlaying = false;
static htpcView currentView = VIEW_NONE;
static bool notEnoughMemory = false;
// last brightness sent, there is no feedback from the lights
static int32_t sliderStairsValue = 0;
static int32_t sliderMainValue = 0;

static lv_obj_t* htpcTab = NULL;
static lv_obj_t* statusLabel = NULL;
static lv_obj_t* viewBox = NULL;
// tiles view
static lv_obj_t* toNowPlayingButton = NULL;
static lv_obj_t* tilesObj = NULL;
static int pressedTile = -1;
// now playing view
static lv_obj_t* posterImage = NULL;
static lv_obj_t* titleLabel = NULL;
static lv_obj_t* grandparentLabel = NULL;
// progress bar, time, transport buttons and slider names are drawn by one widget
static lv_obj_t* controlsObj = NULL;
static int pressedButton = -1;
static const int transportButtons = 5;
static const lv_coord_t transportButtonWidth = 40;
// positions inside the controls widget
static const lv_coord_t progressY = 0;
static const lv_coord_t timeY = 12;
static const lv_coord_t buttonsY = 32;
static const lv_coord_t slidersY = buttonsY + buttonHeight + 12;
static const lv_coord_t sliderRowHeight = 28;
static lv_timer_t* progressTimer = NULL;
static lv_timer_t* imageTimer = NULL;

// --- shared styles ----------------------------------------------------------
static bool stylesInitialized = false;
static lv_style_t style_container;   // transparent, no border, no padding
static lv_style_t style_button;      // color_primary background
static lv_style_t style_small;       // montserrat 12
static lv_style_t style_title;       // montserrat 16
static lv_style_t style_sliderMain;
static lv_style_t style_sliderIndicator;
static lv_style_t style_sliderKnob;
// colors of the blurred peek blocks: vertical gradient between two random colors
static lv_color_t peekColorsTop[2 * peekBlocks];
static lv_color_t peekColorsBottom[2 * peekBlocks];

static void initStyles() {
  if (stylesInitialized) {return;}
  stylesInitialized = true;

  lv_style_init(&style_container);
  lv_style_set_bg_opa(&style_container, LV_OPA_TRANSP);
  lv_style_set_border_width(&style_container, 0);
  lv_style_set_pad_all(&style_container, 0);
  lv_style_set_pad_row(&style_container, rowGap);

  lv_style_init(&style_button);
  lv_style_set_bg_color(&style_button, color_primary);

  lv_style_init(&style_small);
  lv_style_set_text_font(&style_small, &lv_font_montserrat_12);

  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, &lv_font_montserrat_16);

  lv_style_init(&style_sliderMain);
  lv_style_set_bg_opa(&style_sliderMain, LV_OPA_COVER);
  lv_style_set_bg_color(&style_sliderMain, lv_color_lighten(color_primary, 50));
  lv_style_init(&style_sliderIndicator);
  lv_style_set_bg_color(&style_sliderIndicator, lv_color_lighten(lv_color_black(), 30));
  lv_style_set_bg_grad_color(&style_sliderIndicator, lv_color_lighten(lv_palette_main(LV_PALETTE_AMBER), 180));
  lv_style_set_bg_grad_dir(&style_sliderIndicator, LV_GRAD_DIR_HOR);
  lv_style_init(&style_sliderKnob);
  lv_style_set_bg_color(&style_sliderKnob, lv_color_white());

  for (int i=0; i<2 * peekBlocks; i++) {
    uint16_t hue = rand() % 360;
    peekColorsTop[i]    = lv_color_hsv_to_rgb(hue, 60, 50);
    peekColorsBottom[i] = lv_color_hsv_to_rgb((hue + 40 + rand() % 60) % 360, 60, 25);
  }
}

// --- helpers ----------------------------------------------------------------
static std::string formatTime(int64_t ms) {
  int64_t totalSeconds = ms / 1000;
  int hours   = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  char buffer[16];
  if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
  }
  return std::string(buffer);
}

static void setHidden(lv_obj_t* obj, bool hidden) {
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static lv_obj_t* createContainer(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_add_style(box, &style_container, LV_PART_MAIN);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  return box;
}


static void showView_event_cb(lv_event_t* e);

static lv_obj_t* createButton(lv_obj_t* parent, const char* text, lv_coord_t width) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_add_style(button, &style_button, LV_PART_MAIN);
  lv_obj_set_size(button, width, buttonHeight);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}


// --- tiles view: one widget that draws everything ---------------------------
// Layout of the tiles widget. It fills the space below the status line and the "Now playing" button.
static lv_coord_t peekAboveHeight() {
  if (htpc_getPageOffset() == 0) {return 0;}
  lv_coord_t free = lv_obj_get_height(tilesObj) - (2 * tileHeight + tileGap);
  return (free > 2 * rowGap) ? (free - 2 * rowGap) / 2 : 0;
}

static lv_coord_t gridTop() {
  lv_coord_t above = peekAboveHeight();
  return (above > 0) ? above + rowGap : 0;
}

// area of tile i, relative to the widget
static void tileArea(int i, lv_area_t* area) {
  lv_coord_t x = (i % tileColumns) * (tileWidth + tileGap);
  lv_coord_t y = gridTop() + (i / tileColumns) * (tileHeight + tileGap);
  area->x1 = x;
  area->y1 = y;
  area->x2 = x + tileWidth - 1;
  area->y2 = y + tileHeight - 1;
}

static void moveArea(lv_area_t* area, const lv_area_t* by) {
  area->x1 += by->x1;
  area->x2 += by->x1;
  area->y1 += by->y1;
  area->y2 += by->y1;
}

static void drawPeekBlock(lv_draw_ctx_t* draw_ctx, const lv_area_t* area, int colorIndex) {
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.radius = tileRadius;
  rect.bg_grad.dir = LV_GRAD_DIR_VER;
  rect.bg_grad.stops_count = 2;
  rect.bg_grad.stops[0].color = peekColorsTop[colorIndex];
  rect.bg_grad.stops[0].frac = 0;
  rect.bg_grad.stops[1].color = peekColorsBottom[colorIndex];
  rect.bg_grad.stops[1].frac = 255;
  lv_draw_rect(draw_ctx, &rect, area);
}

static void tiles_draw_event_cb(lv_event_t* e) {
  lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
  lv_area_t coords;
  lv_obj_get_coords(tilesObj, &coords);
  // only draw inside the widget, the peek blocks are cut off at its edges
  lv_area_t clip;
  if (!_lv_area_intersect(&clip, draw_ctx->clip_area, &coords)) {return;}
  const lv_area_t* clipOriginal = draw_ctx->clip_area;
  draw_ctx->clip_area = &clip;

  const std::vector<htpcTile>& tiles = htpc_getTiles();

  // peek rows: above only after the first page, below only if there is a next page
  lv_coord_t above = peekAboveHeight();
  for (int b=0; b<peekBlocks; b++) {
    lv_area_t area;
    if (above > 0) {
      area.x1 = b * (tileWidth + tileGap);
      area.x2 = area.x1 + tileWidth - 1;
      area.y2 = above - 1;
      area.y1 = area.y2 - tileHeight + 1;
      moveArea(&area, &coords);
      drawPeekBlock(draw_ctx, &area, b);
    }
    if (htpc_hasNextPage()) {
      area.x1 = b * (tileWidth + tileGap);
      area.x2 = area.x1 + tileWidth - 1;
      area.y1 = gridTop() + 2 * (tileHeight + tileGap) + rowGap - tileGap;
      area.y2 = area.y1 + tileHeight - 1;
      moveArea(&area, &coords);
      drawPeekBlock(draw_ctx, &area, peekBlocks + b);
    }
  }

  // tiles: poster, or the title if there is no poster (yet)
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.radius = tileRadius;
  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.font = &lv_font_montserrat_12;
  label.color = lv_color_white();
  label.align = LV_TEXT_ALIGN_CENTER;
  lv_draw_img_dsc_t img;
  lv_draw_img_dsc_init(&img);

  for (int i=0; i<maxTiles && i<(int)tiles.size(); i++) {
    lv_area_t area;
    tileArea(i, &area);
    moveArea(&area, &coords);
    rect.bg_color = (i == pressedTile) ? lv_color_lighten(color_primary, 60) : color_primary;
    lv_draw_rect(draw_ctx, &rect, &area);

    const lv_img_dsc_t* poster = htpc_images_get(tiles[i].thumbUrl);
    if (poster != NULL) {
      lv_area_t imgArea;
      imgArea.x1 = area.x1 + (tileWidth - poster->header.w) / 2;
      imgArea.y1 = area.y1 + (tileHeight - poster->header.h) / 2;
      imgArea.x2 = imgArea.x1 + poster->header.w - 1;
      imgArea.y2 = imgArea.y1 + poster->header.h - 1;
      lv_draw_img(draw_ctx, &img, &imgArea, poster);
    } else {
      std::string text = tiles[i].title;
      if (tiles[i].year > 0) {
        text += " (" + std::to_string(tiles[i].year) + ")";
      }
      lv_area_t textArea = area;
      textArea.x1 += 4;
      textArea.x2 -= 4;
      lv_point_t size;
      lv_txt_get_size(&size, text.c_str(), label.font, 0, 0, lv_area_get_width(&textArea), LV_TEXT_FLAG_NONE);
      textArea.y1 += (tileHeight - size.y) / 2;
      lv_draw_label(draw_ctx, &label, &textArea, text.c_str(), NULL);
    }
  }

  draw_ctx->clip_area = clipOriginal;
}

// which tile is at the touch point, -1 if none
static int tileAtPoint() {
  lv_indev_t* indev = lv_indev_get_act();
  if (indev == NULL) {return -1;}
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t coords;
  lv_obj_get_coords(tilesObj, &coords);
  for (int i=0; i<maxTiles && i<(int)htpc_getTiles().size(); i++) {
    lv_area_t area;
    tileArea(i, &area);
    moveArea(&area, &coords);
    if (_lv_area_is_point_on(&area, &point, 0)) {return i;}
  }
  return -1;
}

static void tiles_input_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    pressedTile = tileAtPoint();
    lv_obj_invalidate(tilesObj);
  } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
    pressedTile = -1;
    lv_obj_invalidate(tilesObj);
  } else if (code == LV_EVENT_CLICKED) {
    // LVGL still sends CLICKED after a swipe. Never start a movie because of a swipe.
    lv_indev_t* indev = lv_indev_get_act();
    if ((indev != NULL) && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE)) {return;}
    int index = tileAtPoint();
    if (index >= 0) {htpc_playTile(index);}
  }
}

static void buildTilesView() {
  if (htpc_getPlayerState().playing) {
    toNowPlayingButton = createButton(viewBox, LV_SYMBOL_PLAY " Now playing", lv_pct(100));
    lv_obj_add_event_cb(toNowPlayingButton, showView_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)true);
  }

  tilesObj = lv_obj_create(viewBox);
  lv_obj_remove_style_all(tilesObj);
  lv_obj_set_width(tilesObj, lv_pct(100));
  lv_obj_set_flex_grow(tilesObj, 1);
  lv_obj_clear_flag(tilesObj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tilesObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tilesObj, tiles_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(tilesObj, tiles_input_event_cb, LV_EVENT_ALL, NULL);
}

static void updateTilesView() {
  // the "Now playing" button only exists while something is playing, rebuild if that changed
  bool playing = htpc_getPlayerState().playing;
  if (playing != (toNowPlayingButton != NULL)) {
    lv_obj_clean(viewBox);
    toNowPlayingButton = NULL;
    buildTilesView();
  }
  lv_obj_invalidate(tilesObj);
}

// --- now playing view -------------------------------------------------------
static void updateProgress() {
  if ((currentView != VIEW_NOW_PLAYING) || (controlsObj == NULL)) {return;}
  // only the progress bar and the time change
  lv_area_t coords;
  lv_obj_get_coords(controlsObj, &coords);
  coords.y2 = coords.y1 + buttonsY - 1;
  lv_obj_invalidate_area(controlsObj, &coords);
}

static lv_coord_t transportButtonX(int i) {
  return i * ((tabContentWidth - transportButtonWidth) / (transportButtons - 1));
}

static void controls_draw_event_cb(lv_event_t* e) {
  lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
  lv_area_t coords;
  lv_obj_get_coords(controlsObj, &coords);
  const htpcPlayerState& state = htpc_getPlayerState();

  // progress bar
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.radius = LV_RADIUS_CIRCLE;
  rect.bg_color = lv_color_darken(lv_palette_main(LV_PALETTE_BLUE), 180);
  lv_area_t bar = {coords.x1, (lv_coord_t)(coords.y1 + progressY), coords.x2, (lv_coord_t)(coords.y1 + progressY + 7)};
  lv_draw_rect(draw_ctx, &rect, &bar);
  std::string timeText;
  if (state.hasPosition && (state.duration_ms > 0)) {
    int64_t position = htpc_getCurrentPosition_ms();
    lv_area_t done = bar;
    done.x2 = bar.x1 + (lv_area_get_width(&bar) - 1) * position / state.duration_ms;
    rect.bg_color = lv_palette_main(LV_PALETTE_BLUE);
    if (done.x2 > done.x1) {lv_draw_rect(draw_ctx, &rect, &done);}
    timeText = formatTime(position) + " / " + formatTime(state.duration_ms);
  }

  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.font = &lv_font_montserrat_12;
  label.color = lv_color_white();
  lv_area_t textArea = {coords.x1, (lv_coord_t)(coords.y1 + timeY), coords.x2, (lv_coord_t)(coords.y1 + timeY + 15)};
  lv_draw_label(draw_ctx, &label, &textArea, timeText.c_str(), NULL);

  // transport buttons
  static const char* symbols[transportButtons] = {"-10", NULL, "+30", LV_SYMBOL_STOP, LV_SYMBOL_LIST};
  rect.radius = 10;
  label.font = LV_FONT_DEFAULT;
  label.align = LV_TEXT_ALIGN_CENTER;
  for (int i=0; i<transportButtons; i++) {
    lv_area_t button = {(lv_coord_t)(coords.x1 + transportButtonX(i)), (lv_coord_t)(coords.y1 + buttonsY),
                        (lv_coord_t)(coords.x1 + transportButtonX(i) + transportButtonWidth - 1), (lv_coord_t)(coords.y1 + buttonsY + buttonHeight - 1)};
    rect.bg_color = (i == pressedButton) ? lv_color_lighten(color_primary, 60) : color_primary;
    lv_draw_rect(draw_ctx, &rect, &button);
    const char* text = (i == 1) ? (state.paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE) : symbols[i];
    lv_point_t size;
    lv_txt_get_size(&size, text, label.font, 0, 0, transportButtonWidth, LV_TEXT_FLAG_NONE);
    lv_area_t buttonText = button;
    buttonText.y1 += (buttonHeight - size.y) / 2;
    lv_draw_label(draw_ctx, &label, &buttonText, text, NULL);
  }

  // names of the light sliders (the sliders themselves are widgets)
  label.font = &lv_font_montserrat_12;
  label.align = LV_TEXT_ALIGN_LEFT;
  static const char* sliderNames[2] = {"Stairs", "Main"};
  for (int i=0; i<2; i++) {
    lv_area_t nameArea = {coords.x1, (lv_coord_t)(coords.y1 + slidersY + i * sliderRowHeight), (lv_coord_t)(coords.x1 + 56), (lv_coord_t)(coords.y1 + slidersY + i * sliderRowHeight + 15)};
    lv_draw_label(draw_ctx, &label, &nameArea, sliderNames[i], NULL);
  }
}

// which transport button is at the touch point, -1 if none
static int buttonAtPoint() {
  lv_indev_t* indev = lv_indev_get_act();
  if (indev == NULL) {return -1;}
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t coords;
  lv_obj_get_coords(controlsObj, &coords);
  lv_coord_t x = point.x - coords.x1;
  lv_coord_t y = point.y - coords.y1;
  if ((y < buttonsY) || (y >= buttonsY + buttonHeight)) {return -1;}
  for (int i=0; i<transportButtons; i++) {
    if ((x >= transportButtonX(i)) && (x < transportButtonX(i) + transportButtonWidth)) {return i;}
  }
  return -1;
}

static void controls_input_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    pressedButton = buttonAtPoint();
    lv_obj_invalidate(controlsObj);
  } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
    pressedButton = -1;
    lv_obj_invalidate(controlsObj);
  } else if (code == LV_EVENT_CLICKED) {
    lv_indev_t* indev = lv_indev_get_act();
    if ((indev != NULL) && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE)) {return;}
    switch (buttonAtPoint()) {
      case 0: executeCommand(HTPC_SEEK_BACK); break;
      case 1: executeCommand(HTPC_PLAY_PAUSE); break;
      case 2: executeCommand(HTPC_SEEK_FORWARD); break;
      case 3: executeCommand(HTPC_STOP); break;
      case 4: showView_event_cb(e); break;
      default: break;
    }
  }
}

// send the brightness only when the slider is released, not on every step
static void lightSlider_event_cb(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target(e);
  uint16_t command = *(uint16_t*)lv_event_get_user_data(e);
  int32_t value = lv_slider_get_value(slider);
  if (command == HTPC_LIGHT_STAIRS_BRIGHTNESS) {
    sliderStairsValue = value;
  } else {
    sliderMainValue = value;
  }
  char payload[8];
  snprintf(payload, sizeof(payload), "%.2f", float(value));
  executeCommand(command, payload);
}

static void createLightSlider(lv_obj_t* parent, lv_coord_t y, uint16_t* command, int32_t value) {
  lv_obj_t* slider = lv_slider_create(parent);
  lv_obj_add_style(slider, &style_sliderMain, LV_PART_MAIN);
  lv_obj_add_style(slider, &style_sliderIndicator, LV_PART_INDICATOR);
  lv_obj_add_style(slider, &style_sliderKnob, LV_PART_KNOB);
  lv_slider_set_range(slider, 0, 100);
  // leave room for the knob on both sides
  lv_obj_set_size(slider, tabContentWidth - 58 - 10, 10);
  lv_obj_set_pos(slider, 58, y + 3);
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, lightSlider_event_cb, LV_EVENT_RELEASED, command);
}

// absolute positions instead of layout containers, to save widgets
static void buildNowPlayingView() {
  lv_coord_t textX = posterWidth + 8;

  posterImage = lv_img_create(viewBox);
  lv_obj_set_pos(posterImage, 0, 0);
  lv_obj_add_flag(posterImage, LV_OBJ_FLAG_HIDDEN);

  titleLabel = lv_label_create(viewBox);
  lv_obj_add_style(titleLabel, &style_title, LV_PART_MAIN);
  lv_obj_set_width(titleLabel, tabContentWidth - textX);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(titleLabel, textX, 0);

  grandparentLabel = lv_label_create(viewBox);
  lv_obj_add_style(grandparentLabel, &style_small, LV_PART_MAIN);
  lv_obj_set_width(grandparentLabel, tabContentWidth - textX);
  lv_label_set_long_mode(grandparentLabel, LV_LABEL_LONG_WRAP);

  lv_coord_t y = posterHeight + 8;
  controlsObj = lv_obj_create(viewBox);
  lv_obj_remove_style_all(controlsObj);
  lv_obj_set_pos(controlsObj, 0, y);
  lv_obj_set_size(controlsObj, tabContentWidth, slidersY + 2 * sliderRowHeight);
  lv_obj_clear_flag(controlsObj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(controlsObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(controlsObj, controls_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(controlsObj, controls_input_event_cb, LV_EVENT_ALL, (void*)(intptr_t)false);

  createLightSlider(viewBox, y + slidersY,                   &HTPC_LIGHT_STAIRS_BRIGHTNESS, sliderStairsValue);
  createLightSlider(viewBox, y + slidersY + sliderRowHeight, &HTPC_LIGHT_MAIN_BRIGHTNESS,   sliderMainValue);
}

static void updateNowPlayingView() {
  const htpcPlayerState& state = htpc_getPlayerState();
  const lv_img_dsc_t* poster = htpc_images_get(state.thumbUrl);
  if (lv_img_get_src(posterImage) != poster) {
    lv_img_set_src(posterImage, poster);
  }
  setHidden(posterImage, poster == NULL);

  lv_label_set_text(titleLabel, state.title.c_str());
  lv_label_set_text(grandparentLabel, state.grandparent.c_str());
  setHidden(grandparentLabel, state.grandparent.empty());
  lv_obj_align_to(grandparentLabel, titleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
  lv_obj_invalidate(controlsObj);
}

// --- switching and updating ---------------------------------------------------
// Deletes the widgets of the current view and builds the other one
static void switchView(htpcView view) {
  if (view == currentView) {return;}
  lv_obj_clean(viewBox);
  posterImage = titleLabel = grandparentLabel = controlsObj = NULL;
  toNowPlayingButton = tilesObj = NULL;
  pressedTile = -1;
  pressedButton = -1;

  lv_mem_monitor_t mem;
  lv_mem_monitor(&mem);
  if (mem.free_size < minLvglFreeForView) {
    omote_log_w("htpc: only %u bytes free in the LVGL pool, not building the view\r\n", (unsigned int)mem.free_size);
    notEnoughMemory = true;
    currentView = VIEW_NONE;
    return;
  }
  notEnoughMemory = false;
  currentView = view;
  if (view == VIEW_TILES) {
    // the tiles widget fills the rest of the tab
    lv_obj_set_layout(viewBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(viewBox, LV_FLEX_FLOW_COLUMN);
    buildTilesView();
  } else {
    // absolute positions
    lv_obj_set_layout(viewBox, 0);
    buildNowPlayingView();
  }
}

void htpc_gui_update(void) {
  if (htpcTab == NULL) {return;}

  const htpcPlayerState& state = htpc_getPlayerState();
  bool nowPlayingVisible = showNowPlaying && state.playing;

  switchView(nowPlayingVisible ? VIEW_NOW_PLAYING : VIEW_TILES);

  if (notEnoughMemory) {
    lv_label_set_text(statusLabel, "Not enough memory for Movies");
  } else if (!htpc_getBridgeOnline()) {
    lv_label_set_text(statusLabel, "htpc offline");
  } else if (!nowPlayingVisible && htpc_getTiles().empty()) {
    lv_label_set_text(statusLabel, "No movies yet");
  } else {
    lv_label_set_text(statusLabel, "");
  }
  setHidden(statusLabel, std::string(lv_label_get_text(statusLabel)).empty());

  if (currentView == VIEW_TILES) {
    updateTilesView();
  } else if (currentView == VIEW_NOW_PLAYING) {
    updateNowPlayingView();
  }
}

void htpc_gui_playingChanged(bool playing) {
  showNowPlaying = playing;
  if (playing) {
    // bring the Movies tab to the front, it will show the now playing view
    executeCommand(GUI_HTPC_ACTIVATE);
  }
  htpc_gui_update();
}

static void deferredUpdate_cb(lv_timer_t* timer) {
  htpc_gui_update();
}

static void showView_event_cb(lv_event_t* e) {
  showNowPlaying = (bool)(intptr_t)lv_event_get_user_data(e);
  // The button sending this event would be deleted by switching the view, so switch after the event is done
  lv_timer_t* timer = lv_timer_create(deferredUpdate_cb, 0, NULL);
  lv_timer_set_repeat_count(timer, 1);
}

static void progressTimer_cb(lv_timer_t* timer) {
  updateProgress();
}

static void imageTimer_cb(lv_timer_t* timer) {
  if (htpc_images_loop()) {
    htpc_gui_update();
  }
}

// Swipe up: next page. Swipe down: previous page, or on the first page the usual "back to scene selection".
static void tab_gesture_event_cb(lv_event_t* e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  bool tilesVisible = (currentView == VIEW_TILES);
  if (dir == LV_DIR_TOP) {
    if (tilesVisible) {htpc_nextPage();}
  } else if (dir == LV_DIR_BOTTOM) {
    if (!(tilesVisible && htpc_previousPage())) {
      executeCommand(SCENE_SELECTION);
    }
  }
}

void create_tab_content_htpc(lv_obj_t* tab) {
  initStyles();
  htpcTab = tab;
  lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  // The tab does not scroll. Vertical swipes page through the movies instead, and the peek rows are simply cut off.
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(tab, tab_gesture_event_cb, LV_EVENT_GESTURE, NULL);

  statusLabel = lv_label_create(tab);
  lv_label_set_text(statusLabel, "");

  // size the tiles so that 3 x 2 fill the tab, in poster format 2:3
  lv_obj_update_layout(tab);
  tabContentWidth  = lv_obj_get_content_width(tab);
  tabContentHeight = lv_obj_get_content_height(tab);
  tileWidth  = (tabContentWidth - (tileColumns - 1) * tileGap) / tileColumns;
  tileHeight = tileWidth * 3 / 2;

  viewBox = createContainer(tab);
  lv_obj_set_width(viewBox, lv_pct(100));
  lv_obj_set_flex_grow(viewBox, 1);
  currentView = VIEW_NONE;

  progressTimer = lv_timer_create(progressTimer_cb, 1000, NULL);
  imageTimer = lv_timer_create(imageTimer_cb, 250, NULL);

  htpc_gui_update();
}

void notify_tab_before_delete_htpc(void) {
  htpcTab = NULL;
  tilesObj = NULL;
  currentView = VIEW_NONE;
  if (progressTimer != NULL) {
    lv_timer_del(progressTimer);
    progressTimer = NULL;
  }
  if (imageTimer != NULL) {
    lv_timer_del(imageTimer);
    imageTimer = NULL;
  }
  // give the heap back while the Movies tab is not in memory. The posters are downloaded again when it comes back.
  htpc_images_retainOnly(std::set<std::string>());
}

void gui_setKeys_htpc() {
  key_repeatModes_htpc = {
    {KEY_STOP, SHORT}, {KEY_REWI, SHORT}, {KEY_PLAY, SHORT}, {KEY_FORW, SHORT},
  };
  key_commands_short_htpc = {
    {KEY_STOP, HTPC_STOP}, {KEY_REWI, HTPC_SEEK_BACK}, {KEY_PLAY, HTPC_PLAY_PAUSE}, {KEY_FORW, HTPC_SEEK_FORWARD},
  };
}

#else

void htpc_gui_update(void) {}
void htpc_gui_playingChanged(bool playing) {}

void create_tab_content_htpc(lv_obj_t* tab) {
  lv_obj_t* label = lv_label_create(tab);
  lv_label_set_text(label, "Needs WiFi and MQTT");
}

void notify_tab_before_delete_htpc(void) {}

void gui_setKeys_htpc() {}

#endif

void register_gui_htpc(void){

  register_gui(
    std::string(tabName_htpc),
    & create_tab_content_htpc,
    & notify_tab_before_delete_htpc,
    & gui_setKeys_htpc,
    & key_repeatModes_htpc,
    & key_commands_short_htpc,
    & key_commands_long_htpc
    );

  register_command(&GUI_HTPC_ACTIVATE, makeCommandData(GUI, {std::to_string(MAIN_GUI_LIST), std::string(tabName_htpc)}));
}
