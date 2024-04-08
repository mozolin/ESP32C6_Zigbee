#include "onewire_bus.h"
#include "ds18b20.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ds18b20_main.h"

//extern float temp_ds18b20;
float temp_ds18b20 = 0;
float ds18b20_temperature_list[ONEWIRE_MAX_DS18B20];
int ds18b20_device_num = 0;
ds18b20_device_handle_t ds18b20s[ONEWIRE_MAX_DS18B20];


void ds18b20_init_task()
{
	//ESP_LOGI(TAG_DS18B20, "It starts...");
	//-- install 1-wire bus
	onewire_bus_handle_t bus = NULL;
	onewire_bus_config_t bus_config = {
	    .bus_gpio_num = ONEWIRE_BUS_GPIO,
	};
	onewire_bus_rmt_config_t rmt_config = {
	    //-- 1byte ROM command + 8byte ROM number + 1byte device command
	    .max_rx_bytes = 10,
	};
	ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

	onewire_device_iter_handle_t iter = NULL;
	onewire_device_t next_onewire_device;
	esp_err_t search_result = ESP_OK;

	//-- create 1-wire device iterator, which is used for device search
	ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
	//ESP_LOGI(TAG_DS18B20, "Device iterator created, start searching...");
	do {
    search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
    //-- found a new device, let's check if we can upgrade it to a DS18B20
    if(search_result == ESP_OK) {
      ds18b20_config_t ds_cfg = {};
      //-- check if the device is a DS18B20, if so, return the ds18b20 handle
      if(ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
        ESP_LOGI(TAG_DS18B20, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, next_onewire_device.address);
        ds18b20_device_num++;
      } else {
        ESP_LOGW(TAG_DS18B20, "Found an unknown device, address: %016llX", next_onewire_device.address);
      }
    }
	} while (search_result != ESP_ERR_NOT_FOUND);
	ESP_ERROR_CHECK(onewire_del_device_iter(iter));
	ESP_LOGI(TAG_DS18B20, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

	//vTaskDelay(5000 / portTICK_PERIOD_MS);
	vTaskDelete(NULL);
}

void ds18b20_show()
{
	//ESP_LOGW(TAG_DS18B20, "Showing: %d DS18B20 device(s)", ds18b20_device_num);
	for(int i = 0; i < ds18b20_device_num; i ++) {
		//ESP_LOGW(TAG_DS18B20, "...1[%d]", i);
		//-- DS18B20_RESOLUTION_9B ... DS18B20_RESOLUTION_12B 
		ESP_ERROR_CHECK(ds18b20_set_resolution(ds18b20s[i], DS18B20_RESOLUTION_9B));
		
		ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[i]));
		//ESP_LOGW(TAG_DS18B20, "...2[%d]", i);
		ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temp_ds18b20));
		//ESP_LOGW(TAG_DS18B20, "...3, Temperature read from DS18B20[%d]: %.2fC", i, temp_ds18b20);
		
		//-- make a list of DS18B20 temperature
		ds18b20_temperature_list[i] = temp_ds18b20;
	}
	//vTaskDelay(5000 / portTICK_PERIOD_MS);
}
