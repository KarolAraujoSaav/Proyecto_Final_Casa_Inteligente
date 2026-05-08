#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"

// --- Definición de Pines ---
#define PIN_SERVO           23
#define PIN_LED_VENTANA_1    5
#define PIN_LED_VENTANA_2   17
#define PIN_LED_VENTANA_3   18
#define PIN_LED_PUERTA      19

// Pines de Entrada (Botones a GND)
#define PIN_BOTON_ABRIR     25
#define PIN_BOTON_CERRAR    13
#define PIN_BOTON_LUZ       14
#define ADC_POTENCIOMETRO   ADC1_CHANNEL_4 // GPIO 32

// --- Configuración PWM ---
#define PWM_FREQ            5000
#define PWM_RESOLUTION      LEDC_TIMER_8_BIT
#define CANAL_LED_1         LEDC_CHANNEL_0
#define CANAL_LED_2         LEDC_CHANNEL_1
#define CANAL_LED_3         LEDC_CHANNEL_2
#define CANAL_SERVO         LEDC_CHANNEL_3

// Variables globales
int estado_luz_puerta = 0;
int angulo_puerta_cerrada = 0;
int angulo_puerta_abierta = 90;

// --- Configuración de Periféricos ---

void configurar_sistema() {
    // 1. Configurar botones con PULL-UP (Porque están conectados a Tierra)
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_BOTON_ABRIR) | (1ULL << PIN_BOTON_CERRAR) | (1ULL << PIN_BOTON_LUZ),
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Mantiene el pin en 3.3V internamente
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // 2. Configurar LED de puerta
    gpio_set_direction(PIN_LED_PUERTA, GPIO_MODE_OUTPUT);

    // 3. Configurar ADC para Potenciómetro
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_POTENCIOMETRO, ADC_ATTEN_DB_11);

    // 4. Configurar Timers PWM
    ledc_timer_config_t timer_leds = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_leds);

    ledc_timer_config_t timer_servo = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_servo);

    // 5. Configurar Canales PWM
    int pins_leds[] = {PIN_LED_VENTANA_1, PIN_LED_VENTANA_2, PIN_LED_VENTANA_3};
    ledc_channel_t canales[] = {CANAL_LED_1, CANAL_LED_2, CANAL_LED_3};
    
    for(int i = 0; i < 3; i++) {
        ledc_channel_config_t lc = {
            .gpio_num = pins_leds[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = canales[i],
            .timer_sel = LEDC_TIMER_0,
            .duty = 0
        };
        ledc_channel_config(&lc);
    }

    ledc_channel_config_t sc = {
        .gpio_num = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CANAL_SERVO,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0
    };
    ledc_channel_config(&sc);
}

// --- Funciones de Acción ---

void mover_servo(int angulo) {
    int pulso = 500 + ((2400 - 500) * angulo) / 180;
    uint32_t duty = (pulso * 65535) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO);
}

void actualizar_brillo_ventanas(int brillo_8bit) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1, brillo_8bit);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2, brillo_8bit);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3, brillo_8bit);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3);
}

// --- Bucle Principal ---

void app_main(void) {
    configurar_sistema();
    
    int ultimo_estado_btn_luz = 1; // Empieza en 1 por el Pull-up
    mover_servo(angulo_puerta_cerrada);

    while (1) {
        // 1. Control de Brillo (Potenciómetro)
        int raw_adc = adc1_get_raw(ADC_POTENCIOMETRO);
        actualizar_brillo_ventanas(raw_adc * 255 / 4095);

        // 2. Control de Puerta (Lógica Inversa: presionar = 0)
        if (gpio_get_level(PIN_BOTON_ABRIR) == 0) {
            mover_servo(angulo_puerta_abierta);
        }
        if (gpio_get_level(PIN_BOTON_CERRAR) == 0) {
            mover_servo(angulo_puerta_cerrada);
        }

        // 3. Control Toggle Luz Puerta (Lógica Inversa)
        int btn_luz = gpio_get_level(PIN_BOTON_LUZ);
        if (btn_luz == 0 && ultimo_estado_btn_luz == 1) {
            estado_luz_puerta = !estado_luz_puerta;
            gpio_set_level(PIN_LED_PUERTA, estado_luz_puerta);
            vTaskDelay(pdMS_TO_TICKS(200)); // Debounce para evitar rebotes
        }
        ultimo_estado_btn_luz = btn_luz;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}