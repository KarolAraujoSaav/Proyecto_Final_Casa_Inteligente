// =============================================================================
// ESP32_MAESTRO
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
#include "esp_err.h"
#include "nvs_flash.h"

// ─── PINES BOTONES ────────────────────────────────────────────────────────────
#define PIN_BOTON_RESET      27
#define PIN_BOTON_CERRAR     13
#define PIN_BOTON_ABRIR      25

// ─── PINES ANALÓGICOS ────────────────────────────────────────────────────────
#define PIN_POTENCIOMETRO    32
#define PIN_LDR              33

// ─── I2C HACIA ESP32_ESCLAVO ─────────────────────────────────────────────────
#define I2C_SDA              21
#define I2C_SCL              22
#define ESCLAVO_ADDR         0x08
#define I2C_MASTER_PORT      I2C_NUM_0
#define I2C_FREQ_HZ          100000

// ─── UART HACIA/DESDE ESP32_WIFI ─────────────────────────────────────────────
#define UART_PORT            UART_NUM_1
#define UART_TXD             17
#define UART_RXD             16
#define UART_BAUD            9600
#define UART_BUF_SIZE        256

// ─── UMBRAL LDR ──────────────────────────────────────────────────────────────
// Por debajo de este valor se considera noche → luz puerta ON.
// Ajusta este número según tu LDR y la iluminación del entorno.
#define UMBRAL_NOCHE         3000  // Ajusta este valor: súbelo si el LED no enciende, bájalo si enciende siempre

static const char *TAG = "MAESTRO";

// ─── ENUMERACIONES ───────────────────────────────────────────────────────────
typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// ─── ESTRUCTURA COMPARTIDA CON ESP32_ESCLAVO ─────────────────────────────────
// __attribute__((packed)) garantiza 5 bytes exactos, sin padding del compilador.
typedef struct __attribute__((packed)) {
    uint8_t puerta;         // 0 cerrada, 1 abierta
    uint8_t brillo_leds;    // Intensidad LEDs ventana: 0-100 %
    uint8_t luz_ventana;    // LEDs ventana ON/OFF (independiente del brillo)
    uint8_t luz_puerta;     // LED puerta: controlado por LDR
    uint8_t reset;          // 0 normal, 1 reset
} datos_sistema_t;

// ─── VARIABLES DE ESTADO ─────────────────────────────────────────────────────
static int valor_potenciometro = 0;
static int valor_ldr           = 0;

static uint8_t boton_abrir  = 0;
static uint8_t boton_cerrar = 0;
static uint8_t boton_reset  = 0;

static uint8_t porcentaje_brillo  = 0;
static uint8_t estado_puerta      = PUERTA_CERRADA;

// luz_ventana es independiente: se gestiona desde la web o botón dedicado.
// El potenciómetro solo controla CUÁNTO brilla, no SI está encendida.
static uint8_t estado_luz_ventana = LUZ_APAGADA;

// luz_puerta es exclusivamente automática: la decide el LDR cada ciclo.
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
    ESP_LOGI(TAG, "I2C Maestro iniciado");
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
// LECTURA DE ENTRADAS
// =============================================================================

void leer_entradas(void) {

    boton_abrir  = !gpio_get_level(PIN_BOTON_ABRIR);
    boton_cerrar = !gpio_get_level(PIN_BOTON_CERRAR);
    boton_reset  = !gpio_get_level(PIN_BOTON_RESET);

    valor_potenciometro = adc1_get_raw(ADC1_CHANNEL_4);
    valor_ldr           = adc1_get_raw(ADC1_CHANNEL_5);

    // Potenciómetro → brillo 0-100 %.
    // Si el brillo sube de 0, los LEDs ventana se encienden automáticamente.
    // Si baja a 0, se apagan.
    porcentaje_brillo = (uint8_t)((valor_potenciometro * 100) / 4095);
    if (porcentaje_brillo > 100) porcentaje_brillo = 100;

    estado_luz_ventana = (porcentaje_brillo > 0) ? LUZ_ENCENDIDA : LUZ_APAGADA;

    // LDR → luz puerta automática.
    // Se actualiza SIEMPRE, independientemente de cualquier otra variable.
    estado_luz_puerta = (valor_ldr < UMBRAL_NOCHE)
                        ? LUZ_ENCENDIDA
                        : LUZ_APAGADA;

    // Botones físicos → puerta.
    if (boton_abrir)  estado_puerta = PUERTA_ABIERTA;
    if (boton_cerrar) estado_puerta = PUERTA_CERRADA;

    // Reset físico: apaga todo y fuerza estado seguro.
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

// =============================================================================
// COMANDOS UART (desde ESP32_WIFI)
// =============================================================================

// Formato recibido: "puerta,brillo,luz_ventana,luz_puerta,reset\n"
// Los botones físicos tienen prioridad; si alguno está pulsado se ignora UART.
void consultar_comandos_uart(void) {

    uint8_t buf[UART_BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));

    if (len <= 0) return;

    buf[len] = '\0';

    int p_puerta, p_brillo, p_luz_vent, p_luz_puer, p_reset;

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d",
               &p_puerta, &p_brillo, &p_luz_vent,
               &p_luz_puer, &p_reset) == 5) {

        if (!boton_abrir && !boton_cerrar && !boton_reset) {

            if (p_puerta == 0 || p_puerta == 1)
                estado_puerta = (uint8_t)p_puerta;

            if (p_brillo >= 0 && p_brillo <= 100)
                porcentaje_brillo = (uint8_t)p_brillo;

            // luz_ventana viene explícita desde la web (botón ON/OFF).
            if (p_luz_vent == 0 || p_luz_vent == 1)
                estado_luz_ventana = (uint8_t)p_luz_vent;

            // luz_puerta desde la web solo si no hay lectura LDR confiable;
            // normalmente el LDR ya la actualizó en leer_entradas().
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

// =============================================================================
// PREPARAR STRUCT
// =============================================================================

void preparar_datos(void) {
    datos_a_enviar.puerta      = estado_puerta;
    datos_a_enviar.brillo_leds = porcentaje_brillo;
    datos_a_enviar.luz_ventana = estado_luz_ventana;
    datos_a_enviar.luz_puerta  = estado_luz_puerta;
    datos_a_enviar.reset       = estado_reset;
}

// =============================================================================
// I2C → ESCLAVO
// =============================================================================

void enviar_datos_i2c(void) {
    esp_err_t ret = i2c_master_write_to_device(
        I2C_MASTER_PORT,
        ESCLAVO_ADDR,
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

// =============================================================================
// UART → ESP32_WIFI
// =============================================================================

// Formato enviado: "puerta,brillo,luz_ventana,luz_puerta,reset\n"
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

// =============================================================================
// DEBUG
// =============================================================================

void mostrar_estado(void) {
    printf("\n=============================\n");
    printf("PUERTA       : %s\n",  estado_puerta      ? "ABIERTA" : "CERRADA");
    printf("BRILLO       : %d %%\n", porcentaje_brillo);
    printf("LUZ VENTANA  : %s\n",  estado_luz_ventana ? "ON" : "OFF");
    printf("LUZ PUERTA   : %s\n",  estado_luz_puerta  ? "ON" : "OFF");
    printf("RESET        : %s\n",  estado_reset       ? "SI" : "NO");
    printf("LDR          : %d\n",  valor_ldr);
    printf("POT          : %d\n",  valor_potenciometro);
    printf("=============================\n");
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
        leer_entradas();            // 1. Lee sensores y botones físicos
        consultar_comandos_uart();  // 2. Aplica comandos web (si no hay botón)
        preparar_datos();           // 3. Arma el struct
        enviar_datos_i2c();         // 4. Envía al esclavo
        enviar_estado_uart();       // 5. Reporta estado a la web
        mostrar_estado();           // 6. Debug consola

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
