#include <lvgl.h>
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/omote_log.h"
#include "guis/gui_sceneSelection.h"
#include "guis/gui_icons.h"
#include "guis/tileGrid.h"

//void activate_scene_async(void *command) {
//  executeCommand(activate_scene_command);
//}
void activate_scene_cb(lv_timer_t *timer) {
  uint16_t scene_command_including_force = (uintptr_t)(timer->user_data);
  // get the force flag from the highest bit
  uint16_t activate_scene_command = scene_command_including_force & 0x7FFF;
  bool doForceScene = (scene_command_including_force & 0x8000) == 0x8000;
  if (doForceScene) {
    executeCommand(activate_scene_command, "FORCE");
  } else {
    executeCommand(activate_scene_command);
  }
}

// a tile was tapped (or held: start the scene again even if it is already active)
static void sceneTile_cb(int index, bool longPress) {
  scene_list scenes = get_scenes_on_sceneSelectionGUI();
  if ((scenes == NULL) || (index >= (int)scenes->size())) {return;}
  std::string scene_name = scenes->at(index);

  uint16_t activate_scene_command = get_activate_scene_command(scene_name);
  if (activate_scene_command == 0) {
    omote_log_w("Cannot activate scene %s, because command was not found\r\n", scene_name.c_str());
    return;
  }
  // the label is only updated on screen if the command is executed a bit later, from a timer
  if (SceneLabel != NULL) {lv_label_set_text(SceneLabel, "changing...");}
  uint16_t scene_command_including_force = activate_scene_command;
  if (longPress) {
    // put the force flag into the highest bit
    scene_command_including_force |= 0x8000;
    omote_log_d("Scene with index %d and name %s was FORCE selected\r\n", index, scene_name.c_str());
  } else {
    omote_log_d("Scene with index %d and name %s was selected\r\n", index, scene_name.c_str());
  }
  lv_timer_t *my_timer = lv_timer_create(activate_scene_cb, 50, (void *)(uintptr_t) scene_command_including_force);
  lv_timer_set_repeat_count(my_timer, 1);
}

void create_tab_content_sceneSelection(lv_obj_t* tab) {

  // one square tile per scene, two per row
  lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_ACTIVE);

  scene_list scenes = get_scenes_on_sceneSelectionGUI();
  if ((scenes == NULL) || (scenes->size() == 0)) {return;}
  std::vector<tileGridItem> items;
  for (const std::string& name : *scenes) {
    items.push_back({name, icon_forName(name.c_str()), tileGrid_colorForName(name.c_str())});
  }
  lv_obj_t* grid = tileGrid_create(tab, 2, sceneTile_cb);
  tileGrid_setItems(grid, items);
}

void notify_tab_before_delete_sceneSelection(void) {
  // remember to set all pointers to lvgl objects to NULL if they might be accessed from outside.
  // They must check if object is NULL and must not use it if so
  
}

void register_gui_sceneSelection(void){
  register_gui(std::string(tabName_sceneSelection), & create_tab_content_sceneSelection, & notify_tab_before_delete_sceneSelection);
}
