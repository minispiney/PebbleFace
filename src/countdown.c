#include <pebble.h>

// defines
#define USE_SET_PEBBLE 1

#define STORAGE_KEY_OFFSET 64

// constants
enum Settings { setting_MOTD = 1, setting_AMVIV, setting_MOT,   setting_MME,
                setting_MACK,     setting_MITM,  setting_MandM, setting_MiF,
                setting_MSSD,     setting_MITO                               };

static Window    *window;
static TextLayer *hours_layer;
static TextLayer *minutes_layer;
static TextLayer *seconds_layer;
static TextLayer *countdown_layer;

#if USE_SET_PEBBLE
// for http://setpebble.com settings
static AppSync app;

static uint8_t buffer[256];

typedef enum {
  event_off = 0,
  event_on,
  event_count
} EventSetting;

//static enum SettingMOTD  { motd_off  = 0, motd_on,  motd_count  } motd;
static EventSetting motd;
static EventSetting amviv;
static EventSetting mot;
static EventSetting mme;
static EventSetting mack;
static EventSetting mitm;
static EventSetting mandm;
static EventSetting mif;
static EventSetting mssd;
static EventSetting mito;
#endif

// Create long-lived buffers
static char hours_buffer[]   = "00:";
static char minutes_buffer[] = "00";
static char seconds_buffer[] = ":00";

static char to_go_buffer[] = "  NOW days";
static char countdown_buffer[256];

static unsigned int event_index = 0;

typedef struct {
  uint8_t id;
  bool    enabled;
  uint8_t length;
  time_t  start;
  char   *name;
} event_data;

static event_data event[] = {
  // Timestamp should be 00:00 on the first day
  // Get the timestamp from http://www.epochconverter.com/
  // Length is the number of days in the event, counting first and last
  
  // Don't change the ID of any event.  Add new one events in time order
  // to the list, and pick the next ID available so that the
  // persistent storage data will be retained from other releases
  // Each event must occur only ONCE on the list, so don't try
  // to add next year's event as well as this year's, bad things
  // will probably happen.  Just keep the ID, change the timestamp,
  // and move it to the right point in the list.
  // If an event goes away for a year, just set the timestamp to
  // either 0 and move it to the front, don't delete it
  // If an event really goes away then set the timestamp to 0x7FFFFFFF
  // and move it to the end of the list AFTER the "MINI" event.

  // bogus event so there's always one before the real events
  {  0,true, 0, 0,          "DUMMY"},

  // start timestamp for A MINI Vacation in Vegas 5/28
  {  2,true, 4, 1432785600, "AMVIV"},   
  // start timestamp for MINIs on Top 6/19
  {  3,true, 2, 1434686400, "MOT"  },
  // start timestamp for MINI Meet East 6/29
  {  4,true, 3, 1435550400, "MME"  },
  // start timestamp for MINIs on the Mack 7/31
  {  5,true, 2, 1438315200, "MACK" },
  // start timestamp for MINIs in the Mountains 8/5
  {  6,true, 5, 1438747200, "MITM" },
  // start timestamp for Mickey and MINI 9/25
  {  7,true, 3, 1443153600, "MandM"},
  // start timestamp for MINIs in Foliage 10/1
  {  8,true, 4, 1443672000, "MiF"  },
  // start timestamp for MINIs Slay the Sleeping Dragon 10/9
  {  9,true, 3, 1444363200, "MSSD" },
  // start timestamp for MINIS in the Ozarks 10/22
  { 10,true, 4, 1445486400, "MITO" },
  // start timestamp for MINIs on the Dragon 5/12/2016
  {  1,true, 5, 1463025600, "MOTD" },

  // bogus event so there's always one after the real events
  {255,true, 0, 0x7FFFFFFE, "MINI" }
};

static unsigned int n_events = sizeof(event)/sizeof(event[0]);

static int id_to_index(uint8_t id)
{
  int index = -1;
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Looking for id %d", id);
  if (id<255) {
    for (uint8_t i = 0; i < n_events; ++i) {
      if (event[i].id == id) {
        index = i;
        //APP_LOG(APP_LOG_LEVEL_DEBUG, "Found  id %d at index %d", id, i);
        break;
      }
    }
  }
  return index;
}

static uint8_t index_to_id(uint8_t index)
{
  int id = 255;
  if (index < n_events) id = event[index].id;
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Index %d is id %d", index, id);
  return id;
}

static bool recall_setting(uint8_t id, bool* enable)
{
  bool found = false;
  uint32_t storage_key =  STORAGE_KEY_OFFSET + (uint32_t)id;
  if (persist_exists(storage_key)) {
    *enable = persist_read_bool(storage_key);
	  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Found setting %lu for id %d value %d", storage_key, id, *enable);
    found = true;
  }
  return (found);
}

static void recall_all_settings()
{
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Recalling all settings");
  uint8_t  id = 255;
  for (uint8_t i = 1; i < n_events; ++i) {
    id = index_to_id(i);
    bool enable;
    if (recall_setting(id, &enable)) event[i].enabled = enable;
  }  
}

static void store_setting(uint8_t id, bool enable)
{
  bool enabled;
  bool exists = recall_setting(id, &enabled);
  if ((exists && enable!=enabled) || !exists) {
    uint32_t storage_key = STORAGE_KEY_OFFSET + (uint32_t)id;
    persist_write_bool(storage_key,enable);
	  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Stored setting %lu for id %d value %d", storage_key, id, enable);
  }
}

static void store_all_settings()
{
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Storing all settings");
  uint8_t id = 255;
  for (uint8_t i = 1; i < n_events; ++i) {
    id = index_to_id(i);
    store_setting(id, event[i].enabled);
  }   
}

// start timestamp
static time_t event_start = 0;
// number of days - 1 (starts at 0)
static int    event_length = 0;
// name of the event
static char   event_name_buffer[32];

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

void choose_event()
{
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Choosing event");
  // pick event based on current time
  // Algorithm:
  //   start at the second entry in the structure.
  //   If the event is enabled through the settings
  //     If the current time is:
  //       greater than the end time of the previous event+1 day
  //       *and*
  //       it's less than the start time of the next event
  //     then this is the event we care about
  //     so set event_index to this one and break out of the loop.

  // get time now
  time_t temp = time(NULL); 

  // initialize variable
  time_t last_finish = event[0].start + (event[0].length+1)*60*60*24;
  
  for (unsigned int i=1; i<n_events; ++i) {
    if (event[i].enabled) {
      time_t this_finish = event[i].start + (event[i].length+1)*60*60*24;

      event_index = i;
      if ((temp > last_finish) && (temp < this_finish)) {
        // time now is after the end of the last event
        // and before the end of the current event
        break;
      }
      last_finish = this_finish;
    }
  }
  event_start = event[event_index].start;
  // account for including first day in the info structure by subtracting 1
  event_length = event[event_index].length-1;
  strncpy(event_name_buffer, event[event_index].name,sizeof(event_name_buffer));
}

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
  if (diff > 1000) 
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\nforever");
  else if (diff > 1)
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%d days",diff);
  else if (diff == 1)
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%d day",diff);
  // diff between -length and 0 means event running
  else if ((diff >= (1-event_length)) && (diff <= 0))
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%s","is ON!");
  //diff less than -length means over
  else if (diff < (1-event_length))
    snprintf(to_go_buffer,sizeof(to_go_buffer),"\n%s","is over");
    
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
  // once a day look to see if we have passed an event
  if (units_changed & DAY_UNIT) {
    choose_event();
    update_countdown();
  }

  if (units_changed & HOUR_UNIT)   {
    update_countdown();
    update_hours();
  }
  
  if (units_changed & MINUTE_UNIT) update_minutes();
  if (units_changed & SECOND_UNIT) update_seconds();
}

static void app_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void* context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "app error %d", app_message_error);
}

static int get_index(const char *which)
{
  int idx = -1;
  int size = sizeof(which);
  for (unsigned int i=1; i<n_events; ++i) {
    if (strncmp(which,event[i].name,size)==0) {
      idx = i;
      break;
    }
  }
  return idx;
}

static bool update_event(char *name, uint8_t value)
{
  bool changed = false;
  int idx = get_index(name);
  if (idx>-1) {
    if ((value < 2) && (event[idx].enabled != value)) {
      //  update value
      event[idx].enabled = value;
      uint8_t id = index_to_id(idx);
      store_setting(id,value);
      changed = true;
    }
  }
  return (changed);
}

#if USE_SET_PEBBLE
static void tuple_changed_callback(const uint32_t key, const Tuple* tuple_new, const Tuple* tuple_old, void* context) {
  //  we know these values are uint8 format
  int value = tuple_new->value->uint8;
  bool changed = false;
  switch (key) {
    case setting_MOTD:
      if(update_event("MOTD",value)) changed = true;
      break;
    case setting_AMVIV:
      if(update_event("AMVIV",value)) changed = true;
      break;
    case setting_MOT:
      if(update_event("MOT",value)) changed = true;
      break;
    case setting_MME:
      if(update_event("MME",value)) changed = true;
      break;
    case setting_MACK:
      if(update_event("MACK",value)) changed = true;
      break;
    case setting_MITM:
      if(update_event("MITM",value)) changed = true;
      break;
    case setting_MandM:
      if(update_event("MandM",value)) changed = true;
      break;
    case setting_MiF:
      if(update_event("MiF",value)) changed = true;
      break;
    case setting_MSSD:
      if(update_event("MSSD",value)) changed = true;
      break;
    case setting_MITO:
      if(update_event("MITO",value)) changed = true;
      break;
  }
  if (changed) {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "SetPabble setting changed");
    choose_event();
    update_countdown();
  }
}
#endif

static void get_setting(char * event, EventSetting *setting)
{
  bool enable;
  if (recall_setting(index_to_id(get_index(event)),&enable)) {
    *setting  = enable?event_on:event_off;
  }
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
  ampm_layer = bitmap_layer_create(GRect(0, 98-25, 22, 25));
  bitmap_layer_set_compositing_mode(ampm_layer,GCompOpAnd);
  bitmap_layer_set_bitmap(ampm_layer, ampm_blank);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(ampm_layer));

  // set up hours display
  hours_layer =   text_layer_create(GRect(21, 63, 52, 40));
	
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
	
#if USE_SET_PEBBLE
  // set initial values from persistent storage
  get_setting("MOTD" , &motd );
  get_setting("AMVIV", &amviv);
  get_setting("MOT"  , &mot  );
  get_setting("MME"  , &mme  );
  get_setting("MACK" , &mack );
  get_setting("MITM" , &mitm );
  get_setting("MandM", &mandm);
  get_setting("MiF"  , &mif  );
  get_setting("MSSD" , &mssd );
  get_setting("MITO" , &mito );

  //  app communication
  Tuplet tuples[] = {
    TupletInteger(setting_MOTD,  motd),
    TupletInteger(setting_AMVIV, amviv),
    TupletInteger(setting_MOT,   mot),
    TupletInteger(setting_MME,   mme),
    TupletInteger(setting_MACK,  mack),
    TupletInteger(setting_MITM,  mitm),
    TupletInteger(setting_MandM, mandm),
    TupletInteger(setting_MiF,   mif),
    TupletInteger(setting_MSSD,  mssd),
    TupletInteger(setting_MITO,  mito)
  };
  
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

//  uint32_t bufsize = dict_calc_buffer_size_from_tuplets(tuples,ARRAY_LENGTH(tuples));
//  APP_LOG(APP_LOG_LEVEL_DEBUG, "Need a buffer this big: %lu", bufsize);

  app_sync_init(&app, buffer, sizeof(buffer), tuples, ARRAY_LENGTH(tuples),
                tuple_changed_callback, app_error_callback, NULL);
#else
  recall_all_settings();
#endif

  choose_event();

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

  store_all_settings();
  
#if USE_SET_PEBBLE
  app_sync_deinit(&app);
#endif
  
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
