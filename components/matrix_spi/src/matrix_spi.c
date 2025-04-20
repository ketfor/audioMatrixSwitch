#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "matrix_spi.h"
#include "events_types.h"

#define TAG "matix_spi"

#define MATRIX_GPIO CONFIG_MATRIX_GPIO

ESP_EVENT_DEFINE_BASE(MATRIXSPI_EVENT);

static SemaphoreHandle_t xSemaphore;

static bool matrixSpiState = false;

static void setRelay()
{

}

static void matrixSpiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    xSemaphoreGive(xSemaphore);
}

static void configureMatrix(void)
{
    xSemaphoreGive(xSemaphore);
}

static void matrixTask(void *pvParameters)
{
    while (1) {
        if(xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
            setRelay();   
        } 
    }
}

#define STACK_SIZE 4096 

void matrixSpiInit(void){
    static StaticSemaphore_t xSemaphoreBuffer;
    xSemaphore = xSemaphoreCreateBinaryStatic(&xSemaphoreBuffer);
    configureMatrix();
    
    static StaticTask_t xTaskBuffer;
    static StackType_t xStack[STACK_SIZE];
    TaskHandle_t xHandle = xTaskCreateStatic(matrixTask, "matrixTask", STACK_SIZE, NULL, 5, xStack, &xTaskBuffer);
    if (xHandle == NULL){
        ESP_LOGE(TAG, "Task matrixTask not created");
    }    

    ESP_ERROR_CHECK(esp_event_handler_register(MATRIXSPI_EVENT, MATRIXSPI_EVENT_SEND, &matrixSpiEventHandler, NULL));
    ESP_LOGI(TAG, "MatrixSpi init finish");
}