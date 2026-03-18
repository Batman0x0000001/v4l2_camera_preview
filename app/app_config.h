#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app.h"

void app_config_init_default(AppConfig *cfg);
void app_print_banner(void);
void app_print_config(const AppConfig *cfg);

#endif