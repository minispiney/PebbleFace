#include <pebble.h>

static Window    *window;
static TextLayer *hours_layer;
static TextLayer *minutes_layer;
static TextLayer *seconds_layer;
static TextLayer *countdown_layer;

// Create long-lived buffers
static char hours_buffer[]   = "00:";
static char minutes_buffer[] = "00";
static char seconds_buffer[] = ":00";

static char event_name_buffer[] = "MOTD";
static char to_go_buffer[] = "  NOW days";
static char countdown_buffer[256];

// start timestamp for MINIS on the Dragon
// midnight on day 1
static time_t event_start = 1430265600;
// number of days - 1 (starts at 0)
static int    event_length = 4;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

static BitmapLayer *battery_layer;

static GBitmap *battery_basic;
static GBitmap *battery_20;
static GBitmap *battery_40;
static GBitmap *battery_60;
static GBitmap *battery_80;
static GBitmap *battery_100;
static GBitmap *battery_charging;
static GBitmap *battery_empty;
static GBitmap *battery_connected;

static BitmapLayer *bt_layer;
static GBitmap *bt_connected;
static GBitmap *bt_disconnected;

static BitmapLayer *ampm_layer;
static GBitmap *ampm_blank;
static GBitmap *ampm_am;
static GBitmap *ampm_pm;

void update_seconds() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  strftime(seconds_buffer, sizeof(":00"), ":%S", tick_time);
  // Display this time on the TextLayer
  text_layer_set_text(seconds_layer, seconds_buffer);
}

void update_minutes() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  strftime(minutes_buffer, sizeof("00"), "%M", tick_time);
  // Display this time on the TextLayer
  text_layer_set_text(minutes_layer, minutes_buffer);
}

void update_hours() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  // Write the current hours into the buffer
  if(clock_is_24h_style() == true) {
    // Use 24 hour format
    strftime(hours_buffer, sizeof("00:"), "%H:", tick_time);
    bitmap_layer_set_bitmap(ampm_layer, ampm_blank);
  } else {
    // Use 12 hour format
    char temp[] = "00:";
    strftime(temp, sizeof("00:"), "%I:", tick_time);
    if (temp[0] == '0')
      strncpy(hours_buffer,temp+1,3);
    else
      strncpy(hours_buffer,temp,3);
      
    if (tick_time->tm_hour > 11)
      bitmap_layer_set_bitmap(ampm_layer, ampm_pm);
    else
      bitmap_layer_set_bitmap(ampm_layer, ampm_am);
  }
  
  // Display this time on the TextLayer
  text_layer_set_text(hours_layer, hours_buffer);
}

void update_countdown() {
  time_t temp = time(NULL); 
  int diff = difftime(event_start,temp)/(60*60*24)+1;

  strncpy(countdown_buffer,event_name_buffer,sizeof(countdown_buffer));
  
  // positive diff means in the future
  if (diff > 1)
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%d days",diff);
  else if (diff == 1)
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%d day",diff);
  // diff between -length and 0 means event running
  else if ((diff >= (1-event_length)) && (diff <= 0))
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%s","NOW");
  //diff less than -length means over
  else if (diff < (1-event_length))
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%s","OVER");

  strncat(countdown_buffer,to_go_buffer,sizeof(countdown_buffer)-sizeof(to_go_buffer));

  // Display this countdown on the TextLayer
  text_layer_set_text(countdown_layer, countdown_buffer);
}

static void handle_battery(BatteryChargeState charge_state) {
  if (charge_state.is_charging)
    bitmap_layer_set_bitmap(battery_layer, battery_charging);
  else if (charge_state.is_plugged)
    bitmap_layer_set_bitmap(battery_layer, battery_connected);
  else if (charge_state.charge_percent >80)
    bitmap_layer_set_bitmap(battery_layer, battery_100);
  else if (charge_state.charge_percent >60)
    bitmap_layer_set_bitmap(battery_layer, battery_80);
  else if (charge_state.charge_percent >40)
    bitmap_layer_set_bitmap(battery_layer, battery_60);
  else if (charge_state.charge_percent >20)
    bitmap_layer_set_bitmap(battery_layer, battery_40);
  else if (charge_state.charge_percent >20)
    bitmap_layer_set_bitmap(battery_layer, battery_20);
  else
    bitmap_layer_set_bitmap(battery_layer, battery_empty);
}

static bool bt_connect_state;

static void handle_bluetooth(bool connected) {
  if (connected)
    bitmap_layer_set_bitmap(bt_layer, bt_connected);
  else
    bitmap_layer_set_bitmap(bt_layer, bt_disconnected);
}

static BatteryChargeState battery_state;

static void seconds_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (units_changed & HOUR_UNIT)   {
    update_countdown();
    update_hours();
  }
  if (units_changed & MINUTE_UNIT) update_minutes();
  if (units_changed & SECOND_UNIT) update_seconds();
}

void handle_init(void) {

  // Create a window and text layer
	window = window_create();

  // background
  // Create GBitmap, then set to created BitmapLayer
  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  s_background_layer = bitmap_layer_create(GRect(0, 0, 144, 168));
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(s_background_layer));

  // Battery Indicator
  // Create GBitmaps for empty battery, and all the other states
  battery_empty     = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_EMPTY);
  battery_20        = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_20);
  battery_40        = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_40);
  battery_60        = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_60);
  battery_80        = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_80);
  battery_100       = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_100);
  battery_connected = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CONNECTED);
  battery_charging  = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CHARGING);
  battery_basic     = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_BASIC);

  battery_layer = bitmap_layer_create(GRect(121, 0, 22, 10));
  bitmap_layer_set_compositing_mode(battery_layer,GCompOpAnd);
  bitmap_layer_set_bitmap(battery_layer, battery_basic);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(battery_layer));

  // bluetooth indicator
  // Create GBitmaps for connected/disconnected Bluetooth
  bt_connected = gbitmap_create_with_resource(RESOURCE_ID_BT_CONNECTED);
  bt_disconnected = gbitmap_create_with_resource(RESOURCE_ID_BT_DISCONNECTED);
  bt_layer = bitmap_layer_create(GRect(0, 0, 18, 10));
  bitmap_layer_set_compositing_mode(bt_layer,GCompOpAnd);
  bitmap_layer_set_bitmap(bt_layer, battery_basic);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(bt_layer));

  // AM/PM indicator
  // Create GBitmaps for blank/AM/PM
  ampm_blank = gbitmap_create_with_resource(RESOURCE_ID_AMPM_BLANK);
  ampm_am    = gbitmap_create_with_resource(RESOURCE_ID_AMPM_AM);
  ampm_pm    = gbitmap_create_with_resource(RESOURCE_ID_AMPM_PM);
  ampm_layer = bitmap_layer_create(GRect(0, 98-25, 23, 25));
  bitmap_layer_set_compositing_mode(ampm_layer,GCompOpAnd);
  bitmap_layer_set_bitmap(ampm_layer, ampm_blank);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(ampm_layer));

  // set up hours display
  hours_layer =   text_layer_create(GRect(24, 63, 49, 40));
	
	// Set the text, font, and text alignment
  text_layer_set_background_color(hours_layer, GColorClear);
  text_layer_set_text_color(hours_layer, GColorBlack);
	text_layer_set_font(hours_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
	text_layer_set_text_alignment(hours_layer, GTextAlignmentRight);

	// Add the text layer to the window
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(hours_layer));

  minutes_layer = text_layer_create(GRect(73, 63, 46, 40));

	// Set the text, font, and text alignment
  text_layer_set_background_color(minutes_layer, GColorClear);
  text_layer_set_text_color(minutes_layer, GColorBlack);
	text_layer_set_font(minutes_layer, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS ));
	text_layer_set_text_alignment(minutes_layer, GTextAlignmentLeft);

	layer_add_child(window_get_root_layer(window), text_layer_get_layer(minutes_layer));

  // set up seconds display
  seconds_layer = text_layer_create(GRect(119, 63, 25, 25));
	
	// Set the text, font, and text alignment
  text_layer_set_background_color(seconds_layer, GColorClear);
  text_layer_set_text_color(seconds_layer, GColorBlack);
	text_layer_set_font(seconds_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	text_layer_set_text_alignment(seconds_layer, GTextAlignmentLeft);

	// Add the text layer to the window
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(seconds_layer));

  // set up countdown display
  countdown_layer = text_layer_create(GRect(0, 96, 144, 72));
	
	// Set the text, font, and text alignment
  text_layer_set_background_color(countdown_layer, GColorClear);
  text_layer_set_text_color(countdown_layer, GColorBlack);
	text_layer_set_font(countdown_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK ));
	text_layer_set_text_alignment(countdown_layer, GTextAlignmentCenter);

	// Add the text layer to the window
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(countdown_layer));

  // register a handler for each one second tick
  tick_timer_service_subscribe(SECOND_UNIT, seconds_handler);
  
  // handler for battery events
  battery_state_service_subscribe(handle_battery);

  // handler for Bluetooth events
  bluetooth_connection_service_subscribe(handle_bluetooth);

	// Push the window
	window_stack_push(window, true);
  
  // update battery status
  battery_state = battery_state_service_peek();
  handle_battery(battery_state);

  // update Bluetooth indicator
  bt_connect_state = bluetooth_connection_service_peek();
  handle_bluetooth(bt_connect_state);
	
  // write initial time and countdown values
  update_hours();
  update_minutes();
  update_seconds();
  update_countdown();
 
  // App Logging!
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "Just pushed a window!");
}

void handle_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();

  // Destroy the text layers
	text_layer_destroy(countdown_layer);
  text_layer_destroy(seconds_layer);
	text_layer_destroy(minutes_layer);
	text_layer_destroy(hours_layer);
	
  // Destroy BitmapLayer
  bitmap_layer_destroy(battery_layer);
  bitmap_layer_destroy(bt_layer);
  bitmap_layer_destroy(ampm_layer);
  bitmap_layer_destroy(s_background_layer);

	// Destroy the window
	window_destroy(window);

  // Destroy GBitmaps
  gbitmap_destroy(bt_connected);
  gbitmap_destroy(bt_disconnected);
  
  gbitmap_destroy(battery_basic);
  gbitmap_destroy(battery_20);
  gbitmap_destroy(battery_40);
  gbitmap_destroy(battery_60);
  gbitmap_destroy(battery_80);
  gbitmap_destroy(battery_100);
  gbitmap_destroy(battery_charging);
  gbitmap_destroy(battery_empty);
  gbitmap_destroy(battery_connected);

  gbitmap_destroy(ampm_blank);
  gbitmap_destroy(ampm_am);
  gbitmap_destroy(ampm_pm);

  gbitmap_destroy(s_background_bitmap);
}

int main(void) {
	handle_init();
	app_event_loop();
	handle_deinit();
}
