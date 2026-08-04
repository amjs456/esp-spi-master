#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "hal/spi_types.h"
#include <stdint.h>
#include <string.h>

#define MISO_IO_NUM 12
#define MOSI_IO_NUM 13
#define SCLK_IO_NUM 14
#define CS_IO_NUM 15


void app_main(void)
{
    spi_device_handle_t dev_handle;
    spi_bus_config_t bus_conf = {
        .mosi_io_num = MOSI_IO_NUM,
        .miso_io_num = MISO_IO_NUM,
        .sclk_io_num = SCLK_IO_NUM,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_device_interface_config_t dev_if_conf = {
        .mode = 0,
        .clock_speed_hz = 1 * 1000 * 1000,
        .spics_io_num = CS_IO_NUM,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_conf, SPI_DMA_CH_AUTO));    
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_if_conf, &dev_handle));

    uint8_t *tx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 8, 0);
    if(tx_buf == NULL) {
        return;
    }
    char tx_data[8] = "hello";
    memcpy(tx_buf, tx_data, sizeof(tx_data));

    uint8_t *rx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 8, 0);
    if(rx_buf == NULL) {
        return;
    }
    
    while(1){
        spi_transaction_t transaction = {
        .flags = 0,
        .tx_buffer = tx_buf,
        .length = sizeof(tx_data) * 8,
        .rx_buffer = rx_buf,
        .rxlength = 0,
        };
        
        ESP_ERROR_CHECK(spi_device_transmit(dev_handle, &transaction));
        printf("%s\n", rx_buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
