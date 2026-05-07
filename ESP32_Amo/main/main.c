#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/adc.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"

#define PIN_BOTON_ABRIR      25
#define PIN_BOTON_CERRAR     26
#define PIN_BOTON_RESET      27

#define PIN_POTENCIOMETRO    32
#define PIN_LDR              33

#define I2C_SDA              21
#define I2C_SCL              22
#define ESCLAVO1_ADDR        0x08

#define I2C_MASTER_PORT      I2C_NUM_0
#define I2C_FREQ_HZ          100000

#define WIFI_SSID            "FACHADA_WIFI"
#define WIFI_PASS            "12345678"
#define URL_COMANDOS         "http://192.168.4.1/cmd"

typedef enum {
    PUERTA_CERRADA = 0,
    PUERTA_ABIERTA = 1
} estado_puerta_t;

typedef enum {
    LUZ_APAGADA = 0,
    LUZ_ENCENDIDA = 1
} estado_luz_t;

typedef enum {
    SISTEMA_NORMAL = 0,
    SISTEMA_RESET = 1
} estado_reset_t;

typedef struct {
    uint8_t puerta;
    uint8_t brillo_leds;
    uint8_t luz_puerta;
    uint8_t reset;
} datos_sistema_t;

int valor_potenciometro = 0;
int valor_ldr = 0;

uint8_t boton_abrir = 0;
uint8_t boton_cerrar = 0;
uint8_t boton_reset = 0;

uint8_t porcentaje_brillo = 0;
uint8_t estado_puerta = PUERTA_CERRADA;
uint8_t estado_luz_puerta = LUZ_APAGADA;
uint8_t estado_reset = SISTEMA_NORMAL;

int umbral_noche = 1800;

datos_sistema_t datos_a_enviar;

static const char *TAG = "MAESTRO";

void configurar_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BOTON_ABRIR) |
                        (1ULL << PIN_BOTON_CERRAR) |
                        (1ULL << PIN_BOTON_RESET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);
}

void configurar_adc(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);

    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11); // GPIO32
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11); // GPIO33
}

void configurar_i2c_maestro(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ
    };

    i2c_param_config(I2C_MASTER_PORT, &conf);
    i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0);
}

void wifi_init_sta(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "Conectando al ESP32 Wi-Fi...");
}

void leer_entradas(void) {
    boton_abrir = !gpio_get_level(PIN_BOTON_ABRIR);
    boton_cerrar = !gpio_get_level(PIN_BOTON_CERRAR);
    boton_reset = !gpio_get_level(PIN_BOTON_RESET);

    valor_potenciometro = adc1_get_raw(ADC1_CHANNEL_4);
    valor_ldr = adc1_get_raw(ADC1_CHANNEL_5);

    porcentaje_brillo = (valor_potenciometro * 100) / 4095;

    if (valor_ldr < umbral_noche) {
        estado_luz_puerta = LUZ_ENCENDIDA;
    } else {
        estado_luz_puerta = LUZ_APAGADA;
    }

    if (boton_abrir) {
        estado_puerta = PUERTA_ABIERTA;
    }

    if (boton_cerrar) {
        estado_puerta = PUERTA_CERRADA;
    }

    if (boton_reset) {
        estado_reset = SISTEMA_RESET;
        estado_puerta = PUERTA_CERRADA;
        porcentaje_brillo = 0;
        estado_luz_puerta = LUZ_APAGADA;
    } else {
        estado_reset = SISTEMA_NORMAL;
    }
}

void consultar_comandos_wifi(void) {
    char buffer[80] = {0};

    esp_http_client_config_t config = {
        .url = URL_COMANDOS,
        .timeout_ms = 1000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (esp_http_client_open(client, 0) == ESP_OK) {
        int len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);

        if (len > 0) {
            buffer[len] = '\0';

            int puerta, brillo, luz, reset;

            if (sscanf(buffer, "%d,%d,%d,%d", &puerta, &brillo, &luz, &reset) == 4) {
                if (puerta == 0 || puerta == 1) {
                    estado_puerta = puerta;
                }

                if (brillo >= 0 && brillo <= 100) {
                    porcentaje_brillo = brillo;
                }

                if (luz == 0 || luz == 1) {
                    estado_luz_puerta = luz;
                }

                if (reset == 1) {
                    estado_reset = SISTEMA_RESET;
                    estado_puerta = PUERTA_CERRADA;
                    porcentaje_brillo = 0;
                    estado_luz_puerta = LUZ_APAGADA;
                }
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

void preparar_datos(void) {
    datos_a_enviar.puerta = estado_puerta;
    datos_a_enviar.brillo_leds = porcentaje_brillo;
    datos_a_enviar.luz_puerta = estado_luz_puerta;
    datos_a_enviar.reset = estado_reset;
}

void enviar_datos_i2c(void) {
    esp_err_t ret = i2c_master_write_to_device(
        I2C_MASTER_PORT,
        ESCLAVO1_ADDR,
        (uint8_t *)&datos_a_enviar,
        sizeof(datos_a_enviar),
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Error enviando datos por I2C");
    }
}

void actualizar_oled(void) {
    printf("\n--- ESTADO DEL SISTEMA ---\n");
    printf("Puerta: %s\n", estado_puerta ? "Abierta" : "Cerrada");
    printf("Brillo LEDs: %d %%\n", porcentaje_brillo);
    printf("Luz puerta: %s\n", estado_luz_puerta ? "Encendida" : "Apagada");
    printf("LDR: %d\n", valor_ldr);
    printf("Pot: %d\n", valor_potenciometro);
}

void app_main(void) {
    configurar_gpio();
    configurar_adc();
    configurar_i2c_maestro();
    wifi_init_sta();

    while (1) {
        leer_entradas();
        consultar_comandos_wifi();
        preparar_datos();
        enviar_datos_i2c();
        actualizar_oled();

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}