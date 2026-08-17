#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/uart_vfs.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_intr_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/idf_additions.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "hal/uart_types.h"
#include "portmacro.h"
#include "soc/gpio_num.h"
#include "soc/interrupts.h"
#include "soc/uart_struct.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include "esp_intr_alloc.h"

static const char *TAG = "ESP-SPI-MASTER";

#define MISO_IO_NUM 12
#define MOSI_IO_NUM 13
#define SCLK_IO_NUM 14
#define CS_IO_NUM 15

#define MASTER_TO_SLAVE_IRQ_GPIO_NUM GPIO_NUM_4
#define SLAVE_TO_MASTER_IRQ_GPIO_NUM GPIO_NUM_2

#define QUEUE_SIZE 3
#define BUF_SIZE 64

QueueHandle_t input_buf_queue;

DMA_ATTR static uint8_t tx_buf[QUEUE_SIZE][BUF_SIZE];
DMA_ATTR static uint8_t rx_buf[QUEUE_SIZE][BUF_SIZE];
static spi_transaction_t trans[QUEUE_SIZE];

static spi_device_handle_t dev_handle;
static spi_bus_config_t bus_conf  = {
    .mosi_io_num = MOSI_IO_NUM,
    .miso_io_num = MISO_IO_NUM,
    .sclk_io_num = SCLK_IO_NUM,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
};

static spi_device_interface_config_t dev_if_conf = {
    .mode = 0,
    .clock_speed_hz = 1 * 1000 * 1000,
    .spics_io_num = CS_IO_NUM,
    .queue_size = 1,
};

static TaskHandle_t create_dummy_task_handle;
static TaskHandle_t stdin_fgets_task_handle;
static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
volatile static uint8_t irq_output_level = 1;



static int uart_vfs_init() {
    if (!uart_is_driver_installed(UART_NUM_0)) {
        ESP_ERROR_CHECK(uart_driver_install(
            UART_NUM_0,
            256,
            0,
            0,
            NULL,
            0
        ));
    }

    uart_vfs_dev_use_driver(UART_NUM_0);
    return 0;
}

static int spi_init() {
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_conf, SPI_DMA_CH_AUTO));    
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_if_conf, &dev_handle));
    return 0;
}

static int dma_buf_init(){
    for(int i = 0; i < QUEUE_SIZE; i++) {
        memset(&trans[i], 0, sizeof(trans[i]));
        trans[i].length = BUF_SIZE * 8;
        trans[i].tx_buffer = tx_buf[i];
        trans[i].rx_buffer = rx_buf[i];
    }
    return 0;
}

static void stdin_fgets_task(void *arg) {
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;
    char input_buf[64];
    gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
    while(1){
        if(fgets(input_buf, sizeof(input_buf), stdin) == NULL){
            clearerr(stdin);
            continue;
        }
        input_buf[strcspn(input_buf, "\r\n")] = '\0';
        irq_output_level = 0;
        gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        xQueueSendToBack(*input_buf_queue, input_buf, portMAX_DELAY);
        irq_output_level = 1;
        gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// IRAM_ATTR→IRAMに配置する
static void IRAM_ATTR isr_handler(void *arg) {
    if(irq_output_level == 0){
        xTaskNotifyFromISR(stdin_fgets_task_handle, 0, eNoAction, NULL);
    } else {
        xTaskNotifyFromISR(create_dummy_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);
    }
    
}

static int gpio_irq_init() {
    gpio_config_t gpio_slave_to_master_irq_conf = {
        .pin_bit_mask = (1ULL << SLAVE_TO_MASTER_IRQ_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, 
    };

    gpio_config(&gpio_slave_to_master_irq_conf);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(SLAVE_TO_MASTER_IRQ_GPIO_NUM, isr_handler, NULL);


    const gpio_config_t gpio_master_to_slave_irq_conf = {
    .pin_bit_mask = (1ULL << MASTER_TO_SLAVE_IRQ_GPIO_NUM),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};

    gpio_config(&gpio_master_to_slave_irq_conf);
    gpio_set_direction(MASTER_TO_SLAVE_IRQ_GPIO_NUM, GPIO_MODE_OUTPUT);

    return 0;
}

static void create_dummy_task(void *arg) {
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;

    char dummy[64] = "\0";
    while(1){
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        if(gpio_get_level(SLAVE_TO_MASTER_IRQ_GPIO_NUM) == 0){
            xQueueSendToBack(*input_buf_queue, dummy, portMAX_DELAY);
        }
    }
}


static void transaction_task(void *arg){
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;
    BaseType_t result;
    char data[64];
    int trans_index = 0;
    spi_transaction_t *done;
    gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
    while(1) {
        result = xQueueReceive(*input_buf_queue, &data, portMAX_DELAY);
        if(result != pdPASS){
            ESP_LOGE(TAG, "xQueueReceive failed");
            continue;
        }
        memcpy(tx_buf[trans_index], data, sizeof(data));
        
        spi_device_queue_trans(dev_handle, &trans[trans_index], portMAX_DELAY);
        trans_index = (trans_index + 1) % QUEUE_SIZE;
        irq_output_level = 0;
        gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
        spi_device_get_trans_result(dev_handle, &done, portMAX_DELAY);
        irq_output_level = 1;
        gpio_set_level(MASTER_TO_SLAVE_IRQ_GPIO_NUM, irq_output_level);
        printf("%s\n", (char *)done->rx_buffer);
    }
    
}

void app_main(void)
{
    if(uart_vfs_init() != 0) {
        ESP_LOGE(TAG, "uart_vfs_init failed");
    }

    
    if(spi_init() != 0) {
        ESP_LOGE(TAG, "spi_init failed");
    }

    if(dma_buf_init() != 0) {
        ESP_LOGE(TAG, "dma_buf_init failed");
    }

    if(gpio_irq_init() != 0){
        ESP_LOGE(TAG, "irq_gpio_init() failed");
    }

    input_buf_queue = xQueueCreate(10, 64);
    if(input_buf_queue == NULL){
        ESP_LOGE(TAG, "input_buf_queue creation failed");
    }

    xTaskCreate(stdin_fgets_task, "stdin_fgets_task", 4096 , &input_buf_queue, 2, &stdin_fgets_task_handle);
    xTaskCreate(create_dummy_task, "create_dummy_task", 4096, &input_buf_queue, 2, &create_dummy_task_handle);
    xTaskCreate(transaction_task, "transaction_task", 4096, &input_buf_queue , 2, NULL);
}
