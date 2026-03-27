#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app.h"

/*
 * app_config 模块职责：
 * 1. 提供启动配置的默认值；
 * 2. 打印当前配置，方便调试和确认启动参数；
 * 3. 只描述“配置本身”，不负责真正执行启动动作。
 */

void app_config_init_default(AppConfig *cfg);
void app_print_banner(void);
void app_print_config(const AppConfig *cfg);

#endif