// =============================================================================
// ESP32_WIFI
// Crea un Access Point WiFi con página web de control.
// Se comunica con el ESP32_MAESTRO por UART:
//   RX_16 ← recibe estado del maestro → actualiza variables para la web
//   TX_17 → envía comandos de la web → maestro los aplica
//
// Protocolo UART compartido con ESP32_MAESTRO (ambos sentidos):
//   "puerta,brillo,luz_ventana,luz_puerta,reset\n"
//   Ejemplo: "1,75,1,0,0\n"
//
// Baud rate UART: 9600
// Red WiFi AP: CASA_INTELIGENTE / 87654321
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_syst…
[4:58 p. m., 8/5/2026] Santiago D. Uni: // =============================================================================
// ESP32_ESCLAVO
// Recibe datos del ESP32_MAESTRO por I2C (dirección 0x08).
// Controla: servo (puerta), LEDs ventana PWM, LED puerta digital.
//
// Estructura recibida datos_sistema_t — 5 bytes:
//   puerta      : 0 cerrada / 1 abierta
//   brillo_leds : 0-100 % → LEDs ventana PWM
//   luz_ventana : 0 apagada / 1 encendida (habilita o silencia el brillo)
//   luz_puerta  : 0 apagada / 1 encendida (LED puerta digital)
//   reset       : 0 normal  / 1 reset total
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/g…
[5:29 p. m., 8/5/2026] Santiago D. Uni: // =============================================================================
// ESP32_MAESTRO
// Recibe entradas físicas (botones, potenciómetro, LDR) y comandos del
// ESP32_WIFI por UART. Envía el estado unificado al ESP32_ESCLAVO por I2C
// y reenvía el estado actual al ESP32_WIFI por UART para que actualice la web.
//
// Protocolo UART compartido con ESP32_WIFI (ambos sentidos):
//   "puerta,brillo,luz_ventana,luz_puerta,reset\n"
//   Ejemplo: "1,75,1,0,0\n"
//
// Baud rate UART: 9600 (igual que ESP32_WIFI)
// I2C hacia esclavo 0x08: SDA=21, SCL=22
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/adc.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "nvs_flash.h"

// ─── PINES BOTONES (PDF: BTN_27, BTN_13, BTN_25) ─────────────────────────────
#define PIN_BOTON_RESET      27   // BTN_27 — activo en LOW con PULLUP
#define PIN_BOTON_CERRAR     13   // BTN_13 — activo en LOW con PULLUP
#define PIN_BOTON_ABRIR      25   // BTN_25 — activo en LOW con PULLUP

// ─── PINES ANALÓGICOS ────────────────────────────────────────────────────────
#define PIN_POTENCIOMETRO    32   // ADC_32 — cursor del potenciómetro
#define PIN_LDR              33   // ADC_33 — sensor LDR

// ─── I2C HACIA ESP32_ESCLAVO ─────────────────────────────────────────────────
#define I2C_SDA              21
#define I2C_SCL              22
#define ESCLAVO_ADDR         0x08
#define I2C_MASTER_PORT      I2C_NUM_0
#define I2C_FREQ_HZ          100000

// ─── UART HACIA/DESDE ESP32_WIFI ─────────────────────────────────────────────
#define UART_PORT            UART_NUM_1
#define UART_TXD             17   // TXD_17 → ESP32_WIFI RX
#define UART_RXD             16   // RXD_16 ← ESP32_WIFI TX
#define UART_BAUD            9600 // Igual que ESP32_WIFI
#define UART_BUF_SIZE        256

// ─── UMBRAL LDR ──────────────────────────────────────────────────────────────
#define UMBRAL_NOCHE         1800

static const char *TAG = "MAESTRO";

// ─── ENUMERACIONES COMPARTIDAS ───────────────────────────────────────────────
typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// ─── ESTRUCTURA COMPARTIDA CON ESP32_ESCLAVO (I2C) ───────────────────────────
typedef struct {
    uint8_t puerta;         // 0 cerrada, 1 abierta
    uint8_t brillo_leds;    // Intensidad LEDs ventana (0 a 100)
    uint8_t luz_ventana;    // 0 apagada, 1 encendida
    uint8_t luz_puerta;     // Estado LDR puerta: 0 apagada, 1 encendida
    uint8_t reset;          // 0 normal, 1 reset
} datos_sistema_t;

// ─── VARIABLES DE ESTADO ─────────────────────────────────────────────────────
static int     valor_potenciometro = 0;
static int     valor_ldr           = 0;

static uint8_t boton_abrir  = 0;
static uint8_t boton_cerrar = 0;
static uint8_t boton_reset  = 0;

static uint8_t porcentaje_brillo  = 0;
static uint8_t estado_puerta      = PUERTA_CERRADA;
static uint8_t estado_luz_ventana = LUZ_APAGADA;
static uint8_t estado_luz_puerta  = LUZ_APAGADA;
static uint8_t estado_reset       = SISTEMA_NORMAL;

static datos_sistema_t datos_a_enviar;

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
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11); // GPIO32 pot
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11); // GPIO33 LDR
}

void configurar_i2c_maestro(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA,
        .scl_io_num       = I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ
    };
    i2c_param_config(I2C_MASTER_PORT, &conf);
    i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0);
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
    uart_set_pin(UART_PORT, UART_TXD, UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    ESP_LOGI(TAG, "UART listo: TX=%d RX=%d @%d baud", UART_TXD, UART_RXD, UART_BAUD);
}

// =============================================================================
// LÓGICA
// =============================================================================

// Lee botones y sensores analógicos.
// Los botones físicos tienen prioridad sobre los comandos WiFi.
void leer_entradas(void) {
    boton_abrir  = !gpio_get_level(PIN_BOTON_ABRIR);
    boton_cerrar = !gpio_get_level(PIN_BOTON_CERRAR);
    boton_reset  = !gpio_get_level(PIN_BOTON_RESET);

    valor_potenciometro = adc1_get_raw(ADC1_CHANNEL_4);
    valor_ldr           = adc1_get_raw(ADC1_CHANNEL_5);

    // Brillo: potenciómetro → 0-100 %
    porcentaje_brillo = (uint8_t)((valor_potenciometro * 100) / 4095);

    // luz_ventana: encendida si hay brillo
    estado_luz_ventana = (porcentaje_brillo > 0) ? LUZ_ENCENDIDA : LUZ_APAGADA;

    // luz_puerta: automática por LDR (oscuridad = encendida)
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

// Recibe comandos del ESP32_WIFI por UART RXD_16.
// Formato: "puerta,brillo,luz_ventana,luz_puerta,reset\n"
// Solo aplica si no hay botón físico activo en este ciclo.
void consultar_comandos_uart(void) {
    uint8_t buf[UART_BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));

    if (len <= 0) return;

    buf[len] = '\0';

    int p_puerta, p_brillo, p_luz_vent, p_luz_puer, p_reset;

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d",
               &p_puerta, &p_brillo, &p_luz_vent, &p_luz_puer, &p_reset) == 5) {

        // Botones físicos tienen prioridad
        if (!boton_abrir && !boton_cerrar && !boton_reset) {

            if (p_puerta == 0 || p_puerta == 1)
                estado_puerta = (uint8_t)p_puerta;

            if (p_brillo >= 0 && p_brillo <= 100) {
                porcentaje_brillo  = (uint8_t)p_brillo;
                estado_luz_ventana = (porcentaje_brillo > 0) ? LUZ_ENCENDIDA : LUZ_APAGADA;
            }

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

        ESP_LOGI(TAG, "CMD WiFi: puerta=%d brillo=%d luz_vent=%d luz_puer=%d reset=%d",
                 p_puerta, p_brillo, p_luz_vent, p_luz_puer, p_reset);
    } else {
        ESP_LOGW(TAG, "Trama UART malformada: %s", buf);
    }
}

void preparar_datos(void) {
    datos_a_enviar.puerta      = estado_puerta;
    datos_a_enviar.brillo_leds = porcentaje_brillo;
    datos_a_enviar.luz_ventana = estado_luz_ventana;
    datos_a_enviar.luz_puerta  = estado_luz_puerta;
    datos_a_enviar.reset       = estado_reset;
}

// Envía struct completo al ESP32_ESCLAVO por I2C.
void enviar_datos_i2c(void) {
    esp_err_t ret = i2c_master_write_to_device(
        I2C_MASTER_PORT,
        ESCLAVO_ADDR,
        (uint8_t *)&datos_a_enviar,
        sizeof(datos_a_enviar),
        pdMS_TO_TICKS(100)
    );
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "Error I2C al esclavo 0x%02X", ESCLAVO_ADDR);
}

// Envía estado al ESP32_WIFI por UART TXD_17 para actualizar la página web.
// Formato: "puerta,brillo,luz_ventana,luz_puerta,reset\n"
void enviar_estado_uart(void) {
    char trama[40];
    int n = snprintf(trama, sizeof(trama), "%d,%d,%d,%d,%d\n",
                     estado_puerta,
                     porcentaje_brillo,
                     estado_luz_ventana,
                     estado_luz_puerta,
                     estado_reset);
    uart_write_bytes(UART_PORT, trama, n);
}

void mostrar_estado(void) {
    printf("\n--- ESTADO DEL SISTEMA ---\n");
    printf("Puerta     : %s\n",    estado_puerta      ? "Abierta"   : "Cerrada");
    printf("Brillo     : %d %%\n", porcentaje_brillo);
    printf("Luz ventana: %s\n",    estado_luz_ventana ? "Encendida" : "Apagada");
    printf("Luz puerta : %s\n",    estado_luz_puerta  ? "Encendida" : "Apagada");
    printf("LDR        : %d\n",    valor_ldr);
    printf("Pot        : %d\n",    valor_potenciometro);
}

// =============================================================================
// MAIN
// =============================================================================

void app_main(void) {
    nvs_flash_init();

    configurar_gpio();
    configurar_adc();
    configurar_i2c_maestro();
    configurar_uart();

    ESP_LOGI(TAG, "Sistema maestro iniciado");

    while (1) {
        leer_entradas();           // 1. Lee botones y sensores físicos
        consultar_comandos_uart(); // 2. Aplica comandos WiFi (si no hay botón)
        preparar_datos();          // 3. Arma el struct unificado
        enviar_datos_i2c();        // 4. Envía al ESP32_ESCLAVO por I2C
        enviar_estado_uart();      // 5. Envía estado al ESP32_WIFI por UART
        mostrar_estado();          // 6. Debug por consola

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}