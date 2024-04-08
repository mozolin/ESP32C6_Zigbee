
#include "onewire_bus.h"
#include "ds18b20.h"

#define ONEWIRE_BUS_GPIO    4
#define ONEWIRE_MAX_DS18B20 3
#define TAG_DS18B20 "DS18B20"

void ds18b20_init_task();
void ds18b20_show();
