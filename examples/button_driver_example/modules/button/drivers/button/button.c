#define DT_DRV_COMPAT custom_button

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include "button.h"

LOG_MODULE_REGISTER(button);

static int button_init(const struct device *dev)
{
    const struct button_config *cfg = dev->config;
    if (!gpio_is_ready_dt(&cfg->btn)) {
        LOG_ERR("GPIO controller not ready");
        return -ENODEV;
    }
    return gpio_pin_configure_dt(&cfg->btn, GPIO_INPUT);
}

static int button_state_get(const struct device *dev, uint8_t *state)
{
    const struct button_config *cfg = dev->config;
    int val = gpio_pin_get_dt(&cfg->btn);
    if (val < 0) {
        LOG_ERR("Failed to read pin (%d)", val);
        return val;
    }
    *state = (uint8_t)val;
    return 0;
}

static const struct button_api button_api_funcs = {
    .get = button_state_get,
};

#define BUTTON_DEFINE(inst) \
    static const struct button_config button_config_##inst = { \
        .btn = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), gpios), \
        .id = inst, \
    }; \
    DEVICE_DT_INST_DEFINE(inst, button_init, NULL, NULL, \
                          &button_config_##inst, \
                          POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, \
                          &button_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(BUTTON_DEFINE)
