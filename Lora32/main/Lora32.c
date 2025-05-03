#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lora.h"
#include "esp_http_client.h"
#include "protocol_examples_common.h"

#define GPS_UART_NUM UART_NUM_2
#define GPS_TXD_PIN 12
#define GPS_RXD_PIN 13
#define GPS_BUF_SIZE 1024
#define MESSAGE_LEN 240

// Define el rol del dispositivo: TRANSMITTER o RECEIVER
#define DEVICE_ROLE_RECEIVER 0 // Cambiar a 0 para el transmisor

static const char *TAG = "GPS";
SSD1306_t screen;
TaskHandle_t xHandleRXTask;
int packets = 0;
int rssi = 0;
uint8_t msg[MESSAGE_LEN];

void screen_init()
{
    i2c_master_init(&screen, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&screen, 128, 64);
    ssd1306_contrast(&screen, 0xFF);
}

void screen_clear()
{
    ssd1306_clear_screen(&screen, false);
}

void screen_print(char *message, int page)
{
    ssd1306_clear_line(&screen, page, false);
    ssd1306_display_text(&screen, page, message, strlen(message), false);
}

void send_msg(char *msg, int size)
{
    printf("Send packet: %s\n", msg);
    vTaskSuspend(xHandleRXTask);
    lora_send_packet((uint8_t *) msg, size);
    vTaskResume(xHandleRXTask);
    screen_print("Send Messsage",2);
    vTaskDelay(pdMS_TO_TICKS(50)); // Reducir el tiempo de espera
}
#if DEVICE_ROLE_RECEIVER
void send_coordinates_to_server(float lat, float lon)
{
    char post_data[128];
    snprintf(post_data, sizeof(post_data), "latitude=%.6f&longitude=%.6f", lat, lon);

    esp_http_client_config_t config = {
        .url = "http://192.168.137.1:5000/",
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
#endif

void task_rx(void *p)
{
    int len;
    char packets_count[64];
    char rssi_str[64];
    for (;;)
    {
        lora_receive();
        while (lora_received())
        {
            len = lora_receive_packet(msg, MESSAGE_LEN);
            if (len > 0)
            {
                msg[len] = 0;

                printf("Receive packet: %s, len: %d\n", msg, len);

                char *comma_ptr = strchr((char *)msg, ','); // Buscar la coma que separa latitud y longitud

                if (comma_ptr)
                {
                    *comma_ptr = '\0';
                    char *lat_ptr = (char *)msg;
                    char *lon_ptr = comma_ptr + 1;

                    float lat = atof(lat_ptr);
                    float lon = atof(lon_ptr);

                    char message[64];
                    snprintf(message, sizeof(message), "%.6f,%.6f", lat, lon);

                    screen_print("Lat:", 2);
                    screen_print(lat_ptr, 4);
                    screen_print("Lon:", 5);
                    screen_print(lon_ptr, 6);

                    rssi = lora_packet_rssi();
                    sprintf(rssi_str, "RSSI: %d dBm", rssi);
                    ESP_LOGI(TAG, "%s", rssi_str);
                    sprintf(packets_count, "Count: %d", packets);
                    ESP_LOGI(TAG, "%s", packets_count);

#if DEVICE_ROLE_RECEIVER
                    send_coordinates_to_server(lat, lon);
#endif
                }
                else
                {
                    screen_print("Formato invalido", 2);
                    screen_print((char *)msg, 3);
                    screen_clear();
                }

                packets++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void lora_config_init()
{
    printf("Lora config init\n");
    lora_init();
    lora_set_frequency(433e6);
    lora_set_spreading_factor(10); // Aumentar el SF para mayor alcance
    lora_set_bandwidth(62.5e3); // Reducir el ancho de banda para mayor sensibilidad
    lora_set_preamble_length(15);
    lora_set_tx_power(17); // Configurar la potencia máxima permitida
    lora_enable_crc();
    xTaskCreate(&task_rx, "task_rx", 4096, NULL, 5, &xHandleRXTask);
    //xTaskCreate(&task_tx, "task_tx", 2048, NULL, 5, NULL);
}
// Función para convertir lat/lon NMEA (ddmm.mmmm) a decimal
float nmea_to_decimal(const char *nmea_coord, bool is_lat)
{
    if (strlen(nmea_coord) < 4)
        return 0.0;

    char degs[4] = {0}; // longitud puede usar 3 dígitos
    char mins[10] = {0};

    if (is_lat)
    {
        // Latitud: 2 grados
        strncpy(degs, nmea_coord, 2);
        strncpy(mins, nmea_coord + 2, sizeof(mins) - 1);
    }
    else
    {
        // Longitud: 3 grados
        strncpy(degs, nmea_coord, 3);
        strncpy(mins, nmea_coord + 3, sizeof(mins) - 1);
    }

    float deg = atof(degs);
    float min = atof(mins);

    return deg + (min / 60.0);
}

void parse_nmea_sentence(const char *line)
{
    char packets_count[64];
    if (strstr(line, "$GPRMC") != NULL)
    {
        char buf[128];
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *token;
        int field = 0;
        char lat_raw[16] = {0}, lat_dir = 'N';
        char lon_raw[16] = {0}, lon_dir = 'E';

        token = strtok(buf, ",");
        while (token != NULL)
        {
            field++;
            if (field == 4)
                strncpy(lat_raw, token, sizeof(lat_raw) - 1);
            if (field == 5)
                lat_dir = token[0];
            if (field == 6)
                strncpy(lon_raw, token, sizeof(lon_raw) - 1);
            if (field == 7)
                lon_dir = token[0];
            token = strtok(NULL, ",");
        }

        if (strlen(lat_raw) > 0 && strlen(lon_raw) > 0)
        {
            float lat = nmea_to_decimal(lat_raw, true);
            float lon = nmea_to_decimal(lon_raw, false);

            // Aplicar dirección (N/S, E/W)
            if (lat_dir == 'S')
                lat *= -1;
            if (lon_dir == 'W')
                lon *= -1;

            ESP_LOGI(TAG, "Coordenadas: Lat: %.6f, Lon: %.6f", lat, lon);
            ESP_LOGI(TAG, "Mapa: https://maps.google.com/?q=%.6f,%.6f", lat, lon);
            
            // Enviar el mensaje usando LoRa
            char message[64];
            snprintf(message, sizeof(message), "%.6f,%.6f", lat, lon);
            send_msg(message, strlen(message));
            sprintf(packets_count, "Count: %d", packets);
            screen_print(packets_count, 4);
            
        }
        packets++;
    }
}

void gps_task(void *arg)
{
    uint8_t data[GPS_BUF_SIZE];

    while (1)
    {
        int len = uart_read_bytes(GPS_UART_NUM, data, GPS_BUF_SIZE - 1, pdMS_TO_TICKS(1000));
        if (len > 0)
        {
            data[len] = '\0';
            char *line = strtok((char *)data, "\n");
            while (line != NULL)
            {
                parse_nmea_sentence(line);
                line = strtok(NULL, "\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
//Configurar el uart para gps
uart_config_t uart_config_init(int baud_rate, uart_word_length_t data_bits, uart_parity_t parity, uart_stop_bits_t stop_bits, uart_hw_flowcontrol_t flow_ctrl)
{
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = data_bits,
        .parity = parity,
        .stop_bits = stop_bits,
        .flow_ctrl = flow_ctrl
    };
    return uart_config;
}
//Setup de el aurt
void uart_setup(uart_port_t uart_num, const uart_config_t *uart_config, int tx_pin, int rx_pin, int buf_size)
{
    uart_driver_install(uart_num, buf_size * 2, 0, 0, NULL, 0);
    uart_param_config(uart_num, uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if DEVICE_ROLE_RECEIVER
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    printf("Conectado a la red Wi-Fi...\n");
#endif

    uart_config_t uart_config = uart_config_init(9600, UART_DATA_8_BITS, 
        UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE);

    uart_setup(GPS_UART_NUM, &uart_config, GPS_TXD_PIN, GPS_RXD_PIN, GPS_BUF_SIZE);

    screen_init();
    screen_clear();
    lora_config_init();
    
    // Crear la tarea para procesar datos GPS
    xTaskCreate(gps_task, "gps_task", 4096, NULL, 10, NULL);
}
