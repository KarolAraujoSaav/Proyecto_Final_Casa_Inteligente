// =============================================================================
// ESP32_MAESTRO
// IDF v5.2 — usa driver I2C nuevo (driver_ng) compatible con ssd1306.
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/uart.h"

// Driver I2C nuevo (driver_ng) — compatible con ssd1306
#include "driver/i2c_master.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "ssd1306.h"

// --- PINES BOTONES ------------------------------------------------------------
#define PIN_BOTON_RESET      27
#define PIN_BOTON_CERRAR     13
#define PIN_BOTON_ABRIR      25

// --- PINES ANALÓGICOS --------------------------------------------------------
#define PIN_POTENCIOMETRO    32
#define PIN_LDR              33

// --- I2C ---------------------------------------------------------------------
#define I2C_SDA              21
#define I2C_SCL              22
#define I2C_FREQ_HZ          100000
#define ESCLAVO_ADDR         0x08

// --- UART --------------------------------------------------------------------
#define UART_PORT            UART_NUM_1
#define UART_TXD             17
#define UART_RXD             16
#define UART_BAUD            9600
#define UART_BUF_SIZE        256

// --- LDR ---------------------------------------------------------------------
#define UMBRAL_NOCHE         10

static const char *TAG = "MAESTRO";

// --- ENUMERACIONES -----------------------------------------------------------
typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// --- STRUCT COMPARTIDA -------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t puerta;
    uint8_t brillo_leds;
    uint8_t luz_ventana;
    uint8_t luz_puerta;
    uint8_t reset;
} datos_sistema_t;

// --- VARIABLES DE ESTADO -----------------------------------------------------
static int valor_potenciometro = 0;
static int valor_ldr           = 0;

static uint8_t boton_abrir  = 0;
static uint8_t boton_cerrar = 0;
static uint8_t boton_reset  = 0;

static uint8_t porcentaje_brillo  = 0;
static uint8_t estado_puerta      = PUERTA_CERRADA;
static uint8_t estado_luz_ventana = LUZ_APAGADA;
static uint8_t estado_luz_puerta  = LUZ_APAGADA;
static uint8_t estado_reset       = SISTEMA_NORMAL;

static datos_sistema_t datos_a_enviar;

// --- HANDLES I2C (driver nuevo) ----------------------------------------------
// i2c_bus lo gestiona ssd1306 internamente; solo necesitamos el device handle
static i2c_master_dev_handle_t  i2c_esclavo;

// --- OLED --------------------------------------------------------------------
static SSD1306_t oled;

// =============================================================================
// CONFIGURACIONES
// =============================================================================

void configurar_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BOTON_ABRIR)  |
                        (1ULL << PIN_BOTON_CERRAR) |
                        (1ULL << PIN_BOTON_RESET),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

void configurar_adc(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12); // GPIO32 pot
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_12); // GPIO33 LDR
}

// El bus I2C lo crea la librería ssd1306 al llamar i2c_master_init().
// Esta función agrega el esclavo 0x08 al bus que ssd1306 ya creó.
// DEBE llamarse DESPUÉS de i2c_master_init(&oled, ...).
void configurar_i2c_esclavo_en_bus(void) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ESCLAVO_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    // oled._i2c_bus_handle es el handle del bus que creó ssd1306
    ESP_ERROR_CHECK(i2c_master_bus_add_device(oled._i2c_bus_handle, &dev_cfg, &i2c_esclavo));
    ESP_LOGI(TAG, "Esclavo I2C 0x%02X agregado al bus", ESCLAVO_ADDR);
}

void configurar_uart(void) {
    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_PORT, &uart_cfg);
    uart_set_pin(UART_PORT, UART_TXD, UART_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    ESP_LOGI(TAG, "UART lista");
}

// =============================================================================
// LÓGICA
// =============================================================================

void leer_entradas(void) {
    boton_abrir  = !gpio_get_level(PIN_BOTON_ABRIR);
    boton_cerrar = !gpio_get_level(PIN_BOTON_CERRAR);
    boton_reset  = !gpio_get_level(PIN_BOTON_RESET);

    valor_potenciometro = adc1_get_raw(ADC1_CHANNEL_4);
    valor_ldr           = adc1_get_raw(ADC1_CHANNEL_5);

    porcentaje_brillo = (uint8_t)((valor_potenciometro * 100) / 4095);
    if (porcentaje_brillo > 100) porcentaje_brillo = 100;

    // Potenciómetro controla encendido Y brillo de LEDs ventana
    estado_luz_ventana = (porcentaje_brillo > 0) ? LUZ_ENCENDIDA : LUZ_APAGADA;

    // LDR controla LED puerta automáticamente
    estado_luz_puerta = (valor_ldr < UMBRAL_NOCHE) ? LUZ_ENCENDIDA : LUZ_APAGADA;

    if (boton_abrir)  estado_puerta = PUERTA_ABIERTA;
    if (boton_cerrar) estado_puerta = PUERTA_CERRADA;

    if (boton_reset) {
        estado_reset       = SISTEMA_RESET;
        estado_puerta      = PUERTA_CERRADA;
        porcentaje_brillo  = 0;
        estado_luz_ventana = LUZ_APAGADA;
        estado_luz_puerta  = LUZ_APAGADA;
    } else {
        estado_reset = SISTEMA_NORMAL;
    }
}

void consultar_comandos_uart(void) {
    uint8_t buf[UART_BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));
    if (len <= 0) return;

    buf[len] = '\0';
    int p_puerta, p_brillo, p_luz_vent, p_luz_puer, p_reset;

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d",
               &p_puerta, &p_brillo, &p_luz_vent, &p_luz_puer, &p_reset) == 5) {

        if (!boton_abrir && !boton_cerrar && !boton_reset) {
            if (p_puerta == 0 || p_puerta == 1)
                estado_puerta = (uint8_t)p_puerta;
            if (p_brillo >= 0 && p_brillo <= 100) {
                porcentaje_brillo  = (uint8_t)p_brillo;
                estado_luz_ventana = (porcentaje_brillo > 0) ? LUZ_ENCENDIDA : LUZ_APAGADA;
            }
            if (p_luz_vent == 0 || p_luz_vent == 1)
                estado_luz_ventana = (uint8_t)p_luz_vent;
            if (p_luz_puer == 0 || p_luz_puer == 1)
                estado_luz_puerta = (uint8_t)p_luz_puer;
            if (p_reset == 1) {
                estado_reset       = SISTEMA_RESET;
                estado_puerta      = PUERTA_CERRADA;
                porcentaje_brillo  = 0;
                estado_luz_ventana = LUZ_APAGADA;
                estado_luz_puerta  = LUZ_APAGADA;
            }
        }
        ESP_LOGI(TAG, "UART RX -> puerta=%d brillo=%d vent=%d puer=%d reset=%d",
                 p_puerta, p_brillo, p_luz_vent, p_luz_puer, p_reset);
    } else {
        ESP_LOGW(TAG, "UART malformado: %s", buf);
    }
}

void preparar_datos(void) {
    datos_a_enviar.puerta      = estado_puerta;
    datos_a_enviar.brillo_leds = porcentaje_brillo;
    datos_a_enviar.luz_ventana = estado_luz_ventana;
    datos_a_enviar.luz_puerta  = estado_luz_puerta;
    datos_a_enviar.reset       = estado_reset;
}

// Envía el struct al esclavo usando el driver nuevo
void enviar_datos_i2c(void) {
    esp_err_t ret = i2c_master_transmit(
        i2c_esclavo,
        (uint8_t *)&datos_a_enviar,
        sizeof(datos_a_enviar),
        pdMS_TO_TICKS(100)
    );

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C OK -> [%d,%d,%d,%d,%d]",
                 datos_a_enviar.puerta,
                 datos_a_enviar.brillo_leds,
                 datos_a_enviar.luz_ventana,
                 datos_a_enviar.luz_puerta,
                 datos_a_enviar.reset);
    } else {
        ESP_LOGE(TAG, "Error I2C: %s", esp_err_to_name(ret));
    }
}

void enviar_estado_uart(void) {
    char trama[40];
    int n = snprintf(trama, sizeof(trama), "%d,%d,%d,%d,%d\n",
                     estado_puerta, porcentaje_brillo,
                     estado_luz_ventana, estado_luz_puerta, estado_reset);
    uart_write_bytes(UART_PORT, trama, n);
}

// =============================================================================
// OLED — 4 líneas en pantalla 128x32
// =============================================================================

void actualizar_oled(void) {
    char linea[17];

    ssd1306_clear_screen(&oled, false);

    // Línea 0: PUERTA: ABIERTA / CERRADA
    snprintf(linea, sizeof(linea), "PUERTA:%-8s",
             estado_puerta ? "ABIERTA" : "CERRADA");
    ssd1306_display_text(&oled, 0, linea, strlen(linea), false);

    // Línea 1: BRILLO: 75%
    snprintf(linea, sizeof(linea), "BRILLO: %3d%%", porcentaje_brillo);
    ssd1306_display_text(&oled, 1, linea, strlen(linea), false);

    // Línea 2: LUZ P: ON / OFF
    snprintf(linea, sizeof(linea), "LUZ P:  %-4s",
             estado_luz_puerta ? "ON" : "OFF");
    ssd1306_display_text(&oled, 2, linea, strlen(linea), false);

    // Línea 3: DIA / NOCHE según LDR
    snprintf(linea, sizeof(linea), "%s",
             (valor_ldr < UMBRAL_NOCHE) ? "  *** NOCHE ***" : "  ***  DIA  ***");
    ssd1306_display_text(&oled, 3, linea, strlen(linea), false);
}

// =============================================================================
// DEBUG
// =============================================================================

void mostrar_estado(void) {
    printf("\n=============================\n");
    printf("PUERTA      : %s\n",  estado_puerta      ? "ABIERTA" : "CERRADA");
    printf("BRILLO      : %d %%\n", porcentaje_brillo);
    printf("LUZ VENTANA : %s\n",  estado_luz_ventana ? "ON" : "OFF");
    printf("LUZ PUERTA  : %s\n",  estado_luz_puerta  ? "ON" : "OFF");
    printf("RESET       : %s\n",  estado_reset       ? "SI" : "NO");
    printf("LDR         : %d\n",  valor_ldr);
    printf("POT         : %d\n",  valor_potenciometro);
    printf("=============================\n");
}

// =============================================================================
// MAIN
// =============================================================================

void app_main(void) {
    nvs_flash_init();
    configurar_gpio();
    configurar_adc();
    configurar_uart();

    // 1. OLED primero: ssd1306 crea el bus I2C internamente
    i2c_master_init(&oled, I2C_SDA, I2C_SCL, -1);
    ssd1306_init(&oled, 128, 32);
    ssd1306_clear_screen(&oled, false);

    // 2. Ahora agregamos el esclavo al bus que ssd1306 ya creó
    configurar_i2c_esclavo_en_bus();

    ESP_LOGI(TAG, "Sistema maestro iniciado");

    while (1) {
        leer_entradas();
        consultar_comandos_uart();
        preparar_datos();
        enviar_datos_i2c();
        enviar_estado_uart();
        mostrar_estado();
        actualizar_oled();

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
