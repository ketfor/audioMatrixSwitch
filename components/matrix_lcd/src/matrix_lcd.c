#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "matrix_lcd.h"
#include "matrix_lcd_types.h"

static const char *TAG = "matrix_lcd";

#define PIN_NUM_SDA CONFIG_PIN_NUM_SDA
#define PIN_NUM_SCL CONFIG_PIN_NUM_SCL
#define I2C_PORT I2C_NUM_0
#define CLOCK_SPEED_HZ (100000) // 100 KHz

#define LCD_ADDR                0x27
#define LCD_COLS                16
#define LCD_ROWS                2

static i2c_master_dev_handle_t i2cDevice = NULL;

static void lcdPulseEnable(uint8_t data)
{
    uint8_t buf = data | LCD_ENABLE;
    i2c_master_transmit(i2cDevice, &buf, 1, -1);
    ets_delay_us(1);
    buf = data & ~LCD_ENABLE;
    i2c_master_transmit(i2cDevice, &buf, 1, -1);
    ets_delay_us(500);
}

static void lcdWriteNibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data = (nibble & 0xF0) | mode | LCD_BACKLIGHT;
    i2c_master_transmit(i2cDevice, &data, 1, -1);   
    lcdPulseEnable(data);                                              // Clock data into LCD
}

static void lcdWriteByte(uint8_t data, uint8_t mode)
{
    lcdWriteNibble(data & 0xF0, mode);
    lcdWriteNibble((data << 4) & 0xF0, mode);
}

void lcdSetCursor(uint8_t col, uint8_t row)
{
    if (row > LCD_ROWS - 1) {
        ESP_LOGE(TAG, "Cannot write to row %d. Please select a row in the range (0, %d)", row, LCD_ROWS - 1);
        row = LCD_ROWS - 1;
    }
    uint8_t row_offsets[] = {LCD_LINEONE, LCD_LINETWO, LCD_LINETHREE, LCD_LINEFOUR};
    lcdWriteByte(LCD_SET_DDRAM_ADDR | (col + row_offsets[row]), LCD_COMMAND);
}

void lcdWriteChar(char c)
{
    lcdWriteByte(c, LCD_WRITE);                                        // Write data to DDRAM
}

void lcdWriteStr(const char* str)
{
    while (*str) {
        lcdWriteChar(*str++);
    }
}

void lcdHome(void)
{
    lcdWriteByte(LCD_HOME, LCD_COMMAND);
    ets_delay_us(2000);                                              // This command takes a while to complete
}

void lcdClearScreen(void)
{
    lcdWriteByte(LCD_CLEAR, LCD_COMMAND);
    ets_delay_us(2000);                                                // This command takes a while to complete
}

static void lcdInit(void)
{
    vTaskDelay(50 / portTICK_PERIOD_MS);                                 // Initial 50 mSec delay
    lcdWriteNibble(LCD_BACKLIGHT, LCD_COMMAND); 
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // Reset the LCD controller
    lcdWriteNibble(LCD_FUNCTION_RESET, LCD_COMMAND);                   // First part of reset sequence
    ets_delay_us(4500);                                                  // min 4.1 mS delay (min)
    lcdWriteNibble(LCD_FUNCTION_RESET, LCD_COMMAND);                   // second part of reset sequence
    ets_delay_us(4500);                              // min 4.1 mS delay (min)
    lcdWriteNibble(LCD_FUNCTION_RESET, LCD_COMMAND);                   // third part of reset sequence
    ets_delay_us(150);
    lcdWriteNibble(LCD_FUNCTION_SET_4BIT, LCD_COMMAND);                // Activate 4-bit mode
    ets_delay_us(80);                                                  // 40 uS delay (min)
    // --- Busy flag now available ---
    // Function Set instruction
    lcdWriteByte(LCD_FUNCTION_SET_4BIT, LCD_COMMAND);                  // Set mode, lines, and font
    ets_delay_us(80);

    lcdWriteByte(LCD_DISPLAY_ON, LCD_COMMAND);

    // Clear Display instruction
    lcdWriteByte(LCD_CLEAR, LCD_COMMAND);                              // clear display RAM
    ets_delay_us(1000);                                                // Clearing memory takes a bit longer
    
    // Entry Mode Set instruction
    lcdWriteByte(LCD_ENTRY_MODE, LCD_COMMAND);                         // Set desired shift characteristics
    ets_delay_us(80);
    
}

void matrixLcdInit(void)
{
    i2c_master_bus_config_t i2cBusConfig = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_NUM_SDA,
        .scl_io_num = PIN_NUM_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = 1,
        .glitch_ignore_cnt = 7,            
        .intr_priority = 0,                
        .trans_queue_depth = 0
    };

    i2c_master_bus_handle_t i2cBusHandle;

    esp_err_t err = i2c_new_master_bus(&i2cBusConfig, &i2cBusHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed initilize i2c bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };

    i2c_device_config_t i2cDeviceConfig = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_ADDR,
        .scl_speed_hz = CLOCK_SPEED_HZ,
        .scl_wait_us = 0,
        .flags.disable_ack_check = 1
    };

    err = i2c_master_bus_add_device(i2cBusHandle, &i2cDeviceConfig, &i2cDevice);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed add i2c bus, err: %d (%s)", err, esp_err_to_name(err));
        return;
    };

    lcdInit();
    lcdHome();
    ESP_LOGI(TAG, "matrix_lcd init finished.");
}