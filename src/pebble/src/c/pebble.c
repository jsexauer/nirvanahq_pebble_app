#include <pebble.h>

#define MAX_TASKS 20
#define MAX_TASK_NAME_LENGTH 64

static Window *s_main_window;
static MenuLayer *s_main_menu_layer;

static Window *s_tasks_window;
static MenuLayer *s_tasks_menu_layer;
static TextLayer *s_loading_layer;

static Window *s_success_window;
static TextLayer *s_success_text_layer;

static DictationSession *s_dictation_session;
static char s_dictation_text[512];

static char s_tasks[MAX_TASKS][MAX_TASK_NAME_LENGTH];
static int s_task_states[MAX_TASKS];
static int s_num_tasks = 0;
static bool s_is_loading = true;
static bool s_js_ready = false;
static bool s_pending_task_request = false;
static int s_current_view = 0; // 0=Inbox, 1=Focus, 2=Next, 3=Waiting

static void request_tasks() {
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

static void success_timer_callback(void *data);

static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_cstring(iter, MESSAGE_KEY_AppKeyCreateTask, transcription);
    app_message_outbox_send();
  }
}

static uint16_t tasks_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_tasks;
}

static void tasks_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_tasks[cell_index->row], NULL, NULL);
}

static uint16_t main_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return 5;
}

static void main_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell_layer, "New Task", NULL, NULL);
      break;
    case 1:
      menu_cell_basic_draw(ctx, cell_layer, "View Inbox", NULL, NULL);
      break;
    case 2:
      menu_cell_basic_draw(ctx, cell_layer, "View Focus", NULL, NULL);
      break;
    case 3:
      menu_cell_basic_draw(ctx, cell_layer, "View Next", NULL, NULL);
      break;
    case 4:
      menu_cell_basic_draw(ctx, cell_layer, "View Waiting", NULL, NULL);
      break;
  }
}

static void main_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  switch (cell_index->row) {
    case 0:
      {
        DictionaryIterator *iter;
        app_message_outbox_begin(&iter);
        dict_write_uint8(iter, MESSAGE_KEY_AppKeyInitiateTaskCreation, 1);
        app_message_outbox_send();
      }
      break;
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

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *ready_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyReady);
  Tuple *count_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyTaskCount);
  Tuple *index_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyTaskIndex);
  Tuple *name_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyTaskName);
  Tuple *state_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyTaskState);
  Tuple *start_dictation_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyStartDictation);
  Tuple *success_tuple = dict_find(iterator, MESSAGE_KEY_AppKeyTaskCreatedSuccess);

  if (success_tuple) {
    window_stack_push(s_success_window, true);
    app_timer_register(2000, success_timer_callback, NULL);
    return;
  }

  if (start_dictation_tuple) {
    dictation_session_start(s_dictation_session);
    return;
  }

  if(ready_tuple) {
    s_js_ready = true;
    if (s_pending_task_request) {
      s_pending_task_request = false;
      request_tasks();
    }
    return;
  }

  if(count_tuple && index_tuple && name_tuple) {
    if (s_is_loading) {
      s_is_loading = false;
      if (s_loading_layer && s_tasks_menu_layer) {
        layer_set_hidden(text_layer_get_layer(s_loading_layer), true);
        layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), false);
      }
    }
    
    int index = index_tuple->value->int32;
    // int count = count_tuple->value->int32;
    
    if (index == 0) {
      s_num_tasks = 0; // Reset on first task
    }
    
    if (index < MAX_TASKS) {
      strncpy(s_tasks[index], name_tuple->value->cstring, MAX_TASK_NAME_LENGTH - 1);
      s_tasks[index][MAX_TASK_NAME_LENGTH - 1] = '\0';
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
  });
  menu_layer_set_click_config_onto_window(s_tasks_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_tasks_menu_layer));
  layer_set_hidden(menu_layer_get_layer(s_tasks_menu_layer), s_is_loading);
}

static void tasks_window_unload(Window *window) {
  menu_layer_destroy(s_tasks_menu_layer);
  text_layer_destroy(s_loading_layer);
}

static void success_timer_callback(void *data) {
  window_stack_remove(s_success_window, true);
}

static void success_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_success_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 20));
  text_layer_set_text(s_success_text_layer, "Task Added!");
  text_layer_set_text_alignment(s_success_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_success_text_layer));
}

static void success_window_unload(Window *window) {
  text_layer_destroy(s_success_text_layer);
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

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  s_tasks_window = window_create();
  window_set_window_handlers(s_tasks_window, (WindowHandlers) {
    .load = tasks_window_load,
    .unload = tasks_window_unload,
  });

  s_success_window = window_create();
  window_set_window_handlers(s_success_window, (WindowHandlers) {
    .load = success_window_load,
    .unload = success_window_unload,
  });

  s_dictation_session = dictation_session_create(sizeof(s_dictation_text), dictation_session_callback, NULL);
  window_stack_push(s_main_window, true);

  // Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  // Open AppMessage
  const int inbox_size = 512;
  const int outbox_size = 128;
  app_message_open(inbox_size, outbox_size);
}

static void deinit() {
  window_destroy(s_main_window);
  window_destroy(s_tasks_window);
  window_destroy(s_success_window);
  dictation_session_destroy(s_dictation_session);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
