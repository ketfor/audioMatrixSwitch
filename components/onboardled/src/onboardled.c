#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "onboardled.h"
#include "events_types.h"

#define TAG "onboardled"

#define BLINK_GPIO CONFIG_BLINK_GPIO
#define BLINK_TICK_PERIOD CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS
ESP_EVENT_DEFINE_BASE(ONBOARDLED_EVENT);

static uint8_t s_led_state = 0;

#ifdef CONFIG_BLINK_LED_STRIP

static led_strip_handle_t led_strip;

static led_strip_collor_t led_strip_color = {
    .red = 16,
    .green = 16,
    .blue = 16
};

static void setColor(led_strip_collor_t* color)
{
    led_strip_color.red = color->red;
    led_strip_color.green = color->green;
    led_strip_color.blue = color->blue;
}

static void checkColor(TickType_t vTick)
{
    event_data_t event_data;
    static led_strip_collor_t* color = NULL;
    if(eventsDataQueueGet(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &event_data, vTick) == pdTRUE){
        color = (led_strip_collor_t*) event_data;
        ESP_LOGI(TAG, "Event queue receive: red = %d, green = %d, blue = %d", color->red, color->green, color->blue);
        setColor(color);
    }
}
static void onboardLedEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    led_strip_collor_t* color = (led_strip_collor_t*) event_data;
    ESP_LOGI(TAG, "Event receive: red = %d, green = %d, blue = %d", color->red, color->green, color->blue);
    setColor(color);
}

static void onboardLedCallbackHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    led_strip_collor_t* color = (led_strip_collor_t*) event_data;
    ESP_LOGI(TAG, "Callback receive: red = %d, green = %d, blue = %d", color->red, color->green, color->blue);
    setColor(color);
}

static void blink_led(void)
{
    /* If the addressable LED is enabled */
    if (s_led_state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, led_strip_color.red, led_strip_color.green, led_strip_color.blue);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);
    } else {
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);
    }
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "unsupported LED strip backend"
#endif
    
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
    ESP_ERROR_CHECK(esp_event_handler_register(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &onboardLedEventHandler, NULL));
    ESP_ERROR_CHECK(eventsCallbackHandlerRegister(ONBOARDLED_EVENT, ONBOARDLED_EVENT_SETCOLOR, &onboardLedCallbackHandler, NULL));
}

#elif CONFIG_BLINK_LED_GPIO

static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

#else
#error "unsupported LED type"
#endif

static void blinkTask(void *pvParameters) {

    while (1) {
        checkColor(0);
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(BLINK_TICK_PERIOD);
    }
}

#define STACK_SIZE 4096 

void onboardledInit(void){
    configure_led();
    static StaticTask_t xTaskBuffer;
    static StackType_t xStack[STACK_SIZE];
    TaskHandle_t xHandle = xTaskCreateStatic(blinkTask, "blinkTask", STACK_SIZE, NULL, 5, xStack, &xTaskBuffer);
    if (xHandle == NULL){
        ESP_LOGE(TAG, "OnboarLed init failed. Task blinkTask not created");
    }
    else {
        ESP_LOGI(TAG, "OnboarLed init finish");
    }
}