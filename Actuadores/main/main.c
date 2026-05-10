// =============================================================================
// ESP32_ESCLAVO
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"

#include "esp_log.h"
#include "esp_err.h"

// ─── PINES ───────────────────────────────────────────────────────────────────
#define PIN_SERVO             23
#define PIN_LED_VENTANA_1      5
#define PIN_LED_VENTANA_2     17
#define PIN_LED_VENTANA_3     18
#define PIN_LED_PUERTA        19

// ─── I2C ESCLAVO ─────────────────────────────────────────────────────────────
#define I2C_SDA               21
#define I2C_SCL               22
#define I2C_ADDR              0x08
#define I2C_SLAVE_PORT        I2C_NUM_0

// ─── PWM ─────────────────────────────────────────────────────────────────────
#define PWM_FREQ              5000
#define PWM_RESOLUTION        LEDC_TIMER_8_BIT

#define CANAL_LED_1           LEDC_CHANNEL_0
#define CANAL_LED_2           LEDC_CHANNEL_1
#define CANAL_LED_3           LEDC_CHANNEL_2
#define CANAL_SERVO           LEDC_CHANNEL_3

// ─── SERVO ───────────────────────────────────────────────────────────────────
#define ANGULO_CERRADA        0
#define ANGULO_ABIERTA        90

static const char *TAG = "ESCLAVO";

// ─── ENUMERACIONES ───────────────────────────────────────────────────────────
typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// ─── ESTRUCTURA COMPARTIDA CON ESP32_MAESTRO ─────────────────────────────────
// __attribute__((packed)) = exactamente 5 bytes, sin padding del compilador.
typedef struct __attribute__((packed)) {
    uint8_t puerta;         // 0 cerrada, 1 abierta
    uint8_t brillo_leds;    // Intensidad LEDs ventana: 0-100 %
    uint8_t luz_ventana;    // LEDs ventana ON/OFF
    uint8_t luz_puerta;     // LED puerta ON/OFF (resultado del LDR)
    uint8_t reset;          // 0 normal, 1 reset
} datos_sistema_t;

static datos_sistema_t datos_recibidos;

// =============================================================================
// CONFIGURACIONES
// =============================================================================

void configurar_gpio(void) {
    gpio_set_direction(PIN_LED_PUERTA, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED_PUERTA, 0);
}

void configurar_pwm(void) {

    // Timer 0 — LEDs ventana (8 bits, 5 kHz)
    ledc_timer_config_t timer_leds = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz         = PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_leds);

    ledc_channel_config_t led1 = {
        .gpio_num   = PIN_LED_VENTANA_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = CANAL_LED_1,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0
    };
    ledc_channel_config_t led2 = led1;
    led2.gpio_num = PIN_LED_VENTANA_2;
    led2.channel  = CANAL_LED_2;

    ledc_channel_config_t led3 = led1;
    led3.gpio_num = PIN_LED_VENTANA_3;
    led3.channel  = CANAL_LED_3;

    ledc_channel_config(&led1);
    ledc_channel_config(&led2);
    ledc_channel_config(&led3);

    // Timer 1 — Servo (16 bits, 50 Hz)
    ledc_timer_config_t timer_servo = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz         = 50,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_servo);

    ledc_channel_config_t servo = {
        .gpio_num   = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = CANAL_SERVO,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0,
        .hpoint     = 0
    };
    ledc_channel_config(&servo);

    ESP_LOGI(TAG, "PWM configurado");
}

void configurar_i2c_esclavo(void) {
    i2c_config_t conf = {
        .mode                = I2C_MODE_SLAVE,
        .sda_io_num          = I2C_SDA,
        .scl_io_num          = I2C_SCL,
        .sda_pullup_en       = GPIO_PULLUP_ENABLE,
        .scl_pullup_en       = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr    = I2C_ADDR
    };
    i2c_param_config(I2C_SLAVE_PORT, &conf);
    i2c_driver_install(I2C_SLAVE_PORT, conf.mode, 1024, 1024, 0);
    ESP_LOGI(TAG, "I2C esclavo iniciado en 0x%02X", I2C_ADDR);
}

// =============================================================================
// SERVO
// =============================================================================

uint32_t angulo_a_duty_servo(int angulo) {
    if (angulo < 0)   angulo = 0;
    if (angulo > 180) angulo = 180;
    // Rango típico: 500 µs (0°) a 2400 µs (180°), periodo 20 000 µs
    uint32_t min_duty = 1638;  // 500/20000 * 65535
    uint32_t max_duty = 7864;  // 2400/20000 * 65535
    return min_duty + ((max_duty - min_duty) * (uint32_t)angulo) / 180;
}

void mover_servo(int angulo) {
    uint32_t duty = angulo_a_duty_servo(angulo);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO);
    ESP_LOGI(TAG, "Servo -> %d grados (duty=%lu)", angulo, duty);
}

// =============================================================================
// LEDs VENTANA
// =============================================================================

// Aplica brillo a los tres LEDs de ventana.
// - Si luz_ventana == LUZ_APAGADA  → duty 0 (LEDs apagados).
// - Si luz_ventana == LUZ_ENCENDIDA → duty proporcional al brillo (0-100 %).
// El brillo y el encendido son INDEPENDIENTES: brillo=80 + luz=OFF = apagado.
void controlar_leds_ventana(uint8_t brillo, uint8_t luz_ventana) {

    if (brillo > 100) brillo = 100;

    int duty = (luz_ventana == LUZ_ENCENDIDA)
               ? (brillo * 255) / 100
               : 0;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3);

    ESP_LOGI(TAG, "LEDs ventana -> %s brillo=%d%% duty=%d",
             luz_ventana ? "ON" : "OFF", brillo, duty);
}

// =============================================================================
// LED PUERTA
// =============================================================================

// Enciende o apaga el LED de puerta directamente.
// Estado viene del campo luz_puerta, que el maestro calcula con el LDR.
void controlar_luz_puerta(uint8_t estado) {
    gpio_set_level(PIN_LED_PUERTA, estado);
    ESP_LOGI(TAG, "LED puerta -> %s", estado ? "ON" : "OFF");
}

// =============================================================================
// EJECUTAR DATOS RECIBIDOS
// =============================================================================

void ejecutar_datos_recibidos(void) {

    // ── RESET: prioridad absoluta ─────────────────────────────────────────────
    if (datos_recibidos.reset == SISTEMA_RESET) {
        ESP_LOGW(TAG, "RESET ACTIVADO");
        mover_servo(ANGULO_CERRADA);
        controlar_leds_ventana(0, LUZ_APAGADA);
        controlar_luz_puerta(LUZ_APAGADA);
        return;
    }

    // ── SERVO / PUERTA ────────────────────────────────────────────────────────
    // El servo se mueve SIEMPRE según el campo puerta, sin condiciones extra.
    if (datos_recibidos.puerta == PUERTA_ABIERTA)
        mover_servo(ANGULO_ABIERTA);
    else
        mover_servo(ANGULO_CERRADA);

    // ── LEDs VENTANA ──────────────────────────────────────────────────────────
    // Se pasa brillo Y luz_ventana; la función decide el duty final.
    controlar_leds_ventana(datos_recibidos.brillo_leds,
                           datos_recibidos.luz_ventana);

    // ── LED PUERTA ────────────────────────────────────────────────────────────
    // Se aplica directamente el campo luz_puerta (calculado con LDR en maestro).
    controlar_luz_puerta(datos_recibidos.luz_puerta);
}

// =============================================================================
// RECEPCIÓN I2C
// =============================================================================

void recibir_datos_i2c(void) {

    int bytes = i2c_slave_read_buffer(
        I2C_SLAVE_PORT,
        (uint8_t *)&datos_recibidos,
        sizeof(datos_recibidos),
        pdMS_TO_TICKS(100)
    );

    if (bytes > 0)
        ESP_LOGI(TAG, "Bytes recibidos: %d", bytes);

    // Con packed el struct mide exactamente 5 bytes.
    // Se acepta >= por si el maestro añade bytes en el futuro.
    if (bytes >= (int)sizeof(datos_recibidos)) {
        ESP_LOGI(TAG, "Datos -> [puerta=%d brillo=%d vent=%d puer=%d reset=%d]",
                 datos_recibidos.puerta,
                 datos_recibidos.brillo_leds,
                 datos_recibidos.luz_ventana,
                 datos_recibidos.luz_puerta,
                 datos_recibidos.reset);
        ejecutar_datos_recibidos();
    } else if (bytes > 0) {
        ESP_LOGW(TAG, "Paquete I2C incompleto: %d/%d bytes",
                 bytes, (int)sizeof(datos_recibidos));
    }
}

// =============================================================================
// MAIN
// =============================================================================

void app_main(void) {
    configurar_gpio();
    configurar_pwm();
    configurar_i2c_esclavo();

    // Estado inicial seguro
    mover_servo(ANGULO_CERRADA);
    controlar_leds_ventana(0, LUZ_APAGADA);
    controlar_luz_puerta(LUZ_APAGADA);

    ESP_LOGI(TAG, "Sistema esclavo iniciado");

    while (1) {
        recibir_datos_i2c();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}