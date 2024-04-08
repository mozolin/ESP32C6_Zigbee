#include "esp_zigbee_core.h"

#define MANUFACTURER_NAME               "Espressif Systems"
#define MODEL_NAME                      "MIKE.ESP32-C6"
#define FIRMWARE_VERSION                "1.2.3"

//-- the max amount of connected devices
#define MAX_CHILDREN                    10
//-- enable the install code policy for security
#define INSTALLCODE_POLICY_ENABLE       false
//-- main sensor endpoint
#define BMX280_SENSOR_ENDPOINT          1
//-- whether or not to use the BH1750 sensor at all
#define USE_BH1750_SENSOR               true
//-- BH1750 sensor endpoint
#define BH1750_SENSOR_ENDPOINT          5
//-- whether or not to use a custom endpoint for the BH1750 sensor
#define USE_BH1750_CUSTOM_ENDPOINT      false
//-- LED GPIO
#define CONFIG_BLINK_GPIO               5
#define BLINK_GPIO                      CONFIG_BLINK_GPIO
/***********************************************************

   Update attribute (refresh sensor?) interval in seconds
   
   DEPENDS ON "ONEWIRE_MAX_DS18B20" (ds18b20_main.h):
   If too many DS18B20 sensors are expected (are set), it
   may cause the zigbee task to fail!

*************************************************************/
#define UPDATE_ATTR_INTERVAL            20
//-- Zigbee primary channel mask use in the example
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
//-- The attribute indicates the file version of the downloaded image on the device
#define OTA_UPGRADE_MANUFACTURER        0x1001
//-- The attribute indicates the value for the manufacturer of the device
#define OTA_UPGRADE_IMAGE_TYPE          0x1011
//-- The attribute indicates the file version of the running firmware image on the device
#define OTA_UPGRADE_FILE_VERSION        0x01010101
//-- The parameter indicates the version of hardware
#define OTA_UPGRADE_HW_VERSION          0x0101
//-- The parameter indicates the maximum data size of query block image
#define OTA_UPGRADE_MAX_DATA_SIZE       64


#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif


#define ESP_ZB_ZR_CONFIG()                                                              \
  {                                                                                   \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,          \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE,  \
    .nwk_cfg.zczr_cfg = {                              \
      .max_children = MAX_CHILDREN,                    \
    },                                                 \
  }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                  \
  {                                                    \
    .radio_mode = RADIO_MODE_NATIVE,                   \
  }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                   \
  {                                                    \
    .host_connection_mode = HOST_CONNECTION_MODE_NONE, \
  }
