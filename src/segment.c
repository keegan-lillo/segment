#include <pebble.h>

const int SEGMENT_SIZE = 15;
const int OUTER_SEGMENT_RATIO = 6890; // divided by 1000 later
const int INNER_SEGMENT_RATIO = 4445; // divided by 1000 later
const int INNER_BLOCK_RATIO = 2223;

typedef void (*AnimationAllPhasesCompleteCallback)(void);

struct Colors {
  GColor background;
  GColor hour;
  GColor min_left;
  GColor min_right;
};

enum APP_MESSAGE_KEYS {
  PRESET,
  COLOR_BACKGROUND,
  COLOR_HOUR,
  COLOR_MIN_LEFT,
  COLOR_MIN_RIGHT,
};

static Window *s_window;
static struct tm *s_time;

// max width or height we can make our shapes fit in
static int s_base_radius;
static int s_hour_inner_radius;
static int s_hour_inset;
static int s_minute_inner_radius;
static int s_minute_inset;
static int s_minute_block_size;
static GPoint s_center;
static GRect s_hour_rect;
static GRect s_minute_rect;

static Animation *s_animation;
static AnimationImplementation s_animation_implementation;
static int s_animation_progress;
static bool s_animation_running = false;
static int s_animation_phase = 0;
static int s_animation_target_phase = 0;
static AnimationAllPhasesCompleteCallback s_animation_callback;
static int s_animation_phase_timing[2] = {
  400,
  500
};

static int s_hour_animation_scale[12] = {
  450,
  700,
  450,
  425,
  400,
  450,
  300,
  750,
  300,
  300,
  275,
  325
};

static struct Colors s_colors;

// DECLARATIONS
static void next_animation(bool reverse);

static int32_t get_segment_angle(int segment) {
  return segment * SEGMENT_SIZE * TRIG_MAX_ANGLE / 360;
}

static int animation_distance(int64_t start, int64_t end) {
  return start
    + (s_animation_progress - ANIMATION_NORMALIZED_MIN)
    * (end - start)
    / (ANIMATION_NORMALIZED_MAX - ANIMATION_NORMALIZED_MIN);
}

static void draw_background(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_colors.background);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void draw_outer_expand(GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_colors.hour);
  graphics_fill_radial(
    ctx,
    grect_inset(s_hour_rect, GEdgeInsets(animation_distance(s_base_radius - 7, 0))),
    GOvalScaleModeFillCircle,
    s_base_radius,
    get_segment_angle(0),
    get_segment_angle(24)
  );
}

static void draw_outer_reveal(GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_colors.hour);
  graphics_fill_radial(
    ctx,
    s_hour_rect,
    GOvalScaleModeFillCircle,
    animation_distance(s_base_radius, s_hour_inset),
    get_segment_angle(0),
    get_segment_angle(24)
  );
}

static void draw_hour_outer_segment(GContext *ctx, int start, int end, int anim_start, bool swap_direction) {
  graphics_context_set_fill_color(ctx, s_colors.hour);

  if (swap_direction) {
    graphics_fill_radial(
      ctx,
      s_hour_rect,
      GOvalScaleModeFillCircle,
      s_hour_inset,
      animation_distance(get_segment_angle(anim_start), get_segment_angle(start)),
      get_segment_angle(end)
    );
  } else {
    graphics_fill_radial(
      ctx,
      s_hour_rect,
      GOvalScaleModeFillCircle,
      s_hour_inset,
      get_segment_angle(start),
      animation_distance(get_segment_angle(anim_start), get_segment_angle(end))
    );
  }
}

static void draw_hour_inner_segment(GContext *ctx, int start, int end) {
  graphics_context_set_fill_color(ctx, s_colors.hour);

  graphics_fill_radial(
    ctx,
    s_hour_rect,
    GOvalScaleModeFillCircle,
    s_base_radius,
    get_segment_angle(start),
    get_segment_angle(end)
  );
}

static void _draw_min_outer_segment(GContext *ctx, GColor color, int start, int end) {
  graphics_context_set_fill_color(ctx, color);
//  APP_LOG(APP_LOG_LEVEL_INFO, "s_hour_rect.size.w: %d", s_hour_inset);
  graphics_fill_radial(
    ctx,
    s_minute_rect,
    GOvalScaleModeFillCircle,
    s_minute_inset,
    get_segment_angle(start),
    get_segment_angle(end)
  );
}

static void draw_right_min_outer_segment(GContext *ctx, int start, int end) {
  _draw_min_outer_segment(ctx, s_colors.min_right, start, end);
}

static void draw_left_min_outer_segment(GContext *ctx, int start, int end) {
  _draw_min_outer_segment(ctx, s_colors.min_left, start, end);
}

static void _draw_min_inner_block(GContext *ctx, GColor color, GRect rect) {
  if (rect.size.w < 0) {
    rect.size.w = abs(rect.size.w);
    rect.origin.x = rect.origin.x - rect.size.w;
  }

  if (rect.size.h < 0) {
    rect.size.h = abs(rect.size.h);
    rect.origin.y = rect.origin.y - rect.size.h;
  }

  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

static void draw_right_min_inner_block_from_center(GContext *ctx, int width, int height) {
  _draw_min_inner_block(ctx, s_colors.min_right, GRect(s_center.x, s_center.y, width, height));
}

static void draw_left_min_inner_block_from_center(GContext *ctx, int width, int height) {
  _draw_min_inner_block(ctx, s_colors.min_left, GRect(s_center.x, s_center.y, width, height));
}

static void draw_min_right_inner_block_custom(GContext *ctx, GRect rect) {
  _draw_min_inner_block(ctx, s_colors.min_right, rect);
}

static void draw_left_min_inner_block_custom(GContext *ctx, GRect rect) {
  _draw_min_inner_block(ctx, s_colors.min_left, rect);
}

static void draw_outer(GContext *ctx) {

  switch (s_time->tm_hour) {
    case 1 :
      draw_hour_outer_segment(ctx, 14, 22, -2, true);
      break;

    case 2 :
      draw_hour_outer_segment(ctx, -3, 4, -9, true);
      draw_hour_outer_segment(ctx, 9, 16, 3, true);
      break;

    case 3 :
      draw_hour_outer_segment(ctx, -3, 15, 21, false);
      break;

    case 4 :
      draw_hour_outer_segment(ctx, 5, 7, 1, true);
      draw_hour_outer_segment(ctx, 11, 13, 7, true);
      draw_hour_outer_segment(ctx, 17, 25, 13, true);
      break;

    case 5 :
      draw_hour_outer_segment(ctx, -4, 3, 10, false);
      draw_hour_outer_segment(ctx, 9, 15, 20, false);
      break;

    case 6 :
      draw_hour_outer_segment(ctx, 7, 27, 31, false);
      break;

    case 7 :
      draw_hour_outer_segment(ctx, -3, 3, -9, true);
      draw_hour_outer_segment(ctx, 13, 15, 3, true);
      break;

    case 8 :
      draw_hour_outer_segment(ctx, -4, 4, -8, true);
      draw_hour_outer_segment(ctx, 8, 16, 4, true);
      break;

    case 9 :
      draw_hour_outer_segment(ctx, -5, 15, 19, false);
      break;

    case 10 :
      draw_hour_outer_segment(ctx, -6, -2, 1, false);
      draw_hour_outer_segment(ctx, 14, 18, 11, true);
      draw_hour_outer_segment(ctx, 1, 11, 11, false);
      break;

    case 11 :
      draw_hour_outer_segment(ctx, 2, 10, 14, false);
      draw_hour_outer_segment(ctx, 14, 22, 26, false);
      break;

    case 0 :
      draw_hour_outer_segment(ctx, -2, 3, -3, true);
      draw_hour_outer_segment(ctx, 16, 21, 15, true);
      draw_hour_outer_segment(ctx, 9, 15, 3, true);
      break;
  }
}

static void draw_inner(GContext *ctx) {
  switch (s_time->tm_min / 10) {
    case 0 :
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, s_minute_inner_radius);
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, -s_minute_inner_radius);
      draw_left_min_outer_segment(ctx, -12, 0);
      break;

    case 1 :
      draw_left_min_outer_segment(ctx, -10, -2);
      break;

    case 2 :
      draw_left_min_inner_block_from_center(ctx, -s_minute_inner_radius, s_minute_block_size);
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, -s_minute_inner_radius);
      draw_left_min_outer_segment(ctx, -4, 0);
      draw_left_min_outer_segment(ctx, -12, -6);
      break;

    case 3 :
      draw_left_min_inner_block_custom(ctx, GRect(
        s_center.x - s_minute_block_size,
        s_center.y - s_minute_block_size / 2,
        -s_minute_block_size,
        s_minute_block_size
      ));
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, s_minute_inner_radius);
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, -s_minute_inner_radius);
      draw_left_min_outer_segment(ctx, -4, 0);
      draw_left_min_outer_segment(ctx, 12, 16);
      break;

    case 4 :
      draw_left_min_inner_block_custom(ctx, GRect(
        s_center.x,
        s_center.y - s_minute_block_size / 5,
        -s_hour_inner_radius + s_minute_block_size / 5,
        s_minute_block_size
      ));
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, s_minute_inner_radius);
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, -s_minute_inner_radius);
      draw_left_min_outer_segment(ctx, -7, -3);
      break;

    case 5 :
      draw_left_min_inner_block_from_center(ctx, -s_minute_inner_radius, -s_minute_block_size);
      draw_left_min_inner_block_from_center(ctx, -s_minute_block_size, s_minute_inner_radius);
      draw_left_min_outer_segment(ctx, -6, 0);
      draw_left_min_outer_segment(ctx, -12, -8);
      break;
  }

  switch (s_time->tm_min % 10) {
    case 0 :
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, s_minute_inner_radius);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      draw_right_min_outer_segment(ctx, 0, 12);
      break;

    case 1 :
      draw_right_min_outer_segment(ctx, 2, 10);
      break;

    case 2 :
      draw_right_min_outer_segment(ctx, 0, 6);
      draw_right_min_outer_segment(ctx, 8, 12);
      draw_right_min_inner_block_from_center(ctx, s_minute_inner_radius, -s_minute_block_size);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, s_minute_inner_radius);
      break;

    case 3 :
      draw_right_min_outer_segment(ctx, 1, 11);
      draw_min_right_inner_block_custom(ctx, GRect(
        s_center.x + s_minute_inner_radius,
        s_center.y - s_minute_block_size / 2,
        -s_minute_block_size,
        s_minute_block_size
      ));
      break;

    case 4 :
      draw_right_min_outer_segment(ctx, 3, 10);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      draw_min_right_inner_block_custom(ctx, GRect(
        s_center.x,
        s_center.y - s_minute_block_size / 5,
        s_minute_inner_radius,
        s_minute_block_size
      ));
      break;

    case 5 :
      draw_right_min_outer_segment(ctx, 0, 4);
      draw_right_min_outer_segment(ctx, 6, 12);
      draw_right_min_inner_block_from_center(ctx, s_minute_inner_radius, s_minute_block_size);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      break;

    case 6 :
      draw_right_min_outer_segment(ctx, 6, 12);
      draw_right_min_inner_block_from_center(ctx, s_minute_inner_radius, s_minute_block_size);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, s_minute_inner_radius);
      break;

    case 7 :
      draw_min_right_inner_block_custom(ctx, GRect(
        s_center.x + s_minute_inner_radius,
        s_center.y,
        -s_minute_block_size,
        -s_minute_block_size
      ));
      draw_right_min_outer_segment(ctx, 0, 10);
      break;

    case 8 :
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, s_minute_inner_radius);
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      draw_min_right_inner_block_custom(ctx, GRect(
        s_center.x,
        s_center.y - s_minute_block_size / 2,
        s_minute_inner_radius,
        s_minute_block_size
      ));
      draw_right_min_outer_segment(ctx, 0, 12);
      break;

    case 9 :
      draw_right_min_inner_block_from_center(ctx, s_minute_block_size, -s_minute_inner_radius);
      draw_min_right_inner_block_custom(ctx, GRect(
        s_center.x,
        s_center.y - s_minute_block_size / 2,
        s_minute_inner_radius,
        s_minute_block_size
      ));
      draw_right_min_outer_segment(ctx, 0, 12);
      break;

  }

  switch (s_time->tm_hour) {
    case 1 :
      break;

    case 2 :
      draw_hour_inner_segment(ctx, 2, 4);
      draw_hour_inner_segment(ctx, 14, 16);
      break;

    case 3 :
      draw_hour_inner_segment(ctx, 5, 7);
      break;

    case 4 :
      draw_hour_inner_segment(ctx, -1, 1);
      draw_hour_inner_segment(ctx, 5, 7);
      draw_hour_inner_segment(ctx, 11, 13);
      draw_hour_inner_segment(ctx, 17, 19);
      break;

    case 5 :
      draw_hour_inner_segment(ctx, -4, -2);
      draw_hour_inner_segment(ctx, 8, 10);
      break;

    case 6 :
      draw_hour_inner_segment(ctx, 7, 9);
      draw_hour_inner_segment(ctx, 15, 17);
      break;

    case 7 :
      draw_hour_inner_segment(ctx, 1, 3);
      draw_hour_inner_segment(ctx, 13, 15);
      break;

    case 8 :
      draw_hour_inner_segment(ctx, -4, -2);
      draw_hour_inner_segment(ctx, 2, 4);
      draw_hour_inner_segment(ctx, 14, 16);
      draw_hour_inner_segment(ctx, 8, 10);
      break;

    case 9 :
      draw_hour_inner_segment(ctx, -5, -3);
      draw_hour_inner_segment(ctx, 3, 5);
      break;

    case 10 :
      draw_hour_inner_segment(ctx, 1, 3);
      draw_hour_inner_segment(ctx, 9, 11);
      break;

    case 0 :
      draw_hour_inner_segment(ctx, 1, 3);
      draw_hour_inner_segment(ctx, 13, 15);
      break;
  }
}


static void update_layer(Layer *layer, GContext *ctx) {
  draw_background(layer, ctx);

  switch (s_animation_phase) {
    case 0 :
      draw_outer_expand(ctx);
      break;

    case 1 :
      draw_inner(ctx);
      draw_outer_reveal(ctx);
      break;

    case 2 :
      draw_inner(ctx);
      draw_outer(ctx);
      break;
  }
}

static void update_animation(Animation *animation, AnimationProgress progress) {
  s_animation_progress = progress;
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void on_animation_stopped(Animation *animation, bool stopped, void *ctx) {
  if(s_animation_phase < s_animation_target_phase) {
    s_animation_progress = ANIMATION_NORMALIZED_MIN;
    ++s_animation_phase;
    next_animation(false);
  } else if(s_animation_phase > s_animation_target_phase) {
    s_animation_progress = ANIMATION_NORMALIZED_MAX;
    --s_animation_phase;
    next_animation(true);
  } else {
    s_animation_running = false;
    if(s_animation_callback) {
      s_animation_callback();
    }
  }
}

static int get_animation_timing() {
  if(s_animation_phase == 2) {
    return s_hour_animation_scale[s_time->tm_hour];
  } else {
    return s_animation_phase_timing[s_animation_phase];
  }
}

static void next_animation(bool reverse) {
  s_animation = animation_create();
  s_animation_implementation.update = update_animation;
  animation_set_duration(s_animation, get_animation_timing());
  animation_set_reverse(s_animation, reverse);
  animation_set_curve(s_animation, AnimationCurveEaseInOut);
  animation_set_implementation(s_animation, &s_animation_implementation);
  animation_set_handlers(s_animation, (AnimationHandlers) {
    .stopped = on_animation_stopped
  }, NULL);

  animation_schedule(s_animation);
}

static void animate_to_phase(int phase, AnimationAllPhasesCompleteCallback callback) {
  if(s_animation_running) {
    return;
  }
  s_animation_running = true;
  s_animation_callback = callback;
  s_animation_target_phase = phase;
  next_animation(phase < s_animation_phase);
}

static void main_window_unload(Window *window) {

}

static void set_time(void) {
  // Get a tm structure
  time_t temp = time(NULL);
  s_time = localtime(&temp);
  s_time->tm_hour = s_time->tm_hour % 12;
//  s_time->tm_hour = 1;
//  s_time->tm_min = 8;
  animate_to_phase(2, NULL);
}

static void update_time() {
  if(s_animation_phase == 2) {
    animate_to_phase(1, set_time);
  } else {
    set_time();
    animate_to_phase(2, NULL);
  }

}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  s_base_radius = (window_bounds.size.w < window_bounds.size.h ? window_bounds.size.w : window_bounds.size.h) / 2;
  s_hour_inner_radius = s_base_radius * OUTER_SEGMENT_RATIO / 10000;
  s_hour_inset = s_base_radius - s_hour_inner_radius + 1; // add 1 extra pixel to stop weird gaps in antialiasing
  s_minute_inner_radius = s_base_radius * INNER_SEGMENT_RATIO / 10000;
  s_minute_inset = s_hour_inner_radius - s_minute_inner_radius;
  s_minute_block_size = s_base_radius * INNER_BLOCK_RATIO / 10000;
  s_center = GPoint(window_bounds.size.w / 2,  window_bounds.size.h / 2);
  s_hour_rect = GRect(window_bounds.size.w / 2 - s_base_radius,
                    window_bounds.size.h / 2 - s_base_radius,
                    s_base_radius * 2,
                    s_base_radius * 2);
  s_minute_rect = GRect(window_bounds.size.w / 2 - s_hour_inner_radius,
                        window_bounds.size.h / 2 - s_hour_inner_radius,
                        s_hour_inner_radius * 2,
                        s_hour_inner_radius * 2);
  APP_LOG(APP_LOG_LEVEL_INFO, "s_hour_rect: %d", s_hour_rect.size.w);
  APP_LOG(APP_LOG_LEVEL_INFO, "s_minute_inner_radius: %d", s_minute_inner_radius);
  APP_LOG(APP_LOG_LEVEL_INFO, "s_minute_inset: %d", s_minute_inset);
  layer_set_update_proc(window_get_root_layer(window), update_layer);

}

static void did_focus(bool in_focus) {
  if(!in_focus) { return; }

  app_focus_service_unsubscribe();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  update_time();
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Incoming message dropped: %d", reason);
}

void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *tuple;

  tuple = dict_find(received, COLOR_BACKGROUND);
  if(tuple) {
    s_colors.background = GColorFromHEX(tuple->value->int32);
    persist_write_int(COLOR_BACKGROUND, tuple->value->int32);
  }
  tuple = dict_find(received, COLOR_HOUR);
  if(tuple) {
    s_colors.hour = GColorFromHEX(tuple->value->int32);
    persist_write_int(COLOR_HOUR, tuple->value->int32);
  }
  tuple = dict_find(received, COLOR_MIN_LEFT);
  if(tuple) {
    s_colors.min_left = GColorFromHEX(tuple->value->int32);
    persist_write_int(COLOR_MIN_LEFT, tuple->value->int32);
  }
  tuple = dict_find(received, COLOR_MIN_RIGHT);
  if(tuple) {
    s_colors.min_right = GColorFromHEX(tuple->value->int32);
    persist_write_int(COLOR_MIN_RIGHT, tuple->value->int32);
  }

  layer_mark_dirty(window_get_root_layer(s_window));

}

static void init() {
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_inbox_received(in_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  // Create main Window element and assign to pointer
  s_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  app_focus_service_subscribe_handlers((AppFocusHandlers) {
    .did_focus = did_focus
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_window, true);
  s_colors.background = GColorFromHEX(persist_exists(COLOR_BACKGROUND) ? persist_read_int(COLOR_BACKGROUND) : 0x000055);
  s_colors.hour = GColorFromHEX(persist_exists(COLOR_HOUR) ? persist_read_int(COLOR_HOUR) : 0x0055ff);
  s_colors.min_left = GColorFromHEX(persist_exists(COLOR_MIN_LEFT) ? persist_read_int(COLOR_MIN_LEFT) :0x55aaff);
  s_colors.min_right = GColorFromHEX(persist_exists(COLOR_MIN_RIGHT) ? persist_read_int(COLOR_MIN_RIGHT) : 0xaaffff);
}

static void deinit() {
  // Destroy Window
  APP_LOG(APP_LOG_LEVEL_INFO, "DESTROY deinit %d", (int)s_animation);
  if(s_animation) {
    animation_destroy(s_animation);
    s_animation = NULL;
  }

  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
