#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"

#define PIN_SERVO            23

#define PIN_LED_VENTANA_1     5
#define PIN_LED_VENTANA_2    17
#define PIN_LED_VENTANA_3    18

#define PIN_LED_PUERTA       19

#define I2C_SDA              21
#define I2C_SCL              22
#define I2C_ADDR             0x08
#define I2C_SLAVE_PORT       I2C_NUM_0

#define PWM_FREQ             5000
#define PWM_RESOLUTION       LEDC_TIMER_8_BIT

#define CANAL_LED_1          LEDC_CHANNEL_0
#define CANAL_LED_2          LEDC_CHANNEL_1
#define CANAL_LED_3          LEDC_CHANNEL_2
#define CANAL_SERVO          LEDC_CHANNEL_3

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

datos_sistema_t datos_recibidos;

int angulo_puerta_cerrada = 0;
int angulo_puerta_abierta = 90;
int angulo_actual_servo = 0;

uint8_t brillo_recibido = 0;
int duty_leds = 0;

void configurar_i2c_esclavo(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = I2C_ADDR
    };

    i2c_param_config(I2C_SLAVE_PORT, &conf);
    i2c_driver_install(I2C_SLAVE_PORT, conf.mode, 1024, 1024, 0);
}

void configurar_pwm(void) {
    ledc_timer_config_t timer_leds = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ledc_timer_config(&timer_leds);

    ledc_channel_config_t led1 = {
        .gpio_num = PIN_LED_VENTANA_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CANAL_LED_1,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };

    ledc_channel_config_t led2 = led1;
    led2.gpio_num = PIN_LED_VENTANA_2;
    led2.channel = CANAL_LED_2;

    ledc_channel_config_t led3 = led1;
    led3.gpio_num = PIN_LED_VENTANA_3;
    led3.channel = CANAL_LED_3;

    ledc_channel_config(&led1);
    ledc_channel_config(&led2);
    ledc_channel_config(&led3);

    ledc_timer_config_t timer_servo = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ledc_timer_config(&timer_servo);

    ledc_channel_config_t servo = {
        .gpio_num = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CANAL_SERVO,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0
    };

    ledc_channel_config(&servo);
}

void configurar_gpio(void) {
    gpio_set_direction(PIN_LED_PUERTA, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED_PUERTA, 0);
}

uint32_t angulo_a_duty_servo(int angulo) {
    int pulso_min_us = 500;
    int pulso_max_us = 2400;
    int periodo_us = 20000;

    int pulso = pulso_min_us + ((pulso_max_us - pulso_min_us) * angulo) / 180;

    uint32_t duty = (pulso * 65535) / periodo_us;

    return duty;
}

void mover_servo(int angulo) {
    uint32_t duty = angulo_a_duty_servo(angulo);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO);

    angulo_actual_servo = angulo;
}

void controlar_leds_pwm(uint8_t brillo) {
    duty_leds = (brillo * 255) / 100;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3);
}

void controlar_luz_puerta(uint8_t estado) {
    gpio_set_level(PIN_LED_PUERTA, estado);
}

void ejecutar_datos_recibidos(void) {
    if (datos_recibidos.reset == SISTEMA_RESET) {
        mover_servo(angulo_puerta_cerrada);
        controlar_leds_pwm(0);
        controlar_luz_puerta(LUZ_APAGADA);
        return;
    }

    if (datos_recibidos.puerta == PUERTA_ABIERTA) {
        mover_servo(angulo_puerta_abierta);
    } else {
        mover_servo(angulo_puerta_cerrada);
    }

    controlar_leds_pwm(datos_recibidos.brillo_leds);
    controlar_luz_puerta(datos_recibidos.luz_puerta);
}

void recibir_datos_i2c(void) {
    int bytes = i2c_slave_read_buffer(
        I2C_SLAVE_PORT,
        (uint8_t *)&datos_recibidos,
        sizeof(datos_recibidos),
        pdMS_TO_TICKS(100)
    );

    if (bytes == sizeof(datos_recibidos)) {
        ejecutar_datos_recibidos();
    }
}

void app_main(void) {
    configurar_gpio();
    configurar_pwm();
    configurar_i2c_esclavo();

    mover_servo(angulo_puerta_cerrada);
    controlar_leds_pwm(0);
    controlar_luz_puerta(LUZ_APAGADA);

    while (1) {
        recibir_datos_i2c();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}