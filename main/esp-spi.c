#include "driver/spi_common.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "hal/spi_types.h"
#include "hal/uart_types.h"
#include "soc/uart_struct.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MISO_IO_NUM 12
#define MOSI_IO_NUM 13
#define SCLK_IO_NUM 14
#define CS_IO_NUM 15


void app_main(void)
{
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

    uint8_t *tx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 64, 0);
    if(tx_buf == NULL) {
        return;
    }
    uint8_t *rx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 64, 0);
    if(rx_buf == NULL) {
        return;
    }

    char input_buf[64];
    
    while(1){
        if(fgets(input_buf, sizeof(input_buf), stdin) == NULL){
            clearerr(stdin);
            continue;
        }
        input_buf[strcspn(input_buf, "\r\n")] = '\0';
        memcpy(tx_buf, input_buf, sizeof(input_buf));
        spi_transaction_t transaction = {
        .flags = 0,
        .tx_buffer = tx_buf,
        .length = sizeof(input_buf) * 8,
        .rx_buffer = rx_buf,
        .rxlength = 0,
        };
        
        ESP_ERROR_CHECK(spi_device_transmit(dev_handle, &transaction));
        printf("%s\n", rx_buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
