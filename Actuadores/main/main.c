// =============================================================================
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

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"

// ─── PINES ───────────────────────────────────────────────────────────────────
#define PIN_SERVO            23
#define PIN_LED_VENTANA_1     5
#define PIN_LED_VENTANA_2    17
#define PIN_LED_VENTANA_3    18
#define PIN_LED_PUERTA       19

// ─── I2C ESCLAVO ─────────────────────────────────────────────────────────────
#define I2C_SDA              21
#define I2C_SCL              22
#define I2C_ADDR             0x08
#define I2C_SLAVE_PORT       I2C_NUM_0

// ─── PWM LEDs ────────────────────────────────────────────────────────────────
#define PWM_FREQ             5000
#define PWM_RESOLUTION       LEDC_TIMER_8_BIT
#define CANAL_LED_1          LEDC_CHANNEL_0
#define CANAL_LED_2          LEDC_CHANNEL_1
#define CANAL_LED_3          LEDC_CHANNEL_2
#define CANAL_SERVO          LEDC_CHANNEL_3

// ─── ÁNGULOS SERVO ───────────────────────────────────────────────────────────
#define ANGULO_CERRADA       0
#define ANGULO_ABIERTA       90

// ─── ENUMERACIONES COMPARTIDAS ───────────────────────────────────────────────
typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// ─── ESTRUCTURA COMPARTIDA CON ESP32_MAESTRO (I2C) ───────────────────────────
typedef struct {
    uint8_t puerta;         // 0 cerrada, 1 abierta
    uint8_t brillo_leds;    // Intensidad LEDs ventana (0 a 100)
    uint8_t luz_ventana;    // 0 apagada, 1 encendida
    uint8_t luz_puerta;     // Estado LDR puerta: 0 apagada, 1 encendida
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

    // Canales LEDs ventana
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
}

void configurar_i2c_esclavo(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_SLAVE,
        .sda_io_num       = I2C_SDA,
        .scl_io_num       = I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr    = I2C_ADDR
    };
    i2c_param_config(I2C_SLAVE_PORT, &conf);
    i2c_driver_install(I2C_SLAVE_PORT, conf.mode, 1024, 1024, 0);
}

// =============================================================================
// ACTUADORES
// =============================================================================

// Convierte ángulo (0-180°) a duty PWM de 16 bits para el servo.
uint32_t angulo_a_duty_servo(int angulo) {
    int pulso = 500 + ((2400 - 500) * angulo) / 180; // us
    return (uint32_t)((pulso * 65535) / 20000);
}

void mover_servo(int angulo) {
    uint32_t duty = angulo_a_duty_servo(angulo);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO);
}

// Controla el brillo de los tres LEDs de ventana.
// Si luz_ventana == 0, apaga independientemente del brillo.
void controlar_leds_ventana(uint8_t brillo, uint8_t luz_ventana) {
    int duty = (luz_ventana == LUZ_ENCENDIDA) ? (brillo * 255) / 100 : 0;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3);
}

void controlar_luz_puerta(uint8_t estado) {
    gpio_set_level(PIN_LED_PUERTA, estado);
}

// =============================================================================
// LÓGICA DE DATOS RECIBIDOS
// =============================================================================

void ejecutar_datos_recibidos(void) {
    // Reset tiene prioridad absoluta
    if (datos_recibidos.reset == SISTEMA_RESET) {
        mover_servo(ANGULO_CERRADA);
        controlar_leds_ventana(0, LUZ_APAGADA);
        controlar_luz_puerta(LUZ_APAGADA);
        return;
    }

    // Puerta
    if (datos_recibidos.puerta == PUERTA_ABIERTA)
        mover_servo(ANGULO_ABIERTA);
    else
        mover_servo(ANGULO_CERRADA);

    // LEDs ventana: brillo + flag luz_ventana
    controlar_leds_ventana(datos_recibidos.brillo_leds, datos_recibidos.luz_ventana);

    // LED puerta
    controlar_luz_puerta(datos_recibidos.luz_puerta);
}

void recibir_datos_i2c(void) {
    int bytes = i2c_slave_read_buffer(
        I2C_SLAVE_PORT,
        (uint8_t *)&datos_recibidos,
        sizeof(datos_recibidos),
        pdMS_TO_TICKS(100)
    );

    // Solo ejecuta si llegó el paquete completo (5 bytes)
    if (bytes == sizeof(datos_recibidos))
        ejecutar_datos_recibidos();
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

    while (1) {
        recibir_datos_i2c();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}