#include "joystick.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

static adc_oneshot_unit_handle_t adc_handle;

void joystick_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1};

    adc_oneshot_new_unit(
        &init_cfg,
        &adc_handle);

    adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT};

    adc_oneshot_config_channel(
        adc_handle,
        ADC_CHANNEL_6,
        &cfg);

    adc_oneshot_config_channel(
        adc_handle,
        ADC_CHANNEL_7,
        &cfg);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_25),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE};

    gpio_config(&io_conf);
}

joystick_data_t joystick_read(void)
{
    joystick_data_t data;

    adc_oneshot_read(
        adc_handle,
        ADC_CHANNEL_6,
        &data.x);

    adc_oneshot_read(
        adc_handle,
        ADC_CHANNEL_7,
        &data.y);

    data.button =
        gpio_get_level(GPIO_NUM_25);

    return data;
}