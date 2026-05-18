#include <pebble.h>
#include <string.h>

#define MAX_TASKS 50
#define MAX_TASK_NAME_LENGTH 64
#define MAX_TASK_ID_LENGTH 64
#define DETAIL_BUF_SIZE 128

// --- Main Menu ---
static Window *s_main_window;
static MenuLayer *s_main_menu_layer;

// --- Tasks List Window ---
static Window *s_tasks_window;
static MenuLayer *s_tasks_menu_layer;
static TextLayer *s_loading_layer;

// --- Detail Window ---
static Window *s_detail_window;
static ScrollLayer *s_detail_scroll_layer;
static TextLayer *s_detail_title_layer;
static TextLayer *s_detail_project_label;
static TextLayer *s_detail_areas_label;
static TextLayer *s_detail_contexts_label;
static TextLayer *s_detail_due_label;
static TextLayer *s_detail_status_label;
static TextLayer *s_detail_note_label;
static ActionBarLayer *s_detail_action_bar;
static GBitmap *s_icon_complete;
static GBitmap *s_icon_edit;
static GBitmap *s_icon_status;

// --- Status Bars ---
static TextLayer *s_main_status_bar_layer;
static TextLayer *s_tasks_status_bar_layer;
static TextLayer *s_detail_status_bar_layer;

// --- Status Picker Window ---
static Window *s_status_window;
static SimpleMenuLayer *s_status_menu_layer;
static SimpleMenuSection s_status_section;
static SimpleMenuItem s_status_items[5];

// --- Edit Menu Window ---
static Window *s_edit_window;
static SimpleMenuLayer *s_edit_menu_layer;
static SimpleMenuSection s_edit_section;
static SimpleMenuItem s_edit_items[6];

// --- List Window ---
static Window *s_list_window;
static MenuLayer *s_list_menu_layer;
static char s_list_items[MAX_TASKS][MAX_TASK_NAME_LENGTH];
static char s_list_ids[MAX_TASKS][MAX_TASK_ID_LENGTH];
static int s_list_num_items = 0;
static int s_list_type = 0; // 0=Title, 1=Area, 2=Tags, 3=Project

// --- Dictation ---
static DictationSession *s_dictation_session;
static char s_dictation_text[512];
static bool s_dictation_is_rename = false;
static int s_edit_mode = 0; // 0=Title, 1=Area, 2=Tags, 3=Project
static bool s_action_mode = false;

// --- Done / Completion Animation Window ---
static Window *s_done_window;
static Layer  *s_done_layer;
static AnimationProgress s_done_progress = 0;

// --- Task Data ---
static char s_tasks[MAX_TASKS][MAX_TASK_NAME_LENGTH];
static char s_task_ids[MAX_TASKS][MAX_TASK_ID_LENGTH];
static int s_task_states[MAX_TASKS];
static int s_num_tasks = 0;
static bool s_is_loading = true;
static bool s_js_ready = false;
static bool s_pending_task_request = false;
static int s_current_view = 0;

// --- Current Detail Task ---
static char s_detail_id[MAX_TASK_ID_LENGTH];
static char s_detail_name[MAX_TASK_NAME_LENGTH];
static char s_detail_note[DETAIL_BUF_SIZE];
static char s_detail_due[32];
static char s_detail_areas[DETAIL_BUF_SIZE];
static char s_detail_contexts[DETAIL_BUF_SIZE];
static char s_detail_project[MAX_TASK_NAME_LENGTH];
static int  s_detail_state = 0;
static int  s_detail_focus = 0;

// Built display string buffers
static char s_buf_project[DETAIL_BUF_SIZE + 16];
static char s_buf_status[32];
static char s_buf_due[48];

// ==================== HELPERS ====================

static const char* state_name(int state) {
  switch (state) {
    case 0: return "Inbox";
    case 1: return "Next";
    case 2: return "Waiting";
    case 3: return "Scheduled";
    case 4: return "Someday";
    case 5: return "Later";
    case 6: return "Active Project";
    case 7: return "Logged";
    case 8: return "Trash";
    default: return "Unknown";
  }
}

// ==================== FORWARD DECLS ====================
static void detail_window_push(void);
static void request_detail(void);

// ==================== TASK REQUEST ====================

static void request_tasks(void) {
  s_is_loading = true;
  s_num_tasks = 0;
  if (s_loading_layer && s_tasks_menu_layer) {
    layer_set_hidden(text_layer_get_layer(s_loading_layer), false);
    layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), true);
  }
  if (s_tasks_menu_layer) {
    menu_layer_reload_data(s_tasks_menu_layer);
  }
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint8(iter, MESSAGE_KEY_AppKeyRequestTasks, s_current_view);
  app_message_outbox_send();
}

static void request_detail(void) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
  app_message_outbox_send();
}

// ==================== DICTATION ====================

static void dictation_session_callback(DictationSession *session,
                                        DictationSessionStatus status,
                                        char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    DictionaryIterator *iter;
    if (s_dictation_is_rename) {
      app_message_outbox_begin(&iter);
      if (s_edit_mode == 0) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyRenameTask, transcription);
      } else if (s_edit_mode == 1) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditArea, transcription);
      } else if (s_edit_mode == 2) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditTags, transcription);
      } else if (s_edit_mode == 3) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditProject, transcription);
      } else if (s_edit_mode == 4) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditNote, transcription);
      } else if (s_edit_mode == 5) {
        dict_write_cstring(iter, MESSAGE_KEY_AppKeyAppendNote, transcription);
      }
      dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
      app_message_outbox_send();
    } else {
      app_message_outbox_begin(&iter);
      dict_write_cstring(iter, MESSAGE_KEY_AppKeyCreateTask, transcription);
      app_message_outbox_send();
    }
  }
}

// ==================== LIST WINDOW ====================

static uint16_t list_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_list_num_items;
}

static void list_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_list_items[cell_index->row], NULL, NULL);
}

static void list_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
  
  if (s_list_type == 0) {
    dict_write_cstring(iter, MESSAGE_KEY_AppKeyRenameTask, s_list_items[cell_index->row]);
  } else if (s_list_type == 1) {
    dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditArea, s_list_ids[cell_index->row]);
  } else if (s_list_type == 2) {
    dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditTags, s_list_items[cell_index->row]);
  } else if (s_list_type == 3) {
    dict_write_cstring(iter, MESSAGE_KEY_AppKeyEditProject, s_list_ids[cell_index->row]);
  }
  
  app_message_outbox_send();
  window_stack_pop(true); // pop list
  window_stack_pop(true); // pop edit menu
}

static void list_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_list_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_list_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = list_menu_get_num_rows_callback,
    .draw_row = list_menu_draw_row_callback,
    .select_click = list_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_list_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_list_menu_layer));
}

static void list_window_unload(Window *window) {
  menu_layer_destroy(s_list_menu_layer);
}

static void request_list(int type) {
  s_list_type = type;
  s_list_num_items = 0;
  if (s_list_menu_layer) {
    menu_layer_reload_data(s_list_menu_layer);
  }
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_int32(iter, MESSAGE_KEY_AppKeyRequestList, type);
  app_message_outbox_send();
  window_stack_push(s_list_window, true);
}

// ==================== EDIT MENU WINDOW ====================

static void edit_title_cb(int index, void *ctx) {
  request_list(0);
}

static void edit_area_cb(int index, void *ctx) {
  request_list(1);
}

static void edit_tags_cb(int index, void *ctx) {
  request_list(2);
}

static void edit_project_cb(int index, void *ctx) {
  request_list(3);
}

static void edit_desc_replace_cb(int index, void *ctx) {
  s_dictation_is_rename = true;
  s_edit_mode = 4; // 4 = Description/Note Replace
  dictation_session_start(s_dictation_session);
}

static void edit_desc_append_cb(int index, void *ctx) {
  s_dictation_is_rename = true;
  s_edit_mode = 5; // 5 = Description/Note Append
  dictation_session_start(s_dictation_session);
}

static void edit_window_load(Window *window) {
  s_edit_items[0] = (SimpleMenuItem){ .title = "Edit Title",   .callback = edit_title_cb };
  s_edit_items[1] = (SimpleMenuItem){ .title = "Edit Area",    .callback = edit_area_cb };
  s_edit_items[2] = (SimpleMenuItem){ .title = "Edit Tags",    .callback = edit_tags_cb };
  s_edit_items[3] = (SimpleMenuItem){ .title = "Edit Project", .callback = edit_project_cb };
  s_edit_items[4] = (SimpleMenuItem){ .title = "Replace Description", .callback = edit_desc_replace_cb };
  s_edit_items[5] = (SimpleMenuItem){ .title = "Append Description", .callback = edit_desc_append_cb };

  s_edit_section = (SimpleMenuSection){
    .title = "Edit Task",
    .items = s_edit_items,
    .num_items = 6
  };

  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_edit_menu_layer = simple_menu_layer_create(bounds, window, &s_edit_section, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_edit_menu_layer));
}

static void edit_window_unload(Window *window) {
  simple_menu_layer_destroy(s_edit_menu_layer);
}

// ==================== STATUS PICKER ====================

static void status_pick(int new_state) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
  dict_write_int32(iter, MESSAGE_KEY_AppKeyChangeTaskState, new_state);
  app_message_outbox_send();
  window_stack_remove(s_status_window, true);
}

static void status_focus_cb(int index, void *ctx)    { status_pick(-1); } // -1 = focus
static void status_next_cb(int index, void *ctx)     { status_pick(1); }
static void status_later_cb(int index, void *ctx)    { status_pick(5); }
static void status_someday_cb(int index, void *ctx)  { status_pick(4); }
static void status_inbox_cb(int index, void *ctx)    { status_pick(0); }

static void status_window_load(Window *window) {
  s_status_items[0] = (SimpleMenuItem){ .title = "Focus",   .callback = status_focus_cb };
  s_status_items[1] = (SimpleMenuItem){ .title = "Next",    .callback = status_next_cb };
  s_status_items[2] = (SimpleMenuItem){ .title = "Later",   .callback = status_later_cb };
  s_status_items[3] = (SimpleMenuItem){ .title = "Someday", .callback = status_someday_cb };
  s_status_items[4] = (SimpleMenuItem){ .title = "Inbox",   .callback = status_inbox_cb };

  s_status_section = (SimpleMenuSection){
    .title = "Change Status",
    .items = s_status_items,
    .num_items = 5
  };

  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_status_menu_layer = simple_menu_layer_create(bounds, window, &s_status_section, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_status_menu_layer));
}

static void status_window_unload(Window *window) {
  simple_menu_layer_destroy(s_status_menu_layer);
}

// ==================== DONE ANIMATION WINDOW ====================

static GPoint gpoint_lerp(GPoint a, GPoint b, AnimationProgress t, AnimationProgress max) {
  return GPoint(a.x + (int32_t)(b.x - a.x) * t / max,
                a.y + (int32_t)(b.y - a.y) * t / max);
}

static void done_layer_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w, H = bounds.size.h;

  // Black background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Checkmark anchor points (percent of screen)
  GPoint p0 = GPoint(W * 19 / 100, H * 52 / 100); // left arm start
  GPoint p1 = GPoint(W * 38 / 100, H * 68 / 100); // vertex
  GPoint p2 = GPoint(W * 81 / 100, H * 31 / 100); // right arm end

  graphics_context_set_stroke_color(ctx, GColorGreen);
  graphics_context_set_stroke_width(ctx, 5);

  AnimationProgress M = ANIMATION_NORMALIZED_MAX;
  AnimationProgress p = s_done_progress;
  AnimationProgress left_end = M * 30 / 100; // left arm = first 30%

  if (p > 0) {
    if (p <= left_end) {
      GPoint ep = gpoint_lerp(p0, p1, p, left_end);
      graphics_draw_line(ctx, p0, ep);
    } else {
      graphics_draw_line(ctx, p0, p1);
      AnimationProgress rp = p - left_end;
      AnimationProgress rm = M - left_end;
      GPoint ep = gpoint_lerp(p1, p2, rp, rm);
      graphics_draw_line(ctx, p1, ep);
    }
  }

  // "Done!" text fades in after 80% of animation
  if (p > M * 80 / 100) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "Done!",
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(0, H * 74 / 100, W, 30),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void done_anim_update(Animation *anim, AnimationProgress progress) {
  s_done_progress = progress;
  layer_mark_dirty(s_done_layer);
}

static const AnimationImplementation s_done_anim_impl = {
  .update = done_anim_update,
};

static void done_dismiss_callback(void *data) {
  window_stack_remove(s_detail_window, false);
  window_stack_remove(s_done_window, true);
}

static void done_window_appear(Window *window) {
  s_done_progress = 0;
  vibes_short_pulse();
  Animation *anim = animation_create();
  animation_set_duration(anim, 700);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_done_anim_impl);
  animation_schedule(anim);
  app_timer_register(1500, done_dismiss_callback, NULL);
}

static void done_window_load(Window *window) {
  window_set_background_color(window, GColorBlack);
  Layer *root = window_get_root_layer(window);
  s_done_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_done_layer, done_layer_draw);
  layer_add_child(root, s_done_layer);
}

static void done_window_unload(Window *window) {
  layer_destroy(s_done_layer);
}

// ==================== DETAIL WINDOW ====================

// --- Scroll helpers (manual up/down since we manage click config ourselves) ---
static void detail_scroll_up(ClickRecognizerRef r, void *ctx) {
  GPoint off = scroll_layer_get_content_offset(s_detail_scroll_layer);
  off.y += 24;
  if (off.y > 0) off.y = 0;
  scroll_layer_set_content_offset(s_detail_scroll_layer, off, true);
}
static void detail_scroll_down(ClickRecognizerRef r, void *ctx) {
  GPoint off = scroll_layer_get_content_offset(s_detail_scroll_layer);
  off.y -= 24;
  scroll_layer_set_content_offset(s_detail_scroll_layer, off, true);
}

// --- Action mode helpers ---
static void scroll_click_config(void *ctx);
static void action_click_config(void *ctx);

static void exit_action_mode(void) {
  s_action_mode = false;
  layer_set_hidden(action_bar_layer_get_layer(s_detail_action_bar), true);
  window_set_click_config_provider(s_detail_window, scroll_click_config);
}

static void detail_toggle_action_mode(ClickRecognizerRef r, void *ctx) {
  if (s_action_mode) {
    exit_action_mode();
  } else {
    s_action_mode = true;
    layer_set_hidden(action_bar_layer_get_layer(s_detail_action_bar), false);
    window_set_click_config_provider(s_detail_window, action_click_config);
  }
}

// Action mode button handlers
static void action_up_click(ClickRecognizerRef r, void *ctx) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
  dict_write_uint8(iter, MESSAGE_KEY_AppKeyCompleteTask, 1);
  app_message_outbox_send();
  exit_action_mode();
  // Show completion animation instead of immediately popping
  window_stack_push(s_done_window, true);
}
static void action_select_click(ClickRecognizerRef r, void *ctx) {
  exit_action_mode();
  window_stack_push(s_edit_window, true);
}
static void action_down_click(ClickRecognizerRef r, void *ctx) {
  exit_action_mode();
  window_stack_push(s_status_window, true);
}

static void scroll_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   50, detail_scroll_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 50, detail_scroll_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, detail_toggle_action_mode);
}
static void action_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     action_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, action_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   action_down_click);
}

static void detail_refresh_layers(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_detail_window));
  int W = bounds.size.w;
  int w = W - 8;
  int y = 4;

  // Title: dynamic height measuring
  text_layer_set_text(s_detail_title_layer, s_detail_name);
  GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize title_size = graphics_text_layout_get_content_size(s_detail_name, title_font,
      GRect(0, 0, w, 1000), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int title_h = title_size.h;
  if (title_h < 24) title_h = 24;
  layer_set_frame(text_layer_get_layer(s_detail_title_layer), GRect(4, y, w, title_h));
  y += title_h + 4;

  // Status: bold text, no prefix label
  if (s_detail_focus) {
    snprintf(s_buf_status, sizeof(s_buf_status), "\u2605 %s", state_name(s_detail_state));
    text_layer_set_text(s_detail_status_label, s_buf_status);
    text_layer_set_text_color(s_detail_status_label, GColorYellow);
  } else {
    text_layer_set_text(s_detail_status_label, state_name(s_detail_state));
    text_layer_set_text_color(s_detail_status_label, GColorBlack);
  }
  layer_set_frame(text_layer_get_layer(s_detail_status_label), GRect(0, y, W, 20));
  y += 20;

  // Areas (completely collapsed if empty)
  if (s_detail_areas[0]) {
    text_layer_set_text(s_detail_areas_label, s_detail_areas);
    layer_set_frame(text_layer_get_layer(s_detail_areas_label), GRect(0, y, W, 20));
    layer_set_hidden(text_layer_get_layer(s_detail_areas_label), false);
    y += 20;
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_areas_label), true);
  }

  // Contexts (completely collapsed if empty)
  if (s_detail_contexts[0]) {
    text_layer_set_text(s_detail_contexts_label, s_detail_contexts);
    layer_set_frame(text_layer_get_layer(s_detail_contexts_label), GRect(0, y, W, 20));
    layer_set_hidden(text_layer_get_layer(s_detail_contexts_label), false);
    y += 20;
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_contexts_label), true);
  }

  y += 4; // gap

  // Project (completely collapsed if empty)
  if (s_detail_project[0]) {
    snprintf(s_buf_project, sizeof(s_buf_project), "Project: %s", s_detail_project);
    text_layer_set_text(s_detail_project_label, s_buf_project);
    layer_set_frame(text_layer_get_layer(s_detail_project_label), GRect(4, y, w, 18));
    layer_set_hidden(text_layer_get_layer(s_detail_project_label), false);
    y += 20;
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_project_label), true);
  }

  // Due date (completely collapsed if empty)
  if (s_detail_due[0]) {
    snprintf(s_buf_due, sizeof(s_buf_due), "Due: %s", s_detail_due);
    text_layer_set_text(s_detail_due_label, s_buf_due);
    layer_set_frame(text_layer_get_layer(s_detail_due_label), GRect(4, y, w, 18));
    layer_set_hidden(text_layer_get_layer(s_detail_due_label), false);
    y += 20;
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_due_label), true);
  }

  // Notes: dynamic height measuring
  const char *note_text = s_detail_note[0] ? s_detail_note : "(no notes)";
  text_layer_set_text(s_detail_note_label, note_text);
  GFont note_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GSize note_size = graphics_text_layout_get_content_size(note_text, note_font,
      GRect(0, 0, w, 1000), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int note_h = note_size.h;
  if (note_h < 20) note_h = 20;
  layer_set_frame(text_layer_get_layer(s_detail_note_label), GRect(4, y, w, note_h + 10));
  y += note_h + 20;

  // Dynamic scroll size
  scroll_layer_set_content_size(s_detail_scroll_layer, GSize(W, y + 10));
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int W = bounds.size.w;

  // Top Status Bar
  s_detail_status_bar_layer = text_layer_create(GRect(0, 0, bounds.size.w, 16));
  text_layer_set_text(s_detail_status_bar_layer, "NirvanaHQ GTD");
  text_layer_set_text_alignment(s_detail_status_bar_layer, GTextAlignmentCenter);
  text_layer_set_font(s_detail_status_bar_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_background_color(s_detail_status_bar_layer, GColorCobaltBlue);
  text_layer_set_text_color(s_detail_status_bar_layer, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_detail_status_bar_layer));

  // Scroll layer uses remaining height — added FIRST so action bar renders on top
  s_detail_scroll_layer = scroll_layer_create(GRect(0, 16, bounds.size.w, bounds.size.h - 16));
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll_layer));

  // Action bar: overlay on right edge, added LAST so it's on top of scroll content.
  // Starts HIDDEN — shown only when user presses middle button.
  s_detail_action_bar = action_bar_layer_create();
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_UP,     s_icon_complete);
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_SELECT, s_icon_edit);
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_DOWN,   s_icon_status);
  GRect bar_frame = GRect(W - ACTION_BAR_WIDTH, 16, ACTION_BAR_WIDTH, bounds.size.h - 16);
  layer_set_frame(action_bar_layer_get_layer(s_detail_action_bar), bar_frame);
  layer_add_child(root, action_bar_layer_get_layer(s_detail_action_bar));
  layer_set_hidden(action_bar_layer_get_layer(s_detail_action_bar), true);

  int y = 4;
  int w = W - 8;

  // Title: bold, wrapping
  s_detail_title_layer = text_layer_create(GRect(4, y, w, 52));
  text_layer_set_font(s_detail_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_overflow_mode(s_detail_title_layer, GTextOverflowModeWordWrap);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_title_layer));
  y += 56;

  // Status: grey background, bold, centered
  s_detail_status_label = text_layer_create(GRect(0, y, W, 20));
  text_layer_set_font(s_detail_status_label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_background_color(s_detail_status_label, GColorLightGray);
  text_layer_set_text_color(s_detail_status_label, GColorBlack);
  text_layer_set_text_alignment(s_detail_status_label, GTextAlignmentCenter);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_status_label));
  y += 20;

  // Areas: orange background, right below status
  s_detail_areas_label = text_layer_create(GRect(0, y, W, 20));
  text_layer_set_font(s_detail_areas_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_detail_areas_label, GColorOrange);
  text_layer_set_text_color(s_detail_areas_label, GColorWhite);
  text_layer_set_text_alignment(s_detail_areas_label, GTextAlignmentCenter);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_areas_label));
  y += 20;

  // Contexts: pink background, below areas
  s_detail_contexts_label = text_layer_create(GRect(0, y, W, 20));
  text_layer_set_font(s_detail_contexts_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_detail_contexts_label, GColorBabyBlueEyes);
  text_layer_set_text_color(s_detail_contexts_label, GColorBlack);
  text_layer_set_text_alignment(s_detail_contexts_label, GTextAlignmentCenter);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_contexts_label));
  y += 20;

  y += 4; // small gap

  // Project (may be hidden)
  s_detail_project_label = text_layer_create(GRect(4, y, w, 18));
  text_layer_set_font(s_detail_project_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_project_label));
  y += 20;

  // Due date (may be hidden)
  s_detail_due_label = text_layer_create(GRect(4, y, w, 18));
  text_layer_set_font(s_detail_due_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_due_label));
  y += 22;

  // Notes (scrollable)
  s_detail_note_label = text_layer_create(GRect(4, y, w, 120));
  text_layer_set_font(s_detail_note_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_overflow_mode(s_detail_note_label, GTextOverflowModeWordWrap);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_note_label));

  scroll_layer_set_content_size(s_detail_scroll_layer, GSize(W, 300));

  // Use manual click config (not scroll_layer_set_click_config_onto_window)
  // so we can intercept SELECT to toggle the action bar
  window_set_click_config_provider(window, scroll_click_config);

  s_action_mode = false;
  detail_refresh_layers();
}

static void detail_window_unload(Window *window) {
  action_bar_layer_destroy(s_detail_action_bar);
  scroll_layer_destroy(s_detail_scroll_layer);
  text_layer_destroy(s_detail_title_layer);
  text_layer_destroy(s_detail_status_label);
  text_layer_destroy(s_detail_project_label);
  text_layer_destroy(s_detail_areas_label);
  text_layer_destroy(s_detail_contexts_label);
  text_layer_destroy(s_detail_due_label);
  text_layer_destroy(s_detail_note_label);
  text_layer_destroy(s_detail_status_bar_layer);
}

static void detail_window_push(void) {
  window_stack_push(s_detail_window, true);
}

// ==================== TASKS LIST WINDOW ====================

static AppTimer *s_scroll_timer = NULL;
static int s_scroll_offset = 0;

static void scroll_timer_handler(void *data) {
  s_scroll_offset++;
  if (s_tasks_menu_layer) {
    layer_mark_dirty(menu_layer_get_layer(s_tasks_menu_layer));
  }
  s_scroll_timer = app_timer_register(30, scroll_timer_handler, NULL);
}

static void start_scroll_timer(void) {
  s_scroll_offset = 0;
  if (!s_scroll_timer) {
    s_scroll_timer = app_timer_register(30, scroll_timer_handler, NULL);
  }
}

static void stop_scroll_timer(void) {
  if (s_scroll_timer) {
    app_timer_cancel(s_scroll_timer);
    s_scroll_timer = NULL;
  }
}

static void draw_scrolling_menu_cell(GContext *ctx, const Layer *cell_layer, const char *text, bool is_selected) {
  GRect bounds = layer_get_bounds(cell_layer);
  
  // Set text color matching the highlight state
  GColor text_color = is_selected ? GColorWhite : GColorBlack;
  graphics_context_set_text_color(ctx, text_color);
  
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  
  // Measure text width
  GSize text_size = graphics_text_layout_get_content_size(text, font, 
    GRect(0, 0, 1000, bounds.size.h), GTextOverflowModeFill, GTextAlignmentLeft);
  
  int text_margin = 8;
  int available_width = bounds.size.w - (text_margin * 2);
  
  if (is_selected && text_size.w > available_width) {
    int max_scroll = text_size.w - available_width;
    int scroll_ticks = (max_scroll + 1) / 2; // Move 2 pixels per tick, so we need half as many ticks
    
    // Smooth scroll configuration - scaled to 30ms ticks
    int pause_start = 17; // ~0.5 seconds pause at start (17 * 30ms)
    int pause_end = 40;   // ~1.2 seconds pause at end (40 * 30ms)
    int reset_gap = 30;   // Pause before wrap-around
    int period = pause_start + scroll_ticks + pause_end + reset_gap;
    int tick = s_scroll_offset % period;
    
    int draw_offset = 0;
    if (tick < pause_start) {
      draw_offset = 0;
    } else if (tick < pause_start + scroll_ticks) {
      draw_offset = (tick - pause_start) * 2;
      if (draw_offset > max_scroll) {
        draw_offset = max_scroll;
      }
    } else {
      draw_offset = max_scroll;
    }
    
    // Draw the text horizontally offset by draw_offset
    GRect text_rect = GRect(text_margin - draw_offset, (bounds.size.h - 28) / 2, text_size.w, 28);
    graphics_draw_text(ctx, text, font, text_rect, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  } else {
    // Normal drawing with trailing ellipsis if not selected or fits
    GRect text_rect = GRect(text_margin, (bounds.size.h - 28) / 2, available_width, 28);
    graphics_draw_text(ctx, text, font, text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static uint16_t tasks_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_tasks;
}

static void tasks_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  bool is_selected = menu_cell_layer_is_highlighted(cell_layer);
  draw_scrolling_menu_cell(ctx, cell_layer, s_tasks[cell_index->row], is_selected);
}

static void tasks_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int row = cell_index->row;
  strncpy(s_detail_id,   s_task_ids[row],  MAX_TASK_ID_LENGTH - 1);
  strncpy(s_detail_name, s_tasks[row],     MAX_TASK_NAME_LENGTH - 1);
  s_detail_state = s_task_states[row];
  s_detail_note[0]    = '\0';
  s_detail_due[0]     = '\0';
  s_detail_contexts[0] = '\0';
  s_detail_areas[0]    = '\0';
  s_detail_project[0]  = '\0';

  detail_window_push();
  request_detail();
}

static void tasks_menu_selection_changed_callback(MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
  s_scroll_offset = 0;
}

static void tasks_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Top Status Bar
  s_tasks_status_bar_layer = text_layer_create(GRect(0, 0, bounds.size.w, 16));
  text_layer_set_text(s_tasks_status_bar_layer, "NirvanaHQ GTD");
  text_layer_set_text_alignment(s_tasks_status_bar_layer, GTextAlignmentCenter);
  text_layer_set_font(s_tasks_status_bar_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_background_color(s_tasks_status_bar_layer, GColorCobaltBlue);
  text_layer_set_text_color(s_tasks_status_bar_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_tasks_status_bar_layer));

  s_loading_layer = text_layer_create(GRect(0, (bounds.size.h - 16) / 2 + 16 - 10, bounds.size.w, 20));
  text_layer_set_text(s_loading_layer, "Loading tasks...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_loading_layer));

  s_tasks_menu_layer = menu_layer_create(GRect(0, 16, bounds.size.w, bounds.size.h - 16));
  menu_layer_set_callbacks(s_tasks_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = tasks_menu_get_num_rows_callback,
    .draw_row = tasks_menu_draw_row_callback,
    .select_click = tasks_menu_select_callback,
    .selection_changed = tasks_menu_selection_changed_callback,
  });
  menu_layer_set_click_config_onto_window(s_tasks_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_tasks_menu_layer));
  layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), s_is_loading);
}

static void tasks_window_unload(Window *window) {
  menu_layer_destroy(s_tasks_menu_layer);
  text_layer_destroy(s_loading_layer);
  text_layer_destroy(s_tasks_status_bar_layer);
}

static void tasks_window_appear(Window *window) {
  start_scroll_timer();
  
  switch (s_current_view) {
    case 0: text_layer_set_text(s_tasks_status_bar_layer, "Inbox"); break;
    case 1: text_layer_set_text(s_tasks_status_bar_layer, "Focus"); break;
    case 2: text_layer_set_text(s_tasks_status_bar_layer, "Next"); break;
    case 3: text_layer_set_text(s_tasks_status_bar_layer, "Waiting"); break;
    default: text_layer_set_text(s_tasks_status_bar_layer, "Tasks"); break;
  }

  if (s_js_ready) {
    request_tasks();
  } else {
    s_pending_task_request = true;
  }
}

static void tasks_window_disappear(Window *window) {
  stop_scroll_timer();
}

// ==================== MAIN MENU WINDOW ====================

static uint16_t main_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return 5;
}

static void main_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0: menu_cell_basic_draw(ctx, cell_layer, "New Task",     NULL, NULL); break;
    case 1: menu_cell_basic_draw(ctx, cell_layer, "View Inbox",   NULL, NULL); break;
    case 2: menu_cell_basic_draw(ctx, cell_layer, "View Focus",   NULL, NULL); break;
    case 3: menu_cell_basic_draw(ctx, cell_layer, "View Next",    NULL, NULL); break;
    case 4: menu_cell_basic_draw(ctx, cell_layer, "View Waiting", NULL, NULL); break;
  }
}

static void main_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0: {
      s_dictation_is_rename = false;
      DictionaryIterator *iter;
      app_message_outbox_begin(&iter);
      dict_write_uint8(iter, MESSAGE_KEY_AppKeyInitiateTaskCreation, 1);
      app_message_outbox_send();
      break;
    }
    case 1:
    case 2:
    case 3:
    case 4:
      s_current_view = cell_index->row - 1;
      window_stack_push(s_tasks_window, true);
      break;
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Top Status Bar
  s_main_status_bar_layer = text_layer_create(GRect(0, 0, bounds.size.w, 16));
  text_layer_set_text(s_main_status_bar_layer, "NirvanaHQ GTD");
  text_layer_set_text_alignment(s_main_status_bar_layer, GTextAlignmentCenter);
  text_layer_set_font(s_main_status_bar_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_background_color(s_main_status_bar_layer, GColorCobaltBlue);
  text_layer_set_text_color(s_main_status_bar_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_main_status_bar_layer));

  s_main_menu_layer = menu_layer_create(GRect(0, 16, bounds.size.w, bounds.size.h - 16));
  menu_layer_set_callbacks(s_main_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = main_menu_get_num_rows_callback,
    .draw_row = main_menu_draw_row_callback,
    .select_click = main_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_main_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_main_menu_layer));
}

static void main_window_unload(Window *window) {
  menu_layer_destroy(s_main_menu_layer);
  text_layer_destroy(s_main_status_bar_layer);
}

// ==================== APP MESSAGE ====================

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *ready_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyReady);
  Tuple *count_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyTaskCount);
  Tuple *index_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyTaskIndex);
  Tuple *name_tuple          = dict_find(iterator, MESSAGE_KEY_AppKeyTaskName);
  Tuple *state_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyTaskState);
  Tuple *id_tuple            = dict_find(iterator, MESSAGE_KEY_AppKeyTaskId);
  Tuple *start_dict_tuple    = dict_find(iterator, MESSAGE_KEY_AppKeyStartDictation);
  Tuple *detail_tuple        = dict_find(iterator, MESSAGE_KEY_AppKeyTaskDetail);
  Tuple *focus_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyTaskFocus);
  Tuple *note_tuple          = dict_find(iterator, MESSAGE_KEY_AppKeyTaskNote);
  Tuple *due_tuple           = dict_find(iterator, MESSAGE_KEY_AppKeyTaskDueDate);
  Tuple *tags_tuple          = dict_find(iterator, MESSAGE_KEY_AppKeyTaskTags);
  Tuple *areas_tuple         = dict_find(iterator, MESSAGE_KEY_AppKeyTaskAreas);
  Tuple *project_tuple       = dict_find(iterator, MESSAGE_KEY_AppKeyTaskProject);

  Tuple *list_count_tuple    = dict_find(iterator, MESSAGE_KEY_AppKeyListCount);
  Tuple *list_index_tuple    = dict_find(iterator, MESSAGE_KEY_AppKeyListIndex);
  Tuple *list_name_tuple     = dict_find(iterator, MESSAGE_KEY_AppKeyListItemName);
  Tuple *list_id_tuple       = dict_find(iterator, MESSAGE_KEY_AppKeyListItemId);

  // List data arriving
  if (list_count_tuple && list_index_tuple && list_name_tuple) {
    int index = list_index_tuple->value->int32;
    if (index == 0) s_list_num_items = 0;
    
    if (index < MAX_TASKS) {
      strncpy(s_list_items[index], list_name_tuple->value->cstring, MAX_TASK_NAME_LENGTH - 1);
      s_list_items[index][MAX_TASK_NAME_LENGTH - 1] = '\0';
      if (list_id_tuple) {
        strncpy(s_list_ids[index], list_id_tuple->value->cstring, MAX_TASK_ID_LENGTH - 1);
        s_list_ids[index][MAX_TASK_ID_LENGTH - 1] = '\0';
      }
      if (index + 1 > s_list_num_items) {
        s_list_num_items = index + 1;
      }
      if (s_list_menu_layer) {
        menu_layer_reload_data(s_list_menu_layer);
      }
    }
    return;
  }

  // Detail data arriving (new task created OR task selected)
  if (detail_tuple) {
    s_detail_name[0] = '\0';
    s_detail_id[0] = '\0';
    s_detail_note[0] = '\0';
    s_detail_due[0] = '\0';
    s_detail_contexts[0] = '\0';
    s_detail_areas[0] = '\0';
    s_detail_project[0] = '\0';

    if (name_tuple)    { strncpy(s_detail_name,    name_tuple->value->cstring,    MAX_TASK_NAME_LENGTH - 1); }
    if (id_tuple)      { strncpy(s_detail_id,      id_tuple->value->cstring,      MAX_TASK_ID_LENGTH - 1); }
    if (state_tuple)   { s_detail_state = state_tuple->value->int32; }
    if (focus_tuple)   { s_detail_focus = focus_tuple->value->int32; } else { s_detail_focus = 0; }
    if (note_tuple)    { strncpy(s_detail_note,    note_tuple->value->cstring,    DETAIL_BUF_SIZE - 1); }
    if (due_tuple)     { strncpy(s_detail_due,     due_tuple->value->cstring,     sizeof(s_detail_due) - 1); }
    if (tags_tuple)    { strncpy(s_detail_contexts,tags_tuple->value->cstring,    DETAIL_BUF_SIZE - 1); }
    if (areas_tuple)   { strncpy(s_detail_areas,   areas_tuple->value->cstring,   DETAIL_BUF_SIZE - 1); }
    if (project_tuple) { strncpy(s_detail_project, project_tuple->value->cstring, MAX_TASK_NAME_LENGTH - 1); }

    if (!window_stack_contains_window(s_detail_window)) {
      detail_window_push();
    } else {
      detail_refresh_layers();
    }
    return;
  }

  if (start_dict_tuple) {
    dictation_session_start(s_dictation_session);
    return;
  }

  if (ready_tuple) {
    s_js_ready = true;
    if (s_pending_task_request) {
      s_pending_task_request = false;
      request_tasks();
    }
    return;
  }

  if (count_tuple && index_tuple && name_tuple) {
    if (s_is_loading) {
      s_is_loading = false;
      if (s_loading_layer && s_tasks_menu_layer) {
        layer_set_hidden(text_layer_get_layer(s_loading_layer), true);
        layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), false);
      }
    }

    int index = index_tuple->value->int32;

    if (index == 0) {
      s_num_tasks = 0;
    }

    if (index < MAX_TASKS) {
      strncpy(s_tasks[index], name_tuple->value->cstring, MAX_TASK_NAME_LENGTH - 1);
      s_tasks[index][MAX_TASK_NAME_LENGTH - 1] = '\0';

      if (id_tuple) {
        strncpy(s_task_ids[index], id_tuple->value->cstring, MAX_TASK_ID_LENGTH - 1);
        s_task_ids[index][MAX_TASK_ID_LENGTH - 1] = '\0';
      }

      if (state_tuple) {
        s_task_states[index] = state_tuple->value->int32;
      }

      if (index + 1 > s_num_tasks) {
        s_num_tasks = index + 1;
      }
      if (s_tasks_menu_layer) {
        menu_layer_reload_data(s_tasks_menu_layer);
      }
    }
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// ==================== INIT / DEINIT ====================

static void init(void) {
  // Load action bar icons from bundled PNG resources
  s_icon_complete = gbitmap_create_with_resource(RESOURCE_ID_ACTION_COMPLETE);
  s_icon_edit     = gbitmap_create_with_resource(RESOURCE_ID_ACTION_EDIT);
  s_icon_status   = gbitmap_create_with_resource(RESOURCE_ID_ACTION_STATUS);

  // Main window
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });

  // Edit menu window
  s_edit_window = window_create();
  window_set_window_handlers(s_edit_window, (WindowHandlers){
    .load   = edit_window_load,
    .unload = edit_window_unload,
  });

  // List window
  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers){
    .load   = list_window_load,
    .unload = list_window_unload,
  });

  // Tasks list window
  s_tasks_window = window_create();
  window_set_window_handlers(s_tasks_window, (WindowHandlers){
    .load      = tasks_window_load,
    .unload    = tasks_window_unload,
    .appear    = tasks_window_appear,
    .disappear = tasks_window_disappear,
  });

  // Detail window
  s_detail_window = window_create();
  window_set_window_handlers(s_detail_window, (WindowHandlers){
    .load   = detail_window_load,
    .unload = detail_window_unload,
  });

  // Status picker window
  s_status_window = window_create();
  window_set_window_handlers(s_status_window, (WindowHandlers){
    .load   = status_window_load,
    .unload = status_window_unload,
  });

  // Done animation window
  s_done_window = window_create();
  window_set_window_handlers(s_done_window, (WindowHandlers){
    .load   = done_window_load,
    .unload = done_window_unload,
    .appear = done_window_appear,
  });

  // Dictation
  s_dictation_session = dictation_session_create(sizeof(s_dictation_text),
                                                  dictation_session_callback, NULL);

  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  const int inbox_size  = 1024;
  const int outbox_size = 256;
  app_message_open(inbox_size, outbox_size);
}

static void deinit(void) {
  stop_scroll_timer();
  gbitmap_destroy(s_icon_complete);
  window_destroy(s_done_window);
  window_destroy(s_status_window);
  window_destroy(s_edit_window);
  window_destroy(s_list_window);
  dictation_session_destroy(s_dictation_session);
  window_destroy(s_main_window);
  window_destroy(s_tasks_window);
  window_destroy(s_detail_window);
  window_destroy(s_status_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
