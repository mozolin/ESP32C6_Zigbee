#include "esp32c6_zigbee.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zigbee_logo.h"
#include "zigbee_connected.h"
#include "zigbee_disconnected.h"
#include "zigbee_image.h"
#include "iot_button.h"
#include <time.h>
#include <sys/time.h>

#include "driver/i2c.h"
#include "ssd1306.h"
#include "led_strip.h"
#include "bmx280.h"
#include "ds18b20_main.h"
#if USE_BH1750_SENSOR
  #include "bh1750.h"
#endif

//------ Global definitions -----------
static char
  manufacturer[16],
  model[16],
  firmware_version[16];
bool
  time_updated = false,
  connected = false,
  ds18b20_found = false,
  updateAttributeStarted = false;
int lcd_timeout = 30;
uint8_t
  screen_number = 0,
  s_led_state = 0; 
uint16_t
  temperature = 0,
  humidity = 0,
  pressure = 0,
  temperature2 = 0,
  undefined_value = 0x8000;
float
  t = 0,
  p = 0,
  h = 0,
  t2 = 0,
  //temp_ds18b20 = 0,
  l = 0;
char strftime_buf[64];
static ssd1306_handle_t ssd1306_dev = NULL;
SemaphoreHandle_t i2c_semaphore = NULL;
static const char *TAG_ESP32C6 = "ESP32C6_ZIGBEE";
extern int ds18b20_device_num;
extern float ds18b20_temperature_list[ONEWIRE_MAX_DS18B20];
#if USE_BH1750_SENSOR
  uint16_t lIntBH1750 = 0;
  float lBH1750 = 0;
  uint16_t lBH1750Raw = 0;
  
  //-- arrays for val and raw from bh1750_red()
  float arrlBH1750[1];
  uint16_t arrlBH1750Raw[1];
  //float lBH1750Data[2];
  //static const char *TAG_BH1750 = "BH1750";
#endif
//char degree[]="\u00b0";
//char degree[]=0xDF;
//char degree[]="°";


static void blink_led(void)
{
  gpio_set_level(BLINK_GPIO, s_led_state);
}
static void configure_led(void)
{
  gpio_reset_pin(BLINK_GPIO);
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void do_blink()
{
  s_led_state = 1;
  blink_led();

  vTaskDelay(500 / portTICK_PERIOD_MS);

  s_led_state = 0;
  blink_led();
  //vTaskDelete(NULL);
}

static void button_single_click_cb(void *arg,void *usr_data)
{
  ESP_LOGI("Button boot", "Single click, change screen to %d", screen_number);
  lcd_timeout = 30;
  screen_number = screen_number + 1;
  if( screen_number == 2)
  {
    screen_number = 0;
  }
}

static void button_long_press_cb(void *arg,void *usr_data)
{
  ESP_LOGI("Button boot", "Long press, leave & reset");
  esp_zb_factory_reset();
}

void register_button()
{
  //-- create GPIO button
  button_config_t gpio_btn_cfg = {
    .type = BUTTON_TYPE_GPIO,
    .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
    .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
    .gpio_button_config = {
      .gpio_num = 9,
      .active_level = 0,
    },
  };

  button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
  if(NULL == gpio_btn) {
    ESP_LOGE("Button boot", "Button create failed");
  }

  iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, button_single_click_cb,NULL);
  iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_START, button_long_press_cb, NULL);

}

esp_err_t i2c_master_init()
{
  //-- Don't initialize twice!
  if(i2c_semaphore != NULL) {
    return ESP_FAIL;
  }
        
  i2c_semaphore = xSemaphoreCreateMutex();
  if(i2c_semaphore == NULL) {
    return ESP_FAIL;
  }
        
  i2c_config_t i2c_config = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = GPIO_NUM_6,
    .scl_io_num = GPIO_NUM_7,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = 1000000
  };

  esp_err_t ret;

  ret = i2c_param_config(I2C_NUM_0, &i2c_config);
  if(ret != ESP_OK)
  {
    return ret;
  }
        
  ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  if(ret != ESP_OK)
  {
    return ret;
  }
        
  return ESP_OK;
}

//--------- User task section -----------------
static void get_rtc_time()
{
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%a %H:%M:%S", &timeinfo);    
}

void lcd_screen_0()
{
	ssd1306_refresh_gram(ssd1306_dev);
	ssd1306_clear_screen(ssd1306_dev, 0x00);

	char temp_data_str[100] = {0};
	int i = 0;
	for(i = 0; i < ds18b20_device_num; i ++) {
		t2 = ds18b20_temperature_list[i];
		sprintf(temp_data_str, "ds18b20[%d]: %.2f", i, t2);
		//ssd1306_draw_string(ssd1306_dev, 0, 26 + i * 12, (const uint8_t *)temp_data_str, 12, 1);
		ssd1306_draw_string(ssd1306_dev, 0, 15 + i * 12, (const uint8_t *)temp_data_str, 12, 1);
	}

	#if USE_BH1750_SENSOR
    //-- BH1750
    i = 3; //-- 4th row on SSD1036
    sprintf(temp_data_str, "bh1750: %.2f", lBH1750);
    ssd1306_draw_string(ssd1306_dev, 0, 15 + i * 12, (const uint8_t *)temp_data_str, 12, 1);
  #endif

	sprintf(temp_data_str, "%.2f, %.2f%%, %.0f", t, h, p/100);
	//ssd1306_draw_string(ssd1306_dev, 0, 14, (const uint8_t *)temp_data_str, 12, 1);
	ssd1306_draw_string(ssd1306_dev, 0, 5, (const uint8_t *)temp_data_str, 12, 1);
	//ESP_LOGI("4", "T2: %.2f, T: %.2f, H: %.2f, P: %.0f", t2, t, h, p);

	ssd1306_draw_bitmap(ssd1306_dev, 112, 48, zigbee_image, 16, 16);
	if (connected)
	{
		//ESP_LOGW("LCD", "CONNECTED!");
		ssd1306_draw_bitmap(ssd1306_dev, 112, 0, zigbee_connected, 16, 16);
	} else {
		//ESP_LOGE("LCD", "NOT CONNECTED!");
		ssd1306_draw_bitmap(ssd1306_dev, 112, 0, zigbee_disconnected, 16, 16);
	}
	ssd1306_refresh_gram(ssd1306_dev);
}

void lcd_screen_1()
{
  ssd1306_refresh_gram(ssd1306_dev);
  ssd1306_clear_screen(ssd1306_dev, 0x00);
  ssd1306_draw_bitmap(ssd1306_dev, 112, 48, zigbee_image, 16, 16);
  if(connected)
  {
    if(time_updated)
    {
      get_rtc_time();
      ESP_LOGI(TAG_ESP32C6, "The current date/time is: %s", strftime_buf);
      ssd1306_draw_string(ssd1306_dev, 5, 48, (const uint8_t *)strftime_buf, 16, 1);
    }

    char connected_str[16] = {0};
    char PAN_ID[16] = {0};
    char Channel[16] = {0};
    sprintf(connected_str, "  Connected");
    sprintf(PAN_ID, "PAN ID : 0x%04hx", esp_zb_get_pan_id());
    sprintf(Channel, "Channel: %d", esp_zb_get_current_channel());
    ssd1306_draw_string(ssd1306_dev, 5, 0, (const uint8_t *)connected_str, 16, 1);
    ssd1306_draw_string(ssd1306_dev, 5, 16, (const uint8_t *)PAN_ID, 16, 1);
    ssd1306_draw_string(ssd1306_dev, 5, 32, (const uint8_t *)Channel, 16, 1);
    ssd1306_draw_bitmap(ssd1306_dev, 112, 0, zigbee_connected, 16, 16);
  } else {
    char disconnected_str[16] = {0};
    sprintf(disconnected_str, " Disconnected");
    ssd1306_draw_string(ssd1306_dev, 5, 16, (const uint8_t *)disconnected_str, 16, 1);
    ssd1306_draw_bitmap(ssd1306_dev, 112, 0, zigbee_disconnected, 16, 16);
  }
  ssd1306_refresh_gram(ssd1306_dev);
}

static void lcd_task(void *pvParameters)
{
  //-- Start lcd
  ssd1306_dev = ssd1306_create(I2C_NUM_0, SSD1306_I2C_ADDRESS);
  ssd1306_refresh_gram(ssd1306_dev);
  ssd1306_clear_screen(ssd1306_dev, 0x00);

  ssd1306_draw_bitmap(ssd1306_dev, 0, 16, zigbee, 128, 32);
  ssd1306_refresh_gram(ssd1306_dev);
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  
  while(1)
  {  
    switch(screen_number) {
      case 0:
        //ESP_LOGI(TAG_ESP32C6, "Screen number 0 ");
        lcd_screen_0();
        break;
      case 1:
        //ESP_LOGI(TAG_ESP32C6, "Screen number 1 ");
        lcd_screen_1();
        break;
      default:
        ESP_LOGW(TAG_ESP32C6, "Default screen --------");
        break;  
    }
    lcd_timeout = lcd_timeout - 1;
    if(lcd_timeout <= 0) {
      screen_number = 0;
    } else {
      lcd_timeout = lcd_timeout - 1;
      //ESP_LOGI(TAG_ESP32C6, "lcd_timeout %d ", lcd_timeout);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

#if USE_BH1750_SENSOR
static void bh1750_task(void *pvParameters)
{
  while(1) {
	  bh1750_read(arrlBH1750, arrlBH1750Raw);
	  lBH1750 = arrlBH1750[0];
	  lBH1750Raw = arrlBH1750Raw[0];

	  lIntBH1750 = (uint16_t)(lBH1750 * 10000);
	  //ESP_LOGI(TAG_BH1750, "val:%.2f, int:%d, raw:%d", lBH1750, lIntBH1750, lBH1750Raw);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
  
  vTaskDelete(NULL);
}
#endif

static void bmx280_task(void *pvParameters)
{
  bmx280_t* bmx280 = bmx280_create(I2C_NUM_0);

  if(!bmx280) { 
    ESP_LOGE("BMX280", "Could not create bmx280 driver.");
    return;
  }

  ESP_ERROR_CHECK(bmx280_init(bmx280));
  bmx280_config_t bmx_cfg = BMX280_DEFAULT_CONFIG;
  ESP_ERROR_CHECK(bmx280_configure(bmx280, &bmx_cfg));

  while(1)
  {
    ESP_ERROR_CHECK(bmx280_setMode(bmx280, BMX280_MODE_FORCE));
    do {
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    } while(bmx280_isSampling(bmx280));
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(bmx280_readoutFloat(bmx280, &t, &p, &h));
    //ESP_LOGI("BMX280", "Read Values: t = %.2f, p = %.1f, h = %.1f", t, p/100, h);
    temperature = (uint16_t)(t * 100);
    humidity = (uint16_t)(h * 100);
    pressure = (uint16_t)(p/100);

    ds18b20_show();

    if(ds18b20_device_num > 0) {
    	ds18b20_found = true;
    }

    char ds18b20_str[200] = {0};
    char ds18b20_int[10] = {0};
    //strcpy(ds18b20_str, "");
    //strcpy(ds18b20_int, "");
    for(int i = 0; i < ds18b20_device_num; i ++) {
      t2 = ds18b20_temperature_list[i];
      //ESP_LOGI(TAG_DS18B20, "Temperature read from DS18B20[%d]: %.2fC", i, t2);
      if(strlen(ds18b20_str) > 0) {
      	strcat(ds18b20_str, ", ");
      }
      sprintf(ds18b20_int, "%.2f", t2);
      strcat(ds18b20_str, ds18b20_int);
    }

    #if USE_BH1750_SENSOR
      ESP_LOGI("BMX280|DS18B20|BH1750", "T=%.2fC, H=%.2f%%, P=%.0fhPa | DS18B20: %d/%d dev(s) => %s | %.2fLux", t, h, p/100, ds18b20_device_num, ONEWIRE_MAX_DS18B20, ds18b20_str, lBH1750);
    #else
      ESP_LOGI("BMX280|DS18B20", "T=%.2fC, H=%.2f%%, P=%.0fhPa | DS18B20: %d/%d dev(s) => %s", t, h, p/100, ds18b20_device_num, ONEWIRE_MAX_DS18B20, ds18b20_str);
    #endif
  }
}

static void ds18b20_task(void *pvParameters)
{
  while(1)
  {
    ds18b20_show();

    if(ds18b20_device_num > 0) {
    	ds18b20_found = true;
    }

    char ds18b20_str[200] = {0};
    char ds18b20_int[10] = {0};
    
    for(int i = 0; i < ds18b20_device_num; i ++) {
      t2 = ds18b20_temperature_list[i];
      //ESP_LOGI(TAG_DS18B20, "Temperature read from DS18B20[%d]: %.2fC", i, t2);
      if(strlen(ds18b20_str) > 0) {
      	strcat(ds18b20_str, ", ");
      }
      sprintf(ds18b20_int, "%.2f", t2);
      strcat(ds18b20_str, ds18b20_int);
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}


//----------------------------------------

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
  ESP_LOGW(TAG_ESP32C6, "bdb_start_top_level_commissioning_cb()");
  ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

//-- Manual reporting attribute to coordinator
static void reportAttribute(uint8_t srcEndpoint, uint8_t dstEndpoint, uint16_t clusterID, uint16_t attributeID, void *value, uint8_t value_length)
{
  esp_zb_zcl_report_attr_cmd_t cmd = {
    .zcl_basic_cmd = {
      .dst_addr_u.addr_short = 0x0000,
      .src_endpoint = srcEndpoint,
      .dst_endpoint = dstEndpoint,
    },
    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
    .clusterID = clusterID,
    .attributeID = attributeID,
    .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
  };
  esp_zb_zcl_attr_t *value_r = esp_zb_zcl_get_attribute(srcEndpoint, clusterID, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attributeID);
  memcpy(value_r->data_p, value, value_length);
  esp_zb_zcl_report_attr_cmd_req(&cmd);

  vTaskDelay(500 / portTICK_PERIOD_MS);
}

//-- Task for update attribute value
void update_attribute()
{
  while(1)
  {
    if(connected)
    {
      ESP_LOGW("UPD_ATTR", "CONNECTED!");
      //ESP_LOGW(TAG_ESP32C6, "update_attribute(%d - %d - %d)", temperature, humidity, pressure);
      esp_zb_zcl_status_t state_tmp = esp_zb_zcl_set_attribute_val(BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temperature, false);
      if(state_tmp != ESP_ZB_ZCL_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG_ESP32C6, "Setting temperature attribute failed!");
      } else {
        reportAttribute(BMX280_SENSOR_ENDPOINT, BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temperature, 2);
      }
      esp_zb_zcl_status_t state_hum = esp_zb_zcl_set_attribute_val(BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &humidity, false);
      if(state_hum != ESP_ZB_ZCL_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG_ESP32C6, "Setting humidity attribute failed!");
      } else {
        reportAttribute(BMX280_SENSOR_ENDPOINT, BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &humidity, 2);
      }
      esp_zb_zcl_status_t state_press = esp_zb_zcl_set_attribute_val(BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, &pressure, false);
      if(state_press != ESP_ZB_ZCL_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG_ESP32C6, "Setting pressure attribute failed!");
      } else {
        reportAttribute(BMX280_SENSOR_ENDPOINT, BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, &pressure, 2);
      }
      //ESP_LOGW("BMX280", "EP:%d", BMX280_SENSOR_ENDPOINT);

      //-- Write new temperature value for DS18B20
      for(int i = 0; i < ds18b20_device_num; i ++) {
        uint8_t DS18B20_SENSOR_ENDPOINT = BMX280_SENSOR_ENDPOINT + i + 1;
        
        //ESP_LOGE(TAG_ESP32C6, "UPDATE: DS18B20_SENSOR_ENDPOINT = %d", DS18B20_SENSOR_ENDPOINT);
        
        t2 = ds18b20_temperature_list[i];
        temperature2 = t2 * 100;
        
        //ESP_LOGW(TAG_ESP32C6, "UPDATE[%d] WITH %.2f (%d)", i, t2, temperature2);
        
        esp_zb_zcl_status_t ds18b20_state = esp_zb_zcl_set_attribute_val(DS18B20_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temperature2, false);
        if(ds18b20_state != ESP_ZB_ZCL_STATUS_SUCCESS) {
          ESP_LOGE(TAG_ESP32C6, "Setting DS18B20[%d] attribute failed!", i);
        } else {
          reportAttribute(DS18B20_SENSOR_ENDPOINT, 1, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temperature2, 2);
          //reportAttribute(DS18B20_SENSOR_ENDPOINT, DS18B20_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temperature2, 2);
        }
        //ESP_LOGW(TAG_ESP32C6, "REPORT_DS18B20[%d] WITH %.2f (%d)", i, t2, temperature2);
        //ESP_LOGW("DS18B20", "EP:%d", DS18B20_SENSOR_ENDPOINT);
      }

      #if USE_BH1750_SENSOR
        #if !USE_BH1750_CUSTOM_ENDPOINT
          //-- BH1750 (EP #1)
          //ESP_LOGE(TAG_ESP32C6, "1");
          esp_zb_zcl_status_t state_illum = esp_zb_zcl_set_attribute_val(BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, false);
          //ESP_LOGE(TAG_ESP32C6, "2");
          if(state_illum != ESP_ZB_ZCL_STATUS_SUCCESS) {
            ESP_LOGE(TAG_ESP32C6, "Setting lIntBH1750 (EP:%d) attribute failed!", BMX280_SENSOR_ENDPOINT);
          } else {
            reportAttribute(BMX280_SENSOR_ENDPOINT, BMX280_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, 2);
            //reportAttribute(1, 1, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, 2);
          }
          //ESP_LOGW("BH1750", "EP:%d", BMX280_SENSOR_ENDPOINT);
          //ESP_LOGE(TAG_ESP32C6, "3");
        #else
          //-- BH1750 (BH1750_SENSOR_ENDPOINT)
          //ESP_LOGE(TAG_ESP32C6, "1");
          esp_zb_zcl_status_t state_illum = esp_zb_zcl_set_attribute_val(BH1750_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, false);
          //ESP_LOGE(TAG_ESP32C6, "2");
          if(state_illum != ESP_ZB_ZCL_STATUS_SUCCESS) {
            ESP_LOGE(TAG_ESP32C6, "Setting lIntBH1750 (EP:%d) attribute failed!", BH1750_SENSOR_ENDPOINT);
          } else {
            reportAttribute(BH1750_SENSOR_ENDPOINT, 1, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, 2);
            //reportAttribute(BH1750_SENSOR_ENDPOINT, BH1750_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &lBH1750Raw, 2);
          }
          //ESP_LOGE(TAG_ESP32C6, "3");
          //ESP_LOGW("BH1750", "EP:%d", BH1750_SENSOR_ENDPOINT);
        #endif
        //ESP_LOGE(TAG_ESP32C6, "4");
      #endif

      do_blink();

    } else {
      ESP_LOGE("UPD_ATTR", "NOT CONNECTED!");
    }
    
    vTaskDelay(UPDATE_ATTR_INTERVAL * 1000 / portTICK_PERIOD_MS);
    /*
    if(updateAttributeStarted) {
    	vTaskDelay(UPDATE_ATTR_INTERVAL * 1000 / portTICK_PERIOD_MS);
    } else {
    	vTaskDelay(5000 / portTICK_PERIOD_MS);
    	
    }
    */
    //-- first use of this function
    updateAttributeStarted = true;
  }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
  esp_err_t ret = ESP_OK;
  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG_ESP32C6, "Empty message");
  ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG_ESP32C6, "Received message: error status(%d)",
    message->info.status);
  ESP_LOGI(TAG_ESP32C6, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
    message->attribute.id, message->attribute.data.size);
  if(message->info.dst_endpoint == BMX280_SENSOR_ENDPOINT) {
    switch (message->info.cluster) {
      case ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY:
        ESP_LOGI(TAG_ESP32C6, "Identify pressed");
        break;
      default:
        ESP_LOGI(TAG_ESP32C6, "Message data: cluster(0x%x), attribute(0x%x)  ", message->info.cluster, message->attribute.id);
    }
  }
  return ret;
}

static esp_err_t zb_read_attr_resp_handler(const esp_zb_zcl_cmd_read_attr_resp_message_t *message)
{
  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG_ESP32C6, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG_ESP32C6, "Received message: error status(%d)",
      message->info.status);
    ESP_LOGI(TAG_ESP32C6, "Read attribute response: status(%d), cluster(0x%x), attribute(0x%x), type(0x%x), value(%d)", message->info.status,
      message->info.cluster, message->attribute.id, message->attribute.data.type,
      message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : 0);
    if(message->info.dst_endpoint == BMX280_SENSOR_ENDPOINT) {
      switch (message->info.cluster) {
        case ESP_ZB_ZCL_CLUSTER_ID_TIME:
          ESP_LOGI(TAG_ESP32C6, "Server time recieved %lu", *(uint32_t*) message->attribute.data.value);
          struct timeval tv;
          //-- after adding OTA cluster time shifted to 1080 sec... strange issue ... 
          tv.tv_sec = *(uint32_t*) message->attribute.data.value + 946684800 - 1080;
          //tv.tv_sec = *(uint32_t*) message->attribute.data.value + 946684800;
          settimeofday(&tv, NULL);
          time_updated = true;
          break;
        default:
          ESP_LOGI(TAG_ESP32C6, "Message data: cluster(0x%x), attribute(0x%x)  ", message->info.cluster, message->attribute.id);
      }
    }
    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
  esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
      ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
      break;
    case ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID:
      ret = zb_read_attr_resp_handler((esp_zb_zcl_cmd_read_attr_resp_message_t *)message);
      break;
    default:
      ESP_LOGW(TAG_ESP32C6, "Receive Zigbee action(0x%x) callback", callback_id);
      break;
    }
    return ret;
}

void read_server_time()
{
  esp_zb_zcl_read_attr_cmd_t read_req;
  read_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
  read_req.attributeID = ESP_ZB_ZCL_ATTR_TIME_LOCAL_TIME_ID;
  read_req.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TIME;
  read_req.zcl_basic_cmd.dst_endpoint = 1;
  read_req.zcl_basic_cmd.src_endpoint = 1;
  read_req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
  esp_zb_zcl_read_attr_cmd_req(&read_req);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
  uint32_t *p_sg_p       = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = *p_sg_p;
  esp_zb_zdo_signal_leave_params_t *leave_params = NULL;
  switch (sig_type) {
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
  case ESP_ZB_BDB_SIGNAL_STEERING:
    if(err_status != ESP_OK) {
      connected = false;
      ESP_LOGW(TAG_ESP32C6, "Stack %s failure with %s status, steering",esp_zb_zdo_signal_to_string(sig_type), esp_err_to_name(err_status));
      esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
    } else {
      //-- device auto start successfully and on a formed network
      connected = true;
      esp_zb_ieee_addr_t extended_pan_id;
      esp_zb_get_extended_pan_id(extended_pan_id);
      ESP_LOGI(TAG_ESP32C6, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d)",
        extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
        extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
        esp_zb_get_pan_id(), esp_zb_get_current_channel());
      read_server_time();
    }
    break;
  case ESP_ZB_ZDO_SIGNAL_LEAVE:
    leave_params = (esp_zb_zdo_signal_leave_params_t *)esp_zb_app_signal_get_params(p_sg_p);
    if(leave_params->leave_type == ESP_ZB_NWK_LEAVE_TYPE_RESET) {
      ESP_LOGI(TAG_ESP32C6, "Reset device");
      esp_zb_factory_reset();
    }
    break;
  default:
    ESP_LOGI(TAG_ESP32C6, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
      esp_err_to_name(err_status));
    break;
  }
}

static void set_zcl_string(char *buffer, char *value)
{
  buffer[0] = (char) strlen(value);
  memcpy(buffer + 1, value, buffer[0]);
}

void add_bx280_clusters(esp_zb_cluster_list_t *esp_zb_cluster_list)
{
  //-- Temperature cluster
  esp_zb_attribute_list_t *esp_zb_temperature_meas_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT);
  esp_zb_temperature_meas_cluster_add_attr(esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &undefined_value);
  esp_zb_temperature_meas_cluster_add_attr(esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID, &undefined_value);
  esp_zb_temperature_meas_cluster_add_attr(esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID, &undefined_value);
  
  //-- Humidity cluster
  esp_zb_attribute_list_t *esp_zb_humidity_meas_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT);
  esp_zb_humidity_meas_cluster_add_attr(esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &undefined_value);
  esp_zb_humidity_meas_cluster_add_attr(esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MIN_VALUE_ID, &undefined_value);
  esp_zb_humidity_meas_cluster_add_attr(esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MAX_VALUE_ID, &undefined_value);
  
  //-- Pressure cluster
  esp_zb_attribute_list_t *esp_zb_press_meas_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT);
  esp_zb_pressure_meas_cluster_add_attr(esp_zb_press_meas_cluster, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, &undefined_value);
  esp_zb_pressure_meas_cluster_add_attr(esp_zb_press_meas_cluster, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_MIN_VALUE_ID, &undefined_value);
  esp_zb_pressure_meas_cluster_add_attr(esp_zb_press_meas_cluster, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_MAX_VALUE_ID, &undefined_value);
  
  #if USE_BH1750_SENSOR && !USE_BH1750_CUSTOM_ENDPOINT
    //-- BH1750 (BMX280_SENSOR_ENDPOINT): Create attributes list for BH1750
    esp_zb_attribute_list_t *bh1750_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT);
    esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &undefined_value);
    esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MIN_MEASURED_VALUE_ID, &undefined_value);
    esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MAX_MEASURED_VALUE_ID, &undefined_value);
  #endif

  esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_cluster_list, esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_humidity_meas_cluster(esp_zb_cluster_list, esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_pressure_meas_cluster(esp_zb_cluster_list, esp_zb_press_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  #if USE_BH1750_SENSOR && !USE_BH1750_CUSTOM_ENDPOINT
    //-- BH1750 (BMX280_SENSOR_ENDPOINT): Add cluster list for BH1750
    esp_zb_cluster_list_add_illuminance_meas_cluster(esp_zb_cluster_list, bh1750_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  #endif
}

void add_ds18d20_temperature_cluster(int i, esp_zb_ep_list_t *esp_zb_ep_list)
{
  //-- Create attributes list for DS18B20
  esp_zb_attribute_list_t *ds18b20_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT);
  esp_zb_temperature_meas_cluster_add_attr(ds18b20_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &undefined_value);
  esp_zb_temperature_meas_cluster_add_attr(ds18b20_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID, &undefined_value);
  esp_zb_temperature_meas_cluster_add_attr(ds18b20_cluster, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID, &undefined_value);
  //ESP_LOGE(TAG_ESP32C6, "ds18b20_cluster[%d]", i);
  
  //-- Create cluster list for DS18B20
  uint8_t DS18B20_SENSOR_ENDPOINT = BMX280_SENSOR_ENDPOINT + i + 1;
  //ESP_LOGE(TAG_ESP32C6, "DS18B20_SENSOR_ENDPOINT[%d] = %d", i, DS18B20_SENSOR_ENDPOINT);
  esp_zb_cluster_list_t *ds18b20_cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_temperature_meas_cluster(ds18b20_cluster_list, ds18b20_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_ep_list_add_ep(esp_zb_ep_list, ds18b20_cluster_list, DS18B20_SENSOR_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID);
}

//-- add custom BH1750_SENSOR_ENDPOINT
#if USE_BH1750_SENSOR && USE_BH1750_CUSTOM_ENDPOINT
void add_bh1750_illuminance_cluster(esp_zb_ep_list_t *esp_zb_ep_list)
{
  //-- Create attributes list for BH1750
  esp_zb_attribute_list_t *bh1750_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT);
  esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &undefined_value);
  esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MIN_MEASURED_VALUE_ID, &undefined_value);
  esp_zb_illuminance_meas_cluster_add_attr(bh1750_cluster, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MAX_MEASURED_VALUE_ID, &undefined_value);
  //ESP_LOGE(TAG_ESP32C6, "bh1750_cluster[%d]", BH1750_SENSOR_ENDPOINT);
  
  //-- Create cluster list for BH1750
  esp_zb_cluster_list_t *bh1750_cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_illuminance_meas_cluster(bh1750_cluster_list, bh1750_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_ep_list_add_ep(esp_zb_ep_list, bh1750_cluster_list, BH1750_SENSOR_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID);
}
#endif

static void esp_zb_task(void *pvParameters)
{
  //-- initialize Zigbee stack
  esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
  esp_zb_init(&zb_nwk_cfg);
  
  //-- basic cluster create with fully customized
  set_zcl_string(manufacturer, MANUFACTURER_NAME);
  set_zcl_string(model, MODEL_NAME);
  set_zcl_string(firmware_version, FIRMWARE_VERSION);

  esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, firmware_version);
  
  //-- identify cluster create with fully customized
  uint8_t identyfi_id;
  identyfi_id = 0;
  esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
  esp_zb_identify_cluster_add_attr(esp_zb_identify_cluster, ESP_ZB_ZCL_CMD_IDENTIFY_IDENTIFY_ID, &identyfi_id);

  //-- Time cluster
  esp_zb_attribute_list_t *esp_zb_server_time_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TIME);
  
  /** Create ota client cluster with attributes.
   *  Manufacturer code, image type and file version should match with configured values for server.
   *  If the client values do not match with configured values then it shall discard the command and
   *  no further processing shall continue.
   */
  esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
    .ota_upgrade_downloaded_file_ver = OTA_UPGRADE_FILE_VERSION,
    .ota_upgrade_manufacturer = OTA_UPGRADE_MANUFACTURER,
    .ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE,
  };
  esp_zb_attribute_list_t *esp_zb_ota_client_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);
  //-- add client parameters to ota client cluster
  esp_zb_ota_upgrade_client_parameter_t ota_client_parameter_config = {
    //-- time interval for query next image request command
    .query_timer = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,          
    //-- version of hardware
    .hardware_version = OTA_UPGRADE_HW_VERSION,                           
    //-- maximum data size of query block image
    .max_data_size = OTA_UPGRADE_MAX_DATA_SIZE,                           
  };
  void *ota_client_parameters = esp_zb_ota_client_parameter(&ota_client_parameter_config);
  esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_PARAMETER_ID, ota_client_parameters);

  //-- Create full cluster list enabled on device
  esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  add_bx280_clusters(esp_zb_cluster_list);
  esp_zb_cluster_list_add_time_cluster(esp_zb_cluster_list, esp_zb_server_time_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
  esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list, esp_zb_ota_client_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
  
  esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
  esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, BMX280_SENSOR_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID);
  
  //-- Temperature DS18B20 cluster
  for(int i = 0; i < ONEWIRE_MAX_DS18B20; i ++) {
    add_ds18d20_temperature_cluster(i,  esp_zb_ep_list);
  }

  #if USE_BH1750_SENSOR && USE_BH1750_CUSTOM_ENDPOINT
    //-- BH1750: create custom BH1750_SENSOR_ENDPOINT & add iluminance cluster
    add_bh1750_illuminance_cluster(esp_zb_ep_list);
  #endif

  //-- END
  esp_zb_device_register(esp_zb_ep_list);
  esp_zb_core_action_handler_register(zb_action_handler);
  esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
  ESP_ERROR_CHECK(esp_zb_start(true));
  
  esp_zb_main_loop_iteration();
  

  //vTaskDelay(10000 / portTICK_PERIOD_MS);
}

void app_main(void)
{
  register_button();
  ESP_ERROR_CHECK(i2c_master_init());

  configure_led();

  esp_zb_platform_config_t config = {
    .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
    .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
  };
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));

  xTaskCreate(lcd_task, "lcd_task", 4096, NULL, 1, NULL);
  xTaskCreate(bmx280_task, "bmx280_task",  4096, NULL, 2, NULL);
  xTaskCreate(ds18b20_init_task, "ds18b20_init_task", 4096, NULL, 3, NULL);
  xTaskCreate(ds18b20_task, "ds18b20_task", 4096, NULL, 4, NULL);
  #if USE_BH1750_SENSOR
    xTaskCreate(bh1750_task, "bh1750_task", 4096, NULL, 5, NULL);
  #endif

  xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 6, NULL);
  xTaskCreate(update_attribute, "Update_attribute_value", 4096, NULL, 7, NULL);
}
