#include <stdio.h>      
#include <string.h>

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

// LED.

#define LED GPIO_NUM_5   

// BOTONES.

#define BTN_OFF GPIO_NUM_25
#define BTN_50  GPIO_NUM_13
#define BTN_100 GPIO_NUM_14

// === CONFIGURACIÓN PWM ===

#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_TIMER   LEDC_TIMER_0

// === CONFIGURACIÓN WIFI ===

#define SSID "CASA_INTELIGENTE"     // Nombre de la red WiFi que creará el ESP32.
#define PASS "12345678"      // Contraseña (mínimo 8 caracteres).

// === VARIABLE GLOBAL === 

static int level = 0;  // Guarda el nivel actual del LED (0, 50, 100).


// === HTML (PÁGINA WEB) ===

static const char html[] =
"<!DOCTYPE html><html>"  // Inicio de documento HTML.
"<head>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>" // Este comando lo vuelve adaptable para el celular.
"</head>"

"<body style='text-align:center;font-family:sans-serif;'>" // Estilo centrado.
"<h2>Control LED</h2>"  // Título.

// Botones para controlar intensidad desde la web.
"<button onclick='setLevel(0)'>OFF</button>"
"<button onclick='setLevel(50)'>50%</button>"
"<button onclick='setLevel(100)'>100%</button>"

// Texto donde se mostrará el estado del LED.
"<p id='state'>Nivel: 0%</p>"

"<script>"

// Función que envía el nivel seleccionado al ESP32.
"function setLevel(l){"
" fetch(`/set?level=${l}`)"  // Petición al endpoint /set.
" .then(r=>r.text())"        // Recibe respuesta.
" .then(t=>document.getElementById('state').innerText=t);" // Actualiza texto.
"}"

// Función que consulta el estado actual del LED.
"function update(){"
" fetch('/state')"           // Petición al endpoint /state.
" .then(r=>r.text())"
" .then(t=>document.getElementById('state').innerText=t);" // Actualiza texto.
"}"

// Ejecuta la función update cada 1.2 segundos.
"setInterval(update,1200);"

// Ejecuta update al cargar la página.
"update();"

"</script>"
"</body></html>";

// === CONFIGURACIÓN DEL GPIO ===

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

    // Configuración del canal PWM.
    ledc_channel_config_t channel = {
        .gpio_num = LED,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .duty = 0   // Inicia apagado.
    };
    ledc_channel_config(&channel);
}

// === CONTROL DEL LED ===

void set_level(int l) {

    level = l;  // Guarda el estado actual.

    int duty = 0;

    // Conversión de porcentaje a valor PWM.
    
    if (l == 0) duty = 0;
    else if (l == 50) duty = 127;
    else if (l == 100) duty = 255;

    // Aplicar señal PWM al LED.
    
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
}

// === CONFIGURACIÓN DE BOTONES ===

void buttons_init() {

    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<BTN_OFF) | (1ULL<<BTN_50) | (1ULL<<BTN_100),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1  // Activa resistencia pull-up interna.
    };

    gpio_config(&io);
}

// === BOTONES FÍSICOS ===

void button_task(void *arg) {

    while (1) {

        // Si se presiona botón OFF.
        if (!gpio_get_level(BTN_OFF)) {
            set_level(0);
            vTaskDelay(300 / portTICK_PERIOD_MS); // Antirrebote.
        }

        // Si se presiona botón 50%.
        if (!gpio_get_level(BTN_50)) {
            set_level(50);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }

        // Si se presiona botón 100%.
        if (!gpio_get_level(BTN_100)) {
            set_level(100);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS); // Pequeño delay para estabilidad.
    }
}


// === INICIALIZACIÓN DEL WIFI EN MODO ACCESS POINT ===

void wifi_init() {

    esp_netif_create_default_wifi_ap();  // Crea interfaz de red en modo AP.

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Configuración por defecto.
    esp_wifi_init(&cfg);  // Inicializa el WiFi.

    wifi_config_t ap = {
        .ap = {
            .ssid = SSID,                 // Nombre de la red.
            .ssid_len = strlen(SSID),     // Longitud del SSID.
            .password = PASS,             // Contraseña.
            .max_connection = 4,          // Máximo de dispositivos conectados.
            .authmode = WIFI_AUTH_WPA_WPA2_PSK  // Seguridad WPA/WPA2.
        }
    };

    esp_wifi_set_mode(WIFI_MODE_AP);           // Modo Access Point (como router).
    esp_wifi_set_config(WIFI_IF_AP, &ap);      // Aplica configuración.
    esp_wifi_start();                          // Inicia WiFi.

    ESP_LOGI("WIFI", "WiFi listo. Conectate a %s", SSID);  // Mensaje en consola.
}

// === HANDLER DE LA PÁGINA PRINCIPAL "/" === 

esp_err_t root_get(httpd_req_t *req) {
    httpd_resp_send(req, html, strlen(html));  // Envía el HTML al navegador.
    return ESP_OK;
}

// === HANDLER "/set" ===

esp_err_t set_get(httpd_req_t *req) {

    char query[32];
    char val[8];
    int l = 0;

    // Obtiene parámetros de la URL.
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {

        if (httpd_query_key_value(query, "level", val, sizeof(val)) == ESP_OK) {
            l = atoi(val);
        }
    }

    set_level(l);  // Aplica el nivel seleccionado.

    char resp[20];
    sprintf(resp, "Nivel: %d%%", l);

    httpd_resp_sendstr(req, resp);  // Respuesta al navegador.

    return ESP_OK;
}

// === HANDLER "/state" ===

esp_err_t state_get(httpd_req_t *req) {

    char resp[20];
    sprintf(resp, "Nivel: %d%%", level);

    httpd_resp_sendstr(req, resp);  // Devuelve estado actual.

    return ESP_OK;
}

// === INICIO DEL SERVIDOR WEB === 

void start_server() {

    httpd_handle_t server = NULL;  // Handler del servidor.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();  // Configuración por defecto.

    httpd_start(&server, &config);  // Inicia el servidor.

    // Ruta "/" dirige a página principal.
    httpd_uri_t root = {
        .uri = "/",               // Dirección.
        .method = HTTP_GET,       // Método GET.
        .handler = root_get       // Función que responde.
    };

    // Ruta "/set" va a cambiar nivel.
    httpd_uri_t set = {
        .uri = "/set",
        .method = HTTP_GET,
        .handler = set_get
    };
    
    // Ruta "/state" va a obtener el estado.
    httpd_uri_t state = {
        .uri = "/state",
        .method = HTTP_GET,
        .handler = state_get
    };

    // Registrar las rutas en el servidor.
    
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &state);
}

// === FUNCIÓN PRINCIPAL === 

void app_main(void) {

    nvs_flash_init();                 // Inicializa memoria.
    esp_netif_init();                 // Inicializa red.
    esp_event_loop_create_default();  // Sistema de eventos.

    pwm_init();        // Configura PWM.
    buttons_init();   // Configura los botones.
    wifi_init();       // Inicia WiFi.
    start_server();    // Inicia servidor web.
    
     xTaskCreate(button_task, "btn_task", 2048, NULL, 5, NULL); // Tarea para botones.

}
