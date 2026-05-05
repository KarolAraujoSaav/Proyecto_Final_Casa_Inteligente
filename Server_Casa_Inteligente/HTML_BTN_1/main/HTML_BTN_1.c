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

// === DEFINICIÓN DEL PIN === 

#define LED GPIO_NUM_5   

// === CONFIGURACIÓN WIFI ===

#define SSID "CASA_INTELIGENTE"     // Nombre de la red WiFi que creará el ESP32.
#define PASS "12345678"      // Contraseña (mínimo 8 caracteres).

static const char *TAG = "LED_WEB";

// === ESTADO DEL LED === 

static int led_state = 0;  // Guarda si el LED está encendido o apagado. 0 = OFF, 1 = ON.

// === HTML (PÁGINA WEB) ===

static const char html[] =
"<!DOCTYPE html><html>"  // Inicio de documento HTML.
"<head>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>" // Este comando lo vuelve adaptable para el celular.
"</head>"

"<body style='text-align:center;font-family:sans-serif;'>" // Estilo centrado.
"<h2>Control LED</h2>"  // Título.

// Botón que ejecuta la función toggle() en JavaScript
"<button onclick='toggle()' style='font-size:20px;padding:10px;'>TOGGLE</button>"

// Texto donde se mostrará el estado del LED.
"<p id='state'>Estado: OFF</p>"

"<script>"

// Función JS que se ejecuta al presionar el botón.
"function toggle(){"
" fetch('/toggle')"  // Hace petición al ESP32 en la ruta /toggle.
" .then(r=>r.text())"  // Recibe respuesta como texto.
" .then(t=>{"          
"   document.getElementById('state').innerText = t;" 
// Actualiza el texto en pantalla con la respuesta del ESP32.
" });"
"}"

"</script>"
"</body></html>";

// === CONFIGURACIÓN DEL GPIO ===

void gpio_init_led() {
    gpio_set_direction(LED, GPIO_MODE_OUTPUT); 
    gpio_set_level(LED, 0);                     // Inicialmente apagado.
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

    ESP_LOGI(TAG, "WiFi listo. Conectate a %s", SSID);  // Mensaje en consola.
}

// === HANDLER DE LA PÁGINA PRINCIPAL "/" === 

esp_err_t root_get(httpd_req_t *req) {
    httpd_resp_send(req, html, strlen(html));  // Envía el HTML al navegador.
    return ESP_OK;
}

// === HANDLER DE "/toggle" === 

esp_err_t toggle_get(httpd_req_t *req) {

    led_state = !led_state;  
 
    gpio_set_level(LED, led_state);  

    if (led_state)
        httpd_resp_sendstr(req, "Estado: ON");   // Respuesta si está encendido.
    else
        httpd_resp_sendstr(req, "Estado: OFF");  // Respuesta si está apagado.

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

    // Ruta "/toggle" va a cambiar el LED.
    
    httpd_uri_t toggle = {
        .uri = "/toggle",
        .method = HTTP_GET,
        .handler = toggle_get
    };

    // Registrar las rutas en el servidor.
    
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &toggle);
}

// === FUNCIÓN PRINCIPAL === 

void app_main(void) {

    nvs_flash_init();                 // Inicializa memoria.
    esp_netif_init();                 // Inicializa red.
    esp_event_loop_create_default();  // Sistema de eventos.

    gpio_init_led();   // Configura el LED.
    wifi_init();       // Inicia WiFi.
    start_server();    // Inicia servidor web.
}
