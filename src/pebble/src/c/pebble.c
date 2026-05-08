#include <pebble.h>
#include <string.h>

#define MAX_TASKS 20
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
static TextLayer *s_detail_tags_label;
static TextLayer *s_detail_due_label;
static TextLayer *s_detail_status_label;
static TextLayer *s_detail_note_label;
static ActionBarLayer *s_detail_action_bar;
static GBitmap *s_icon_complete;
static GBitmap *s_icon_edit;
static GBitmap *s_icon_status;



// --- Status Picker Window ---
static Window *s_status_window;
static SimpleMenuLayer *s_status_menu_layer;
static SimpleMenuSection s_status_section;
static SimpleMenuItem s_status_items[5];

// --- Dictation ---
static DictationSession *s_dictation_session;
static char s_dictation_text[512];
static bool s_dictation_is_rename = false;
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
static char s_detail_tags[DETAIL_BUF_SIZE];
static char s_detail_project[MAX_TASK_NAME_LENGTH];
static int  s_detail_state = 0;

// Built display string buffers
static char s_buf_project[DETAIL_BUF_SIZE + 16];
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
      dict_write_cstring(iter, MESSAGE_KEY_AppKeyRenameTask, transcription);
      dict_write_cstring(iter, MESSAGE_KEY_AppKeyTaskId, s_detail_id);
      app_message_outbox_send();
    } else {
      app_message_outbox_begin(&iter);
      dict_write_cstring(iter, MESSAGE_KEY_AppKeyCreateTask, transcription);
      app_message_outbox_send();
    }
  }
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
  s_dictation_is_rename = true;
  dictation_session_start(s_dictation_session);
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
  // Title
  text_layer_set_text(s_detail_title_layer, s_detail_name);

  // Status: bold text, no prefix label
  text_layer_set_text(s_detail_status_label, state_name(s_detail_state));

  // Tags: no prefix label; show dash if empty
  text_layer_set_text(s_detail_tags_label, s_detail_tags[0] ? s_detail_tags : "-");

  // Project (hidden if empty)
  if (s_detail_project[0]) {
    snprintf(s_buf_project, sizeof(s_buf_project), "Project: %s", s_detail_project);
    text_layer_set_text(s_detail_project_label, s_buf_project);
    layer_set_hidden(text_layer_get_layer(s_detail_project_label), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_project_label), true);
  }

  // Due date (hidden if empty)
  if (s_detail_due[0]) {
    snprintf(s_buf_due, sizeof(s_buf_due), "Due: %s", s_detail_due);
    text_layer_set_text(s_detail_due_label, s_buf_due);
    layer_set_hidden(text_layer_get_layer(s_detail_due_label), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_detail_due_label), true);
  }

  // Notes
  text_layer_set_text(s_detail_note_label, s_detail_note[0] ? s_detail_note : "(no notes)");

  GRect sb = layer_get_bounds(scroll_layer_get_layer(s_detail_scroll_layer));
  scroll_layer_set_content_size(s_detail_scroll_layer, GSize(sb.size.w, 300));
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int W = bounds.size.w;

  // Scroll layer uses full window width — added FIRST so action bar renders on top
  s_detail_scroll_layer = scroll_layer_create(bounds);
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll_layer));

  // Action bar: overlay on right edge, added LAST so it's on top of scroll content.
  // Starts HIDDEN — shown only when user presses middle button.
  s_detail_action_bar = action_bar_layer_create();
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_UP,     s_icon_complete);
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_SELECT, s_icon_edit);
  action_bar_layer_set_icon(s_detail_action_bar, BUTTON_ID_DOWN,   s_icon_status);
  GRect bar_frame = GRect(W - ACTION_BAR_WIDTH, 0, ACTION_BAR_WIDTH, bounds.size.h);
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

  // Tags: orange background, right below status
  s_detail_tags_label = text_layer_create(GRect(0, y, W, 20));
  text_layer_set_font(s_detail_tags_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_detail_tags_label, GColorOrange);
  text_layer_set_text_color(s_detail_tags_label, GColorWhite);
  text_layer_set_text_alignment(s_detail_tags_label, GTextAlignmentCenter);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_tags_label));
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
  text_layer_destroy(s_detail_tags_label);
  text_layer_destroy(s_detail_due_label);
  text_layer_destroy(s_detail_note_label);
}

static void detail_window_push(void) {
  window_stack_push(s_detail_window, true);
}

// ==================== TASKS LIST WINDOW ====================

static uint16_t tasks_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_tasks;
}

static void tasks_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_tasks[cell_index->row], NULL, NULL);
}

static void tasks_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int row = cell_index->row;
  strncpy(s_detail_id,   s_task_ids[row],  MAX_TASK_ID_LENGTH - 1);
  strncpy(s_detail_name, s_tasks[row],     MAX_TASK_NAME_LENGTH - 1);
  s_detail_state = s_task_states[row];
  s_detail_note[0]    = '\0';
  s_detail_due[0]     = '\0';
  s_detail_tags[0]    = '\0';
  s_detail_project[0] = '\0';

  detail_window_push();
  request_detail();
}

static void tasks_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_loading_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 20));
  text_layer_set_text(s_loading_layer, "Loading tasks...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_loading_layer));

  s_tasks_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_tasks_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = tasks_menu_get_num_rows_callback,
    .draw_row = tasks_menu_draw_row_callback,
    .select_click = tasks_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_tasks_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_tasks_menu_layer));
  layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), s_is_loading);
}

static void tasks_window_unload(Window *window) {
  menu_layer_destroy(s_tasks_menu_layer);
  text_layer_destroy(s_loading_layer);
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
      if (s_js_ready) {
        request_tasks();
      } else {
        s_pending_task_request = true;
      }
      break;
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_main_menu_layer = menu_layer_create(bounds);
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
  Tuple *note_tuple          = dict_find(iterator, MESSAGE_KEY_AppKeyTaskNote);
  Tuple *due_tuple           = dict_find(iterator, MESSAGE_KEY_AppKeyTaskDueDate);
  Tuple *tags_tuple          = dict_find(iterator, MESSAGE_KEY_AppKeyTaskTags);
  Tuple *project_tuple       = dict_find(iterator, MESSAGE_KEY_AppKeyTaskProject);

  // Detail data arriving (new task created OR task selected)
  if (detail_tuple) {
    if (name_tuple)    { strncpy(s_detail_name,    name_tuple->value->cstring,    MAX_TASK_NAME_LENGTH - 1); }
    if (id_tuple)      { strncpy(s_detail_id,      id_tuple->value->cstring,      MAX_TASK_ID_LENGTH - 1); }
    if (state_tuple)   { s_detail_state = state_tuple->value->int32; }
    if (note_tuple)    { strncpy(s_detail_note,    note_tuple->value->cstring,    DETAIL_BUF_SIZE - 1); }
    if (due_tuple)     { strncpy(s_detail_due,     due_tuple->value->cstring,     sizeof(s_detail_due) - 1); }
    if (tags_tuple)    { strncpy(s_detail_tags,    tags_tuple->value->cstring,    DETAIL_BUF_SIZE - 1); }
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

  // Tasks list window
  s_tasks_window = window_create();
  window_set_window_handlers(s_tasks_window, (WindowHandlers){
    .load   = tasks_window_load,
    .unload = tasks_window_unload,
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
  gbitmap_destroy(s_icon_complete);
  gbitmap_destroy(s_icon_edit);
  gbitmap_destroy(s_icon_status);
  dictation_session_destroy(s_dictation_session);
  window_destroy(s_main_window);
  window_destroy(s_tasks_window);
  window_destroy(s_detail_window);
  window_destroy(s_status_window);
  window_destroy(s_done_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
