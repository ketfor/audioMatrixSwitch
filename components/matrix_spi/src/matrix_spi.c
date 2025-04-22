#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "matrix_spi.h"
#include "events_types.h"

#define PIN_NUM_MOSI CONFIG_PIN_NUM_MOSI
#define PIN_NUM_CLK CONFIG_PIN_NUM_CLK
#define PIN_NUM_CS CONFIG_PIN_NUM_CS

#define CLOCK_SPEED_HZ (10000000) // 10 MHz

static const char *TAG = "matix_spi";

spi_device_handle_t spiDevice;

static bool uint16ToBinaryStr(char *buf, uint16_t n) {
    for (uint16_t mask = 0x8000;  mask;  mask >>= 1) {
        bool bit_is_set = n & mask;
        *buf = '0' + bit_is_set;
        ++buf;
    }
    *buf = '\0';          /* add the terminator */
    return true;
}

void SendToMatrix(uint16_t *buf, uint8_t sz)
{
    spi_transaction_t tr;
    memset(&tr, 0, sizeof(tr));
    for (uint8_t i=0; i<sz; i++) {
        tr.length = 16;
        tr.tx_buffer = buf + i;
        char bin[17] = ""; 
        uint16ToBinaryStr(bin, buf[0]);
        ESP_LOGI(TAG, "transmit code: %s", bin);
        spi_device_transmit(spiDevice, &tr);
    }
}

void matrixSpiInit(void){
    /*
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
    */

      // Configure SPI bus
    
    spi_host_device_t host = SPI2_HOST;  
      
    spi_bus_config_t spiBusConfig = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0
    };
    esp_err_t err = spi_bus_initialize(host, &spiBusConfig, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed initilize spi bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };

    //memset(&spiIfConfig, 0, sizeof(spiIfConfig));
    spi_device_interface_config_t spiIfConfig = {
        .spics_io_num = PIN_NUM_CS,
        .clock_speed_hz = CLOCK_SPEED_HZ,
        .mode = 0,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY
    };
    err = spi_bus_add_device(host, &spiIfConfig, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed add spi bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };
    ESP_LOGI(TAG, "matrix_spi init finished.");
}