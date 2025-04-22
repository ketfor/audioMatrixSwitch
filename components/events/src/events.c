#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "events.h"

static const char *TAG = "events";

typedef struct {
    esp_event_base_t event_base;
    int32_t event_id;
    callbackHandler_t callback_handler; 
    void* callback_handler_arg;
} regCallbackHandler_t;

static regCallbackHandler_t* regCallbackHandlers;
static int countCallbackHandler = 0;

#define REG_CALLBACK_HANDLER_SIZE sizeof(regCallbackHandler_t)
#define DATA_QUEUE_ITEM_SIZE sizeof(eventsDataQueueItem_t)
StaticQueue_t dataQueueBuffer;
uint8_t dataQueueStorage[CONFIG_QUEUE_SIZE * DATA_QUEUE_ITEM_SIZE];
static QueueHandle_t dataQueueHandler = NULL;
static eventsDataQueueItem_t* garbageDataQueueItems;
static int countGarbageDataQueueItems = 0;

static void inline __attribute__((always_inline)) dataItemInstanceDelete(eventsDataQueueItem_t* dataItem)
{
    if (dataItem->data_allocated && dataItem->event_data.ptr) {
        free(dataItem->event_data.ptr);
    }
    memset(dataItem, 0, sizeof(*dataItem));
}

static void collectGarbageDataQueueItems(){
    for(int c = 0; c < countGarbageDataQueueItems; c++) {
        eventsDataQueueItem_t dataItem = garbageDataQueueItems[c];
        dataItemInstanceDelete(&dataItem);
    }
    countGarbageDataQueueItems = 0;
}

static void addGarbageDataQueueItem(eventsDataQueueItem_t dataItem){
    garbageDataQueueItems[countGarbageDataQueueItems] = dataItem;
    countGarbageDataQueueItems++;
}

esp_err_t eventsCallbackHandlerRegister(esp_event_base_t event_base, int32_t event_id,
    callbackHandler_t callback_handler, void* callback_handler_arg)
{
    regCallbackHandler_t regCallbackHandler = {
        .event_base = event_base, 
        .event_id = event_id, 
        .callback_handler = callback_handler, 
        .callback_handler_arg = callback_handler_arg
    };
    if (countCallbackHandler < CONFIG_CALLBACK_SIZE){
        regCallbackHandlers[countCallbackHandler] = regCallbackHandler;
        countCallbackHandler++;
    }
    else {
        ESP_LOGE(TAG, "Failed register callback handler");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t eventsCallbackExec(esp_event_base_t event_base, int32_t event_id,
    void* event_data)
{
    for(int c = 0; c < countCallbackHandler; c++){
        regCallbackHandler_t regCallbackHandler = regCallbackHandlers[c];
        if(regCallbackHandler.event_base == event_base && regCallbackHandler.event_id == event_id){
            regCallbackHandler.callback_handler(regCallbackHandler.callback_handler_arg, event_base, event_id, event_data);
        }
    }
    return ESP_OK;
}

BaseType_t eventsDataQueuePost(esp_event_base_t event_base, int32_t event_id,
    void* event_data, size_t event_data_size, TickType_t ticks_to_wait)
{
    collectGarbageDataQueueItems();
    if (dataQueueHandler == NULL){
        ESP_LOGE(TAG, "No QueueHandler");
        return pdFALSE;
    }
    eventsDataQueueItem_t dataItem;
    memset((void*)(&dataItem), 0, sizeof(dataItem));
    if (event_data != NULL && event_data_size != 0) {
        // Make persistent copy of event data on heap.
        void* event_data_copy = calloc(1, event_data_size);

        if (event_data_copy == NULL) {
            return ESP_ERR_NO_MEM;
        }

        memcpy(event_data_copy, event_data, event_data_size);
        dataItem.event_data.ptr = event_data_copy;
        dataItem.data_allocated = true;
        dataItem.data_set = true;
    }
    dataItem.event_base = event_base;
    dataItem.event_id = event_id;
    BaseType_t result = pdFALSE;
    result = xQueueSendToBack(dataQueueHandler, &dataItem, ticks_to_wait);
    if (result != pdTRUE) {
        dataItemInstanceDelete(&dataItem);
        ESP_LOGE(TAG, "Failed to adding data to events queue!");
    }
    return result;
}    

BaseType_t eventsDataQueuePostFromISR(esp_event_base_t event_base, int32_t event_id,
    void* event_data, size_t event_data_size, BaseType_t* task_unblocked)
{
    collectGarbageDataQueueItems();
    if (dataQueueHandler == NULL){
        ESP_LOGE(TAG, "No QueueHandler");
        return pdFALSE;
    }
    eventsDataQueueItem_t dataItem;
    memset((void*)(&dataItem), 0, sizeof(dataItem));
    if (event_data_size > sizeof(dataItem.event_data.val)) {
        ESP_LOGE(TAG, "Invalid event_data_size for adding data to events queue!");
        return ESP_ERR_INVALID_ARG;
    }
    if (event_data != NULL && event_data_size != 0) {
        memcpy((void*)(&(dataItem.event_data.val)), event_data, event_data_size);
        dataItem.data_allocated = false;
        dataItem.data_set = true;
    }
    dataItem.event_base = event_base;
    dataItem.event_id = event_id;
    BaseType_t result = pdFALSE;
    result = xQueueSendToBackFromISR(dataQueueHandler, &dataItem, task_unblocked);
    if (result != pdTRUE) {
        dataItemInstanceDelete(&dataItem);
        ESP_LOGE(TAG, "Failed to adding data to events queue!");
    }
    return result;
}  

BaseType_t eventsDataQueueGet(esp_event_base_t event_base, int32_t event_id,
    event_data_t* event_data, TickType_t ticks_to_wait)
{
    eventsDataQueueItem_t dataItem;
    BaseType_t result = pdFALSE;
    void* data_ptr = NULL;
    if (dataQueueHandler == NULL){
        return pdFALSE;
    }
    if (xQueuePeek(dataQueueHandler, &dataItem, ticks_to_wait) == pdTRUE)
    {
        if(dataItem.event_base == event_base && dataItem.event_id == event_id){
            xQueueReceive(dataQueueHandler, &dataItem, ticks_to_wait);
            if (dataItem.data_set){
                if (dataItem.data_allocated){
                    data_ptr = dataItem.event_data.ptr;
                }
                else {
                    data_ptr = &(dataItem.event_data.val);
                }
                addGarbageDataQueueItem(dataItem);
                result = pdTRUE;
            }
        }
    }
    *event_data = (event_data_t)data_ptr;
    return result;
}

BaseType_t eventsDataQueueGetFromISR(esp_event_base_t event_base, int32_t event_id,
    event_data_t* event_data, BaseType_t *const pxHigherPriorityTaskWoken)
{
    eventsDataQueueItem_t dataItem;
    BaseType_t result = pdFALSE;
    void* data_ptr = NULL;
    if (xQueuePeekFromISR(dataQueueHandler, &dataItem) == pdTRUE)
    {
        if(dataItem.event_base == event_base && dataItem.event_id == event_id){
            xQueueReceiveFromISR(dataQueueHandler, &dataItem, pxHigherPriorityTaskWoken);
            if (dataItem.data_set){
                if (dataItem.data_allocated){
                    data_ptr = dataItem.event_data.ptr;
                }
                else {
                    data_ptr = &(dataItem.event_data.val);
                }
                addGarbageDataQueueItem(dataItem);
                result = pdTRUE;
            }
        }
    }
    *event_data = (event_data_t)data_ptr;
    return result;
}

void eventsInit(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    regCallbackHandlers = (regCallbackHandler_t*)malloc(CONFIG_CALLBACK_SIZE * REG_CALLBACK_HANDLER_SIZE);
    garbageDataQueueItems = (eventsDataQueueItem_t*)malloc(CONFIG_QUEUE_SIZE * DATA_QUEUE_ITEM_SIZE);
    dataQueueHandler = xQueueCreateStatic(CONFIG_QUEUE_SIZE, DATA_QUEUE_ITEM_SIZE, &dataQueueStorage[0], &dataQueueBuffer);
    ESP_LOGI(TAG, "Events init finish");
}