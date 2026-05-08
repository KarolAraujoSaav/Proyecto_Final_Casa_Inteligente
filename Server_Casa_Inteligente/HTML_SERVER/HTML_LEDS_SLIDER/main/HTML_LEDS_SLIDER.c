#include <stdio.h>      
#include <string.h>
#include <stdlib.h>     // Para strcmp() y atoi().

#include "freertos/FreeRTOS.h"   
#include "freertos/task.h"

#include "esp_system.h"          
#include "esp_log.h"             
#include "nvs_flash.h"           // Memoria no volátil.
#include "esp_event.h"           // Eventos del sistema.
#include "esp_netif.h"           // Interfaz de red.
#include "esp_wifi.h"            // Control WiFi.
#include "esp_http_server.h"     // Servidor web.

#include "driver/gpio.h"         // Manejo de GPIO.
#include "driver/uart.h"         // Comunicación UART. Va a transmitir del ESP 3 al ESP 1.


// === CONFIGURACIÓN UART ===

// UART que se utilizará.
#define UART_PORT UART_NUM_1

// Pines UART.
#define UART_TX GPIO_NUM_17
#define UART_RX GPIO_NUM_16

// Velocidad de transmisión.
#define UART_BAUD_RATE 9600


// === CONFIGURACIÓN WIFI ===

// Nombre de la red WiFi creada por el ESP32.
#define SSID "CASA_INTELIGENTE"

// Contraseña de la red.
#define PASS "87654321"


// === ENUMERACIONES ===

// Estados posibles de la puerta.
typedef enum {

    PUERTA_CERRADA = 0,
    PUERTA_ABIERTA = 1

} estado_puerta_t;


// Estados posibles de la luz.
typedef enum {

    LUZ_APAGADA = 0,
    LUZ_ENCENDIDA = 1

} estado_luz_t;


// === VARIABLES ===

// Estado actual de la puerta.
static estado_puerta_t puerta_estado = PUERTA_CERRADA;

// Estado actual de la luz.
static estado_luz_t luz_estado = LUZ_APAGADA;

// Nivel de brillo actual.
static int brillo_luz = 0;


// === HTML (PÁGINA WEB) ===

static const char html[] =

"<!DOCTYPE html><html>"

"<head>"

"<meta name='viewport' content='width=device-width, initial-scale=1'>"

"</head>"

"<body style='text-align:center;font-family:sans-serif;'>"


// =========================
// CONTROL DE PUERTA
// =========================

"<h2>CONTROL DE PUERTA</h2>"

// Botón abrir puerta.
"<button onclick=\"setDoor('open')\">ABRIR PUERTA</button>"

"<br><br>"

// Botón cerrar puerta.
"<button onclick=\"setDoor('close')\">CERRAR PUERTA</button>"

"<br><br>"

// Texto estado puerta.
"<p id='estado_puerta'>Estado puerta: CERRADA</p>"

"<hr>"


// =========================
// CONTROL DE LUCES
// =========================

"<h2>CONTROL DE LUCES</h2>"

// Botón encender luz.
"<button onclick=\"setLight('on')\">ENCENDER LUZ</button>"

"<br><br>"

// Botón apagar luz.
"<button onclick=\"setLight('off')\">APAGAR LUZ</button>"

"<br><br>"


// =========================
// SLIDER DE BRILLO
// =========================

// Barra deslizante de intensidad.
"<input type='range' min='0' max='100' value='0' id='slider' "
"oninput='setBrightness(this.value)'>"

"<br><br>"

// Texto porcentaje brillo.
"<p id='brillo_txt'>Brillo: 0%</p>"

// Texto estado actual luz.
"<p id='estado_luz'>Estado luz: APAGADA</p>"


"<script>"


// =========================
// FUNCIÓN PUERTA
// =========================

// Función para enviar comando de puerta.
"function setDoor(action){"

" fetch(`/set?door=${action}`)"

"}"


// =========================
// FUNCIÓN LUCES
// =========================

// Función para enviar comando de luces.
"function setLight(action){"

" fetch(`/set?light=${action}`)"

"}"


// =========================
// FUNCIÓN BRILLO
// =========================

// Función para enviar porcentaje de brillo.
"function setBrightness(value){"

" fetch(`/set?brightness=${value}`)"

"}"


// =========================
// ACTUALIZAR ESTADOS
// =========================

// Función para actualizar estados.
"function update(){"

" fetch('/state')"

" .then(r=>r.json())"

" .then(s=>{"

// Actualizar texto puerta.
"  document.getElementById('estado_puerta').innerText='Estado puerta: '+s.puerta;"

// Actualizar texto luz.
"  document.getElementById('estado_luz').innerText='Estado luz: '+s.luz;"

// Actualizar porcentaje brillo.
"  document.getElementById('brillo_txt').innerText='Brillo: '+s.brillo+'%';"

// Actualizar posición slider.
"  document.getElementById('slider').value=s.brillo;"

" });"

"}"

// Actualizar cada segundo.
"setInterval(update,1000);"

// Actualizar al abrir página.
"update();"

"</script>"

"</body></html>";


// === CONFIGURACIÓN UART ===

void uart_init() {

    // Configuración principal UART.
    uart_config_t uart_config = {

        // Velocidad de transmisión.
        .baud_rate = UART_BAUD_RATE,

        // 8 bits de datos.
        .data_bits = UART_DATA_8_BITS,

        // Sin paridad.
        .parity = UART_PARITY_DISABLE,

        // 1 bit de stop.
        .stop_bits = UART_STOP_BITS_1,

        // Sin control de flujo.
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    // Aplicar configuración UART.
    uart_param_config(UART_PORT, &uart_config);

    // Configurar pines UART.
    uart_set_pin(
        UART_PORT,
        UART_TX,
        UART_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    // Instalar driver UART.
    uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0);
}


// === ENVÍO DE COMANDOS UART ===

void enviar_uart(char *mensaje) {

    // Enviar texto al ESP32 maestro.
    uart_write_bytes(UART_PORT, mensaje, strlen(mensaje));
}


// === INICIALIZACIÓN WIFI ===

void wifi_init() {

    // Crear interfaz WiFi modo AP.
    esp_netif_create_default_wifi_ap();

    // Configuración WiFi por defecto.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    // Inicializar WiFi.
    esp_wifi_init(&cfg);

    // Configuración del Access Point.
    wifi_config_t ap = {

        .ap = {

            .ssid = SSID,
            .ssid_len = strlen(SSID),

            .password = PASS,

            .max_connection = 4,

            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    // Configurar modo AP.
    esp_wifi_set_mode(WIFI_MODE_AP);

    // Aplicar configuración.
    esp_wifi_set_config(WIFI_IF_AP, &ap);

    // Iniciar WiFi.
    esp_wifi_start();

    ESP_LOGI("WIFI", "WiFi listo. Conectate a %s", SSID);
}


// === HANDLER PÁGINA PRINCIPAL "/" ===

esp_err_t root_get(httpd_req_t *req) {

    // Tipo HTML.
    httpd_resp_set_type(req, "text/html");

    // Enviar página.
    httpd_resp_send(req, html, strlen(html));

    return ESP_OK;
}


// === HANDLER "/set" ===

esp_err_t set_get(httpd_req_t *req) {

    char query[64];

    char accion_puerta[16];
    char accion_luz[16];

    char brillo_str[8];

    // Obtener parámetros URL.
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {

        // =========================
        // CONTROL DE PUERTA
        // =========================

        if (httpd_query_key_value(query, "door", accion_puerta, sizeof(accion_puerta)) == ESP_OK) {

            // Abrir puerta.
            if (strcmp(accion_puerta, "open") == 0) {

                // Actualizar estado.
                puerta_estado = PUERTA_ABIERTA;

                // Enviar comando UART.
                enviar_uart("PUERTA_ABIERTA\n");
            }

            // Cerrar puerta.
            else if (strcmp(accion_puerta, "close") == 0) {

                // Actualizar estado.
                puerta_estado = PUERTA_CERRADA;

                // Enviar comando UART.
                enviar_uart("PUERTA_CERRADA\n");
            }
        }


        // =========================
        // CONTROL DE LUCES
        // =========================

        if (httpd_query_key_value(query, "light", accion_luz, sizeof(accion_luz)) == ESP_OK) {

            // Encender luz.
            if (strcmp(accion_luz, "on") == 0) {

                // Actualizar estado.
                luz_estado = LUZ_ENCENDIDA;

                // Enviar comando UART.
                enviar_uart("LUZ_ENCENDIDA\n");
            }

            // Apagar luz.
            else if (strcmp(accion_luz, "off") == 0) {

                // Actualizar estado.
                luz_estado = LUZ_APAGADA;

                // Enviar comando UART.
                enviar_uart("LUZ_APAGADA\n");
            }
        }


        // =========================
        // CONTROL DE BRILLO
        // =========================

        if (httpd_query_key_value(query, "brightness", brillo_str, sizeof(brillo_str)) == ESP_OK) {

            // Convertir texto a entero.
            brillo_luz = atoi(brillo_str);

            // Limitar rango.
            if (brillo_luz < 0) brillo_luz = 0;
            if (brillo_luz > 100) brillo_luz = 100;

            // Buffer comando UART.
            char comando[32];

            // Crear comando tipo:
            // BRILLO:70
            sprintf(comando, "BRILLO:%d\n", brillo_luz);

            // Enviar comando UART.
            enviar_uart(comando);
        }
    }

    // Respuesta servidor.
    httpd_resp_sendstr(req, "OK");

    return ESP_OK;
}


// === HANDLER "/state" ===

esp_err_t state_get(httpd_req_t *req) {

    char resp[128];

    // Variables auxiliares texto.
    char *puerta_txt;
    char *luz_txt;

    // Convertir estado puerta a texto.
    if (puerta_estado == PUERTA_ABIERTA) {

        puerta_txt = "ABIERTA";

    } else {

        puerta_txt = "CERRADA";
    }

    // Convertir estado luz a texto.
    if (luz_estado == LUZ_ENCENDIDA) {

        luz_txt = "ENCENDIDA";

    } else {

        luz_txt = "APAGADA";
    }

    // Crear JSON.
    sprintf(
        resp,
        "{\"puerta\":\"%s\",\"luz\":\"%s\",\"brillo\":%d}",
        puerta_txt,
        luz_txt,
        brillo_luz
    );

    // Tipo JSON.
    httpd_resp_set_type(req, "application/json");

    // Enviar respuesta.
    httpd_resp_send(req, resp, strlen(resp));

    return ESP_OK;
}


// === INICIO DEL SERVIDOR WEB ===

void start_server() {

    httpd_handle_t server = NULL;

    // Configuración servidor.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Iniciar servidor.
    httpd_start(&server, &config);

    // Página principal.
    httpd_uri_t root = {

        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get
    };

    // Ruta control.
    httpd_uri_t set = {

        .uri = "/set",
        .method = HTTP_GET,
        .handler = set_get
    };

    // Ruta estados.
    httpd_uri_t state = {

        .uri = "/state",
        .method = HTTP_GET,
        .handler = state_get
    };

    // Registrar handlers.
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &state);
}


// === FUNCIÓN PRINCIPAL ===

void app_main(void) {

    // Inicializar memoria NVS.
    nvs_flash_init();

    // Inicializar red.
    esp_netif_init();

    // Inicializar eventos.
    esp_event_loop_create_default();

    // Inicializar UART.
    uart_init();

    // Inicializar WiFi.
    wifi_init();

    // Inicializar servidor web.
    start_server();
}
