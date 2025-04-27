#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include "matrix_relay.h"

#define PIN_NUM_MOSI CONFIG_PIN_NUM_MOSI
#define PIN_NUM_CLK CONFIG_PIN_NUM_CLK
#define PIN_NUM_CS CONFIG_PIN_NUM_CS
#define SPI_HOST SPI2_HOST

#define CLOCK_SPEED_HZ (10000000) // 10 MHz

static const char *TAG = "matrix_relay";

static spi_device_handle_t spiDevice;

static bool uint16ToBinaryStr(char *buf, uint16_t n) {
    for (uint16_t mask = 0x8000;  mask;  mask >>= 1) {
        bool bit_is_set = n & mask;
        *buf = '0' + bit_is_set;
        ++buf;
    }
    *buf = '\0';          /* add the terminator */
    return true;
}

void sendToRelay(uint16_t *buf, uint8_t sz)
{
    spi_transaction_t tr;
    memset(&tr, 0, sizeof(tr));
    for (uint8_t i = 0; i < sz; i++) {
        tr.length = 16;
        tr.tx_buffer = buf + i;
        char bin[17] = ""; 
        uint16ToBinaryStr(bin, buf[0]);
        ESP_LOGI(TAG, "transmit code: %s", bin);
        spi_device_transmit(spiDevice, &tr);
    }
}

void matrixRelayInit(void)
{      
    spi_bus_config_t spiBusConfig = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2,
        .data_io_default_level = false,
        .flags = 0
    };
    
    esp_err_t err = spi_bus_initialize(SPI_HOST, &spiBusConfig, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed initilize spi bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };

    spi_device_interface_config_t spiIfConfig;
    memset(&spiIfConfig, 0, sizeof(spiIfConfig));
    spiIfConfig.spics_io_num = PIN_NUM_CS;
    spiIfConfig.clock_speed_hz = CLOCK_SPEED_HZ;
    spiIfConfig.mode = 0;
    spiIfConfig.queue_size = 1;
    spiIfConfig.flags = SPI_DEVICE_NO_DUMMY;
    
    err = spi_bus_add_device(SPI_HOST, &spiIfConfig, &spiDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed add spi bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };
    //uint16_t buf = 0xFFFF;
    //sendToRelay(&buf, 1);
    ESP_LOGI(TAG, "matrix_relay init finished.");
}