//PROGRAMA ESP32 ESCLAVO/ACTUADORES

//Librerias
#include <stdio.h>
#include <string.h> //Por si se reciben comandos tipo texto
#include "freertos/FreeRTOS.h" //Para controlar tareas y tiempos
#include "freertos/task.h"
#include "driver/gpio.h" //Para pines GPIO
#include "driver/i2c.h" //Para utilizar comunicacion I2C
#include "driver/ledc.h" //Para PWM para controlar LEDs y Servo


// Definici[on de pines, cambiar de acuerdo al hardware
#define PIN_SERVO            23
#define PIN_LED_VENTANA_1     5
#define PIN_LED_VENTANA_2    17
#define PIN_LED_VENTANA_3    18
#define PIN_LED_PUERTA       19
#define I2C_SDA              21  //Para comunicaci[on I2C, estos reciben info. Del maestro
#define I2C_SCL              22
#define I2C_ADDR             0x08 // Dirección I2C del Esclavo 1. El Maestro debe enviar datos a esta dirección.
#define I2C_SLAVE_PORT       I2C_NUM_0 //Puerto I2C utilizado
#define PWM_FREQ             5000 //Frecuencia PWM para los LEDs, esta no es visible para el ojo humano
#define PWM_RESOLUTION       LEDC_TIMER_8_BIT //8 bits, valores de 0 a 255

// Definicion de canales PWM para cada LED y para el servo.
#define CANAL_LED_1          LEDC_CHANNEL_0
#define CANAL_LED_2          LEDC_CHANNEL_1
#define CANAL_LED_3          LEDC_CHANNEL_2
#define CANAL_SERVO          LEDC_CHANNEL_3

// Enumeración para representar el estado de la puerta. 0 significa cerrada y 1 significa abierta.
typedef enum {
    PUERTA_CERRADA = 0,
    PUERTA_ABIERTA = 1
} estado_puerta_t;

// Enumeración para representar el estado de una luz.0 apagada, 1 encendida.
typedef enum {
    LUZ_APAGADA = 0,
    LUZ_ENCENDIDA = 1
} estado_luz_t;

// Enumeración para representar el estado general del sistema. 0 normal, 1 reinicio.
typedef enum {
    SISTEMA_NORMAL = 0,
    SISTEMA_RESET = 1
} estado_reset_t;

// Paquete de datos que el Maestro envía al Esclavo 1 por I2C.
typedef struct {
    uint8_t puerta;       // Estado deseado de la puerta: 0 cerrada, 1 abierta.
    uint8_t brillo_leds;  // Brillo de los LEDs de ventana: 0 a 100%.
    uint8_t luz_puerta;   // Estado del LED de puerta: 0 apagado, 1 encendido.
    uint8_t reset;        // Reinicio del sistema: 0 normal, 1 reset.
} datos_sistema_t;


// Variable donde se guardan los datos recibidos desde el Maestro.
datos_sistema_t datos_recibidos; 

// Ángulo del servo cuando la puerta está cerrada.     //ANGULOS A AJUSTAR DE ACUERDO AL SOFTWARE
int angulo_puerta_cerrada = 0;

// Ángulo del servo cuando la puerta está abierta.
int angulo_puerta_abierta = 90;

// Variable para guardar el ángulo actual del servo.
int angulo_actual_servo = 0;


// Variable para guardar el brillo recibido. Está en porcentaje, de 0 a 100.
uint8_t brillo_recibido = 0;

// Variable donde se convierte el brillo a valor PWM. Como la resolución es de 8 bits, el valor va de 0 a 255.
int duty_leds = 0;


// Configuracion de el ESP32 como esclavo I2C.
void configurar_i2c_esclavo(void) {

    // Estructura de configuración del I2C.
    i2c_config_t conf = {

        // Se configura como esclavo, porque este ESP32 recibe datos.
        .mode = I2C_MODE_SLAVE,

        // Pin SDA del bus I2C.
        .sda_io_num = I2C_SDA,

        // Pin SCL del bus I2C.
        .scl_io_num = I2C_SCL,

        // Se activan resistencias pull-up internas.
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,

        // Se usa dirección de 7 bits, no de 10 bits.
        .slave.addr_10bit_en = 0,

        // Dirección del esclavo. El Maestro debe enviar datos a 0x08.
        .slave.slave_addr = I2C_ADDR
    };

    // Se aplican los parámetros al puerto I2C.
    i2c_param_config(I2C_SLAVE_PORT, &conf);

    // Se instala el driver I2C como esclavo.Los valores 1024 son buffers de recepción y transmisión.
    i2c_driver_install(I2C_SLAVE_PORT, conf.mode, 1024, 1024, 0);
}


//Configuracion de el PWM para LEDs y servo.
void configurar_pwm(void) {

    // Configuración del temporizador para los LEDs.
    ledc_timer_config_t timer_leds = {

        // Modo de velocidad baja.Es suficiente para LEDs y compatible con ESP32.
        .speed_mode = LEDC_LOW_SPEED_MODE,

        // Se usa el timer 0 para los LEDs.
        .timer_num = LEDC_TIMER_0,

        // Resolución de 8 bits. Permite duty de 0 a 255.
        .duty_resolution = PWM_RESOLUTION,

        // Frecuencia de 5000 Hz.
        .freq_hz = PWM_FREQ,

        // Reloj automático.
        .clk_cfg = LEDC_AUTO_CLK
    };

    // Se configura el temporizador para los LEDs.
    ledc_timer_config(&timer_leds);


    // Configuración del canal PWM para el LED 1.
    ledc_channel_config_t led1 = {
        .gpio_num = PIN_LED_VENTANA_1,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CANAL_LED_1,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };

    // Para el LED 2 se copia la configuración del LED 1. Solo se cambia el pin fisico y el canal PWM.
    ledc_channel_config_t led2 = led1;
    led2.gpio_num = PIN_LED_VENTANA_2;
    led2.channel = CANAL_LED_2;


    // Para el LED 3 también se copia la configuración del LED 1.Solo se cambia el pin fisico y el canal PWM.
    ledc_channel_config_t led3 = led1;
    led3.gpio_num = PIN_LED_VENTANA_3;
    led3.channel = CANAL_LED_3;


    // Se activan/configuran los tres canales PWM de los LEDs.
    ledc_channel_config(&led1);
    ledc_channel_config(&led2);
    ledc_channel_config(&led3);


    // Configuración del temporizador para el servomotor.
    ledc_timer_config_t timer_servo = {

        // Modo de baja velocidad.
        .speed_mode = LEDC_LOW_SPEED_MODE,

        // Se usa otro timer diferente al de los LEDs.
        .timer_num = LEDC_TIMER_1,

        // Resolución de 16 bits.Esto permite mayor precisión para controlar el servo.
        .duty_resolution = LEDC_TIMER_16_BIT,

        // Frecuencia típica para servomotores: 50 Hz.
        .freq_hz = 50,

        // Reloj automático.
        .clk_cfg = LEDC_AUTO_CLK
    };

    // Se configura el temporizador del servo.
    ledc_timer_config(&timer_servo);


    // Configuración del canal PWM para el servo.
    ledc_channel_config_t servo = {
        .gpio_num = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CANAL_SERVO,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0
    };

    // Se activa/configura el canal PWM del servo.
    ledc_channel_config(&servo);
}


// Configuracion de el LED de la puerta como salida digital.
void configurar_gpio(void) {

    // Se configura el pin del LED de puerta como salida.
    gpio_set_direction(PIN_LED_PUERTA, GPIO_MODE_OUTPUT);

    // Se inicia apagado.
    gpio_set_level(PIN_LED_PUERTA, 0);
}


// Conversion de un ángulo del servo a un valor duty PWM.
uint32_t angulo_a_duty_servo(int angulo) {

    // Pulso mínimo típico para servo: 500 microsegundos.
    int pulso_min_us = 500;

    // Pulso máximo típico para servo: 2400 microsegundos.
    int pulso_max_us = 2400;

    // Como el servo trabaja a 50 Hz: periodo = 1 / 50 = 0.02 s = 20,000 microsegundos.
    int periodo_us = 20000;

    // Se calcula el ancho de pulso correspondiente al ángulo.
    // Ángulo 0 → pulso mínimo.
    // Ángulo 180 → pulso máximo.
    int pulso = pulso_min_us + ((pulso_max_us - pulso_min_us) * angulo) / 180;

    // Se convierte el pulso a duty de 16 bits.65535 es el máximo valor para resolución de 16 bits.
    uint32_t duty = (pulso * 65535) / periodo_us;

    // Regresa el duty calculado.
    return duty;
}


// Ajuste de el servo al ángulo indicado.
void mover_servo(int angulo) {

    // Convierte el ángulo a valor duty PWM.
    uint32_t duty = angulo_a_duty_servo(angulo);

    // Establece el duty en el canal del servo.
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO, duty);

    // Aplica el cambio al canal del servo.
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_SERVO);

    // Guarda el ángulo actual.
    angulo_actual_servo = angulo;
}


// Control de el brillo de los LEDs de ventana.
void controlar_leds_pwm(uint8_t brillo) {

    // Convierte el porcentaje 0-100 a valor PWM 0-255.
    duty_leds = (brillo * 255) / 100;

    // Se aplica el mismo duty a cada LED.
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_1);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_2);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3, duty_leds);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CANAL_LED_3);
}


// Esta función prende o apaga el LED superior de la puerta.
void controlar_luz_puerta(uint8_t estado) {

    // Si estado = 1, prende el LED.
    // Si estado = 0, apaga el LED.
    gpio_set_level(PIN_LED_PUERTA, estado);
}


// Interpretacion de los datos recibidos del Maestro.
void ejecutar_datos_recibidos(void) {

    // Primero revisa si el Maestro mandó reset.
    if (datos_recibidos.reset == SISTEMA_RESET) {

        // Si hay reset, la puerta se cierra.
        mover_servo(angulo_puerta_cerrada);

        // Se apagan los LEDs de ventana.
        controlar_leds_pwm(0);

        // Se apaga la luz de puerta.
        controlar_luz_puerta(LUZ_APAGADA);

        // Se sale de la función para no ejecutar más acciones.
        return;
    }

    // Si no hay reset, revisa el estado deseado de la puerta.
    if (datos_recibidos.puerta == PUERTA_ABIERTA) {

        // Si el dato indica puerta abierta, mueve el servo a 90 grados.
        mover_servo(angulo_puerta_abierta);

    } else {

        // Si no, mueve el servo a 0 grados.
        mover_servo(angulo_puerta_cerrada);
    }

    // Ajusta el brillo de los LEDs según el porcentaje recibido.
    controlar_leds_pwm(datos_recibidos.brillo_leds);

    // Enciende o apaga el LED de puerta según el dato recibido.
    controlar_luz_puerta(datos_recibidos.luz_puerta);
}


// Leer los datos que llegan por I2C desde el Maestro.
void recibir_datos_i2c(void) {

    // Leer el buffer I2C y guarda la información en datos_recibidos.
    int bytes = i2c_slave_read_buffer(
        I2C_SLAVE_PORT,
        (uint8_t *)&datos_recibidos,
        sizeof(datos_recibidos),
        pdMS_TO_TICKS(100)
    );

    // Verificar que se hayan recibido exactamente los bytes esperados.
    if (bytes == sizeof(datos_recibidos)) {

        // Si el paquete llegó completo, ejecuta las acciones.
        ejecutar_datos_recibidos();
    }
}


// Función principal del programa.
void app_main(void) {

    // Configura el LED de la puerta como salida.
    configurar_gpio();

    // Configura PWM para LEDs y servo.
    configurar_pwm();

    // Configura el ESP32 como esclavo I2C.
    configurar_i2c_esclavo();

    // Estado inicial del sistema: puerta cerrada.
    mover_servo(angulo_puerta_cerrada);

    // LEDs de ventana apagados.
    controlar_leds_pwm(0);

    // LED de puerta apagado.
    controlar_luz_puerta(LUZ_APAGADA);

    // Ciclo infinito del programa.
    while (1) {

        // Revisa constantemente si llegaron datos del Maestro por I2C.
        recibir_datos_i2c();

        // Pequeña pausa para no saturar el procesador.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}