#include <stdio.h>      
#include <string.h>
#include <stdlib.h>     // Para atoi()

#include "freertos/FreeRTOS.h"   
#include "freertos/task.h"

#include "esp_system.h"          
#include "esp_log.h"             
#include "nvs_flash.h"           // Memoria no volátil.
#include "esp_event.h"           // Eventos del sistema.
#include "esp_netif.h"           // Interfaz de red.
#include "esp_wifi.h"            // Control WiFi.
#include "esp_http_server.h"     // Servidor web.
#include "driver/gpio.h" 
#include "driver/ledc.h"         // PWM.        

// === DEFINICIÓN DE PINES === 

// LEDS.

#define LED1 GPIO_NUM_5   
#define LED2 GPIO_NUM_18

// BOTONES.

#define BTN_SEL GPIO_NUM_25   // Cambia LED seleccionado.
#define BTN_50  GPIO_NUM_13
#define BTN_100 GPIO_NUM_14
#define BTN_OFF GPIO_NUM_27

// === CONFIGURACIÓN PWM ===

#define PWM_CHANNEL_1 LEDC_CHANNEL_0
#define PWM_CHANNEL_2 LEDC_CHANNEL_1
#define PWM_TIMER     LEDC_TIMER_0

// === CONFIGURACIÓN WIFI ===

#define SSID "CASA_INTELIGENTE"     // Nombre de la red WiFi que creará el ESP32.
#define PASS "12345678"             // Contraseña (mínimo 8 caracteres).

// === VARIABLES === 

// Estado de ambos LEDs.
static int level[2] = {0,0};

// LED seleccionado por botones.
static int selected = 0;

// === HTML (PÁGINA WEB) ===

static const char html[] =
"<!DOCTYPE html><html>"
"<head>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"</head>"

"<body style='text-align:center;font-family:sans-serif;'>"
"<h2>Control 2 LEDs</h2>"

// LED 1.
"<h3>LED 1</h3>"
"<button onclick='set(1,0)'>OFF</button>"
"<button onclick='set(1,50)'>50%</button>"
"<button onclick='set(1,100)'>100%</button>"
"<p id='l1'>Nivel: 0%</p>"

// LED 2.
"<h3>LED 2</h3>"
"<button onclick='set(2,0)'>OFF</button>"
"<button onclick='set(2,50)'>50%</button>"
"<button onclick='set(2,100)'>100%</button>"
"<p id='l2'>Nivel: 0%</p>"

"<script>"

// Control individual por LED.
"function set(n,l){"
" fetch(`/set?led=${n}&level=${l}`)"
"}"

// Actualiza ambos LEDs.
"function update(){"
" fetch('/state')"
" .then(r=>r.json())"
" .then(s=>{"
"  document.getElementById('l1').innerText='Nivel: '+s.led1+'%';"
"  document.getElementById('l2').innerText='Nivel: '+s.led2+'%';"
"});"
"}"

// Ejecuta la función update cada 1.2 segundos.
"setInterval(update,1200);"

// Ejecuta update al cargar la página.
"update();"

"</script>"
"</body></html>";


// === CONFIGURACIÓN PWM ===

void pwm_init() {

    // Configuración del temporizador PWM.
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT, // Resolución de 0 a 255.
        .freq_hz = 5000                      // Frecuencia de 5 kHz.
    };
    ledc_timer_config(&timer);
 
    // Canal LED1.
    ledc_channel_config_t ch1 = {
        .gpio_num = LED1,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = PWM_CHANNEL_1,
        .timer_sel = PWM_TIMER,
        .duty = 0
    };

    // Canal LED2.
    ledc_channel_config_t ch2 = {
        .gpio_num = LED2,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = PWM_CHANNEL_2,
        .timer_sel = PWM_TIMER,
        .duty = 0
    };

    ledc_channel_config(&ch1);
    ledc_channel_config(&ch2);
}

// === APLICAR PWM A CADA LED ===
void apply_led(int i) {

    int duty = 0;

    // Conversión de porcentaje a valor PWM.
    if (level[i] == 0) duty = 0;
    else if (level[i] == 50) duty = 127;
    else if (level[i] == 100) duty = 255;

    // Selección de canal según LED.
    int channel = (i == 0) ? PWM_CHANNEL_1 : PWM_CHANNEL_2;

    // Aplicar señal PWM.
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel);
}

// === CONFIGURACIÓN DE BOTONES ===

void buttons_init() {

    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<BTN_SEL)|(1ULL<<BTN_50)|(1ULL<<BTN_100)|(1ULL<<BTN_OFF),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1  // Activa resistencia pull-up interna.
    };

    gpio_config(&io);
}

// === BOTONES FÍSICOS ===

void button_task(void *arg) {

    while (1) {

        // Cambiar LED seleccionado
        if (!gpio_get_level(BTN_SEL)) {
            selected = !selected;
            vTaskDelay(pdMS_TO_TICKS(300));  // Delay anti-rebote.
        }

        // 50%
        if (!gpio_get_level(BTN_50)) {
            level[selected] = 50;
            apply_led(selected);
            vTaskDelay(pdMS_TO_TICKS(300)); 
        }

        // 100%
        if (!gpio_get_level(BTN_100)) {
            level[selected] = 100;
            apply_led(selected);
            vTaskDelay(pdMS_TO_TICKS(300));  
        }

        // OFF
        if (!gpio_get_level(BTN_OFF)) {
            level[selected] = 0;
            apply_led(selected);
            vTaskDelay(pdMS_TO_TICKS(300));  
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// === INICIALIZACIÓN DEL WIFI EN MODO ACCESS POINT ===

void wifi_init() {

    esp_netif_create_default_wifi_ap();  // Crea interfaz de red en modo AP.

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Configuración por defecto.
    esp_wifi_init(&cfg);  // Inicializa el WiFi.

    wifi_config_t ap = {
        .ap = {
            .ssid = SSID,                 
            .ssid_len = strlen(SSID),     
            .password = PASS,             
            .max_connection = 4,          
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    esp_wifi_set_mode(WIFI_MODE_AP);           
    esp_wifi_set_config(WIFI_IF_AP, &ap);      
    esp_wifi_start();                          

    ESP_LOGI("WIFI", "WiFi listo. Conectate a %s", SSID);
}

// === HANDLER DE LA PÁGINA PRINCIPAL "/" === 

esp_err_t root_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}


// === HANDLER "/set" ===

esp_err_t set_get(httpd_req_t *req) {

    char query[32];
    char val[8];
    int led = 0;
    int l = 0;

    // Obtiene parámetros de la URL.
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {

        if (httpd_query_key_value(query, "led", val, sizeof(val)) == ESP_OK)
            led = atoi(val);

        if (httpd_query_key_value(query, "level", val, sizeof(val)) == ESP_OK)
            l = atoi(val);
    }

    // Validación.
    if (led >= 1 && led <= 2) {
        level[led-1] = l;
        apply_led(led-1);
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}


// === HANDLER "/state" ===

esp_err_t state_get(httpd_req_t *req) {

    char resp[64];

    // Devuelve JSON para que el HTML lo entienda
    sprintf(resp, "{\"led1\":%d,\"led2\":%d}", level[0], level[1]);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    return ESP_OK;
}


// === INICIO DEL SERVIDOR WEB === 

void start_server() {

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&server, &config);

    httpd_uri_t root = { .uri="/", .method=HTTP_GET, .handler=root_get };
    httpd_uri_t set  = { .uri="/set", .method=HTTP_GET, .handler=set_get };
    httpd_uri_t state= { .uri="/state", .method=HTTP_GET, .handler=state_get };

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &state);
}

// === FUNCIÓN PRINCIPAL === 

void app_main(void) {

    nvs_flash_init();                 
    esp_netif_init();                 
    esp_event_loop_create_default();  

    pwm_init();        
    buttons_init();   
    wifi_init();       
    start_server();    
    
    xTaskCreate(button_task, "btn_task", 2048, NULL, 5, NULL);

}
