// ======================================================
// ESP32_WIFI (ESP32)
//
// RX ← Recibe estados del maestro.
// TX → Envía comandos al maestro.
//
// Protocolo UART: "puerta,brillo,luz_ventana,luz_puerta,reset\n"
// Baud Rate: 9600
// Red WiFi AP: CASA_INTELIGENTE / 87654321
// ======================================================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

#include "driver/gpio.h"
#include "driver/uart.h"

// ======================================================
// CONFIGURACIÓN UART
// ======================================================

#define UART_PORT      UART_NUM_1
#define UART_TX        GPIO_NUM_17   
#define UART_RX        GPIO_NUM_16    
#define UART_BAUD_RATE 9600
#define UART_BUF_SIZE  256

// ======================================================
// CONFIGURACIÓN WIFI
// ======================================================

#define SSID "CASA_INTELIGENTE"
#define PASS "87654321"

static const char *TAG = "WIFI";

// ======================================================
// ENUMERACIONES
// ======================================================

typedef enum { PUERTA_CERRADA = 0, PUERTA_ABIERTA = 1 } estado_puerta_t;
typedef enum { LUZ_APAGADA   = 0, LUZ_ENCENDIDA  = 1 } estado_luz_t;
typedef enum { SISTEMA_NORMAL = 0, SISTEMA_RESET  = 1 } estado_reset_t;

// ======================================================
// VARIABLES GLOBALES
// ======================================================

static estado_puerta_t puerta_estado     = PUERTA_CERRADA;
static estado_luz_t    luz_vent_estado   = LUZ_APAGADA;
static int             brillo_luz        = 0;
static estado_reset_t  sistema_estado    = SISTEMA_NORMAL;
static estado_luz_t    luz_puerta_estado = LUZ_APAGADA;

// Bandera: el usuario controló la luz ventana manualmente desde la web.
// Mientras esté activa, el estado UART del maestro no sobreescribe luz_vent_estado.
// Se limpia solo cuando el maestro envía un reset.
static bool luz_vent_manual = false;

// ======================================================
// HTML
// ======================================================

static const char html[] =

"<!DOCTYPE html>"
"<html lang='es'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>Casa Inteligente</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box;font-family:Arial,sans-serif;}"
"body{background:linear-gradient(135deg,#0f172a,#1e293b);color:white;padding:20px;text-align:center;}"
"h1{margin-bottom:20px;font-size:2rem;}"
".container{display:flex;flex-direction:column;gap:20px;max-width:500px;margin:auto;}"
".card{background:#1e293b;padding:20px;border-radius:20px;box-shadow:0 4px 15px rgba(0,0,0,0.4);}"
".card h2{margin-bottom:15px;}"
"button{border:none;padding:12px 18px;margin:5px;border-radius:12px;font-size:1rem;font-weight:bold;cursor:pointer;transition:0.3s;}"
".on{background:#22c55e;color:white;}"
".off{background:#ef4444;color:white;}"
".warning{background:#f59e0b;color:white;}"
"button:hover{transform:scale(1.05);}"
".status{margin-top:10px;font-size:1.1rem;font-weight:bold;}"
"input[type=range]{width:100%;margin-top:15px;}"
".value{margin-top:10px;font-size:1.2rem;}"
".door-open{color:#22c55e;}"
".door-close{color:#ef4444;}"
"</style>"
"</head>"
"<body>"
"<h1>CASA INTELIGENTE</h1>"
"<div class='container'>"

// Puerta

"<div class='card'>"
"<h2>Control de la Puerta</h2>"
"<button class='on' onclick=\"setDoor('open')\">ABRIR</button>"
"<button class='off' onclick=\"setDoor('close')\">CERRAR</button>"
"<div class='status' id='estado_puerta'>CERRADA</div>"
"</div>"

// LEDs ventana 

"<div class='card'>"
"<h2>Iluminación en ventanas</h2>"
"<button class='on' onclick=\"setLight('on')\">ENCENDER</button>"
"<button class='off' onclick=\"setLight('off')\">APAGAR</button>"
"<div class='value' id='brillo_txt'>0%</div>"
"<div class='status' id='estado_luz'>APAGADOS</div>"
"</div>"

// Luz puerta 

"<div class='card'>"
"<h2>Luz de la Puerta</h2>"
"<div class='status' id='estado_luz_puerta'>APAGADA</div>"
"</div>"

// Reset

"<div class='card'>"
"<h2>Sistema</h2>"
"<button class='warning' onclick='resetSystem()'>REINICIAR</button>"
"<div class='status' id='estado_reset'>NORMAL</div>"
"</div>"

"</div>"

"<script>"
"function setDoor(a){ fetch('/set?door='+a).then(update); }"
"function setLight(a){ fetch('/set?light='+a).then(update); }"
"function resetSystem(){ fetch('/set?reset=1').then(update); }"

"function update(){"
" fetch('/state').then(r=>r.json()).then(s=>{"
"  document.getElementById('estado_puerta').innerText=s.puerta;"
"  document.getElementById('estado_luz').innerText=s.luz_ventana;"
"  document.getElementById('brillo_txt').innerText=s.brillo+'%';"
"  document.getElementById('estado_luz_puerta').innerText=s.luz_puerta;"
"  document.getElementById('estado_reset').innerText=s.reset;"
"  let p=document.getElementById('estado_puerta');"
"  p.className='status '+(s.puerta=='ABIERTA'?'door-open':'door-close');"
" });"
"}"

// Actualiza cada segundo

"setInterval(update,1000);"
"update();"
"</script>"
"</body></html>";

// ======================================================
// UART
// ======================================================

void uart_init(void) {
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX, UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    ESP_LOGI(TAG, "UART listo: TX=GPIO%d RX=GPIO%d @%d baud", UART_TX, UART_RX, UART_BAUD_RATE);
}

void enviar_uart(void) {
    char trama[40];
    int n = snprintf(trama, sizeof(trama), "%d,%d,%d,%d,%d\n",
                     (int)puerta_estado,
                     brillo_luz,
                     (int)luz_vent_estado,
                     (int)luz_puerta_estado,
                     (int)sistema_estado);
    uart_write_bytes(UART_PORT, trama, n);
}

// Recibe el estado que el maestro reporta periódicamente.
// Solo actualiza luz_vent_estado si el usuario NO la controló manualmente.
// Siempre actualiza: puerta, brillo (para mostrarlo), luz_puerta y reset.
void recibir_estado_uart(void) {
    uint8_t buf[UART_BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(10));
    if (len <= 0) return;

    buf[len] = '\0';
    int p, b, lv, lp, r;

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d", &p, &b, &lv, &lp, &r) == 5) {

        if (p == 0 || p == 1)
            puerta_estado = (estado_puerta_t)p;

        // Brillo: se muestra siempre, pero si el usuario apagó manualmente
        // no se reactiva aunque el potenciómetro mande brillo > 0.
        if (b >= 0 && b <= 100)
            brillo_luz = b;

        // luz_ventana: solo se acepta del maestro si el usuario
        // no tomó control manual desde la web.
        if (!luz_vent_manual) {
            if (lv == 0 || lv == 1)
                luz_vent_estado = (estado_luz_t)lv;
        }

        // luz_puerta: siempre viene del LDR via maestro, nunca manual.
        if (lp == 0 || lp == 1)
            luz_puerta_estado = (estado_luz_t)lp;

        // Reset: libera el control manual y acepta todo del maestro de nuevo.
        if (r == 1) {
            luz_vent_manual  = false;
            luz_vent_estado  = LUZ_APAGADA;
            brillo_luz       = 0;
            puerta_estado    = PUERTA_CERRADA;
            luz_puerta_estado = LUZ_APAGADA;
            sistema_estado   = SISTEMA_RESET;
        } else {
            sistema_estado = SISTEMA_NORMAL;
        }
    }
}

// ======================================================
// WIFI
// ======================================================

void wifi_init(void) {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t ap = {
        .ap = {
            .ssid           = SSID,
            .ssid_len       = strlen(SSID),
            .password       = PASS,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    ESP_LOGI(TAG, "WiFi listo. Conectate a %s", SSID);
}

// ======================================================
// HANDLERS HTTP
// ======================================================

esp_err_t root_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

esp_err_t set_get(httpd_req_t *req) {
    char query[64];
    char val[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }

    // Puerta
    if (httpd_query_key_value(query, "door", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "open")  == 0) puerta_estado = PUERTA_ABIERTA;
        if (strcmp(val, "close") == 0) puerta_estado = PUERTA_CERRADA;
        enviar_uart();
    }

    // LEDs ventana — activa bandera manual para proteger el estado
    if (httpd_query_key_value(query, "light", val, sizeof(val)) == ESP_OK) {
        luz_vent_manual = true;   // El usuario tomó el control
        if (strcmp(val, "on")  == 0) {
            luz_vent_estado = LUZ_ENCENDIDA;
            if (brillo_luz == 0) brillo_luz = 50;
        }
        if (strcmp(val, "off") == 0) {
            luz_vent_estado = LUZ_APAGADA;
            brillo_luz = 0;
        }
        enviar_uart();
    }

    // Reset — libera el control manual
    if (httpd_query_key_value(query, "reset", val, sizeof(val)) == ESP_OK) {
        if (atoi(val) == 1) {
            luz_vent_manual   = false;
            sistema_estado    = SISTEMA_RESET;
            puerta_estado     = PUERTA_CERRADA;
            luz_vent_estado   = LUZ_APAGADA;
            brillo_luz        = 0;
            luz_puerta_estado = LUZ_APAGADA;
            enviar_uart();
            sistema_estado    = SISTEMA_NORMAL;
        }
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t state_get(httpd_req_t *req) {
    recibir_estado_uart();

    char resp[256];
    sprintf(resp,
        "{\"puerta\":\"%s\",\"luz_ventana\":\"%s\",\"brillo\":%d,"
        "\"luz_puerta\":\"%s\",\"reset\":\"%s\"}",
        puerta_estado     == PUERTA_ABIERTA ? "ABIERTA"    : "CERRADA",
        luz_vent_estado   == LUZ_ENCENDIDA  ? "ENCENDIDOS" : "APAGADOS",
        brillo_luz,
        luz_puerta_estado == LUZ_ENCENDIDA  ? "ENCENDIDA"  : "APAGADA",
        sistema_estado    == SISTEMA_RESET  ? "RESET"      : "NORMAL"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// ======================================================
// SERVIDOR WEB
// ======================================================

void start_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &config);

    httpd_uri_t root  = { .uri = "/",      .method = HTTP_GET, .handler = root_get  };
    httpd_uri_t set   = { .uri = "/set",   .method = HTTP_GET, .handler = set_get   };
    httpd_uri_t state = { .uri = "/state", .method = HTTP_GET, .handler = state_get };

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &state);
}

// ======================================================
// MAIN
// ======================================================

void app_main(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    uart_init();
    wifi_init();
    start_server();

    while (1) {
        recibir_estado_uart();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
