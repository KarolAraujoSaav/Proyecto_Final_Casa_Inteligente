// ======================================================
// ESP32_WIFI
// Crea un Access Point WiFi con página web de control.
//
// Se comunica con el ESP32_MAESTRO mediante UART:
//
// RX_16 ← Recibe estados del maestro.
// TX_17 → Envía comandos al maestro.
//
// Protocolo UART compartido:
//
// "puerta,brillo,luz_ventana,luz_puerta,reset\n"
//
// Ejemplo:
// "1,75,1,0,0\n"
//
// Baud Rate UART: 9600
// Red WiFi AP: CASA_INTELIGENTE
// Contraseña: 87654321
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

#define UART_PORT UART_NUM_1
#define UART_TX GPIO_NUM_17
#define UART_RX GPIO_NUM_16
#define UART_BAUD_RATE 9600
#define UART_BUF_SIZE 256


// ======================================================
// CONFIGURACIÓN WIFI
// ======================================================

#define SSID "CASA_INTELIGENTE"
#define PASS "87654321"


// ======================================================
// TAG LOGGER
// ======================================================

static const char *TAG = "WIFI";


// ======================================================
// ENUMERACIONES
// ======================================================

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


// ======================================================
// VARIABLES GLOBALES
// ======================================================

static estado_puerta_t puerta_estado = PUERTA_CERRADA;

static estado_luz_t luz_vent_estado = LUZ_APAGADA;

static int brillo_luz = 0;

static estado_reset_t sistema_estado = SISTEMA_NORMAL;

static estado_luz_t luz_puerta_estado = LUZ_APAGADA;


// ======================================================
// HTML (PÁGINA WEB)
// ======================================================

static const char html[] =

"<!DOCTYPE html><html>"

"<head>"

"<meta name='viewport' content='width=device-width, initial-scale=1'>"

"<title>Casa Inteligente</title>"

"</head>"

"<body style='text-align:center;font-family:sans-serif;'>"


// ======================================================
// CONTROL PUERTA
// ======================================================

"<h2>CONTROL DE PUERTA</h2>"

"<button onclick=\"setDoor('open')\">ABRIR PUERTA</button>"

"<br><br>"

"<button onclick=\"setDoor('close')\">CERRAR PUERTA</button>"

"<br><br>"

"<p id='estado_puerta'>Estado puerta: CERRADA</p>"

"<hr>"


// ======================================================
// CONTROL LEDS VENTANA
// ======================================================

"<h2>CONTROL LEDS VENTANA</h2>"

"<button onclick=\"setLight('on')\">ENCENDER LEDS</button>"

"<br><br>"

"<button onclick=\"setLight('off')\">APAGAR LEDS</button>"

"<br><br>"


// ======================================================
// SLIDER BRILLO
// ======================================================

"<input type='range' min='0' max='100' value='0' id='slider' "
"oninput='setBrightness(this.value)'>"

"<br><br>"

"<p id='brillo_txt'>Brillo: 0%</p>"

"<p id='estado_luz'>Estado LEDs ventana: APAGADOS</p>"

"<hr>"


// ======================================================
// ESTADO LUZ PUERTA
// ======================================================

"<h2>ESTADO LUZ PUERTA</h2>"

"<p id='estado_luz_puerta'>Luz puerta: APAGADA</p>"

"<hr>"


// ======================================================
// RESET SISTEMA
// ======================================================

"<h2>RESET DEL SISTEMA</h2>"

"<button onclick=\"resetSystem()\">REINICIAR SISTEMA</button>"

"<br><br>"

"<p id='estado_reset'>Sistema: NORMAL</p>"


// ======================================================
// JAVASCRIPT
// ======================================================

"<script>"

"function setDoor(action){"

" fetch('/set?door='+action).then(update);"

"}"

"function setLight(action){"

" fetch('/set?light='+action).then(update);"

"}"

"function setBrightness(value){"

" fetch('/set?brightness='+value).then(update);"

"}"

"function resetSystem(){"

" fetch('/set?reset=1').then(update);"

"}"

"function update(){"

" fetch('/state').then(r=>r.json()).then(s=>{"

"  document.getElementById('estado_puerta').innerText='Estado puerta: '+s.puerta;"

"  document.getElementById('estado_luz').innerText='Estado LEDs ventana: '+s.luz_ventana;"

"  document.getElementById('brillo_txt').innerText='Brillo: '+s.brillo+'%';"

"  document.getElementById('slider').value=s.brillo;"

"  document.getElementById('estado_luz_puerta').innerText='Luz puerta: '+s.luz_puerta;"

"  document.getElementById('estado_reset').innerText='Sistema: '+s.reset;"

" });"

"}"

"setInterval(update,1000);"

"update();"

"</script>"

"</body></html>";


// ======================================================
// CONFIGURACIÓN UART
// ======================================================

void uart_init(void) {

    uart_config_t cfg = {

        .baud_rate = UART_BAUD_RATE,

        .data_bits = UART_DATA_8_BITS,

        .parity = UART_PARITY_DISABLE,

        .stop_bits = UART_STOP_BITS_1,

        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_PORT, &cfg);

    uart_set_pin(
        UART_PORT,
        UART_TX,
        UART_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    uart_driver_install(
        UART_PORT,
        UART_BUF_SIZE * 2,
        0,
        0,
        NULL,
        0
    );

    ESP_LOGI(
        TAG,
        "UART listo: TX=%d RX=%d @%d baud",
        UART_TX,
        UART_RX,
        UART_BAUD_RATE
    );
}


// ======================================================
// ENVÍO UART
// ======================================================

void enviar_uart(void) {

    char trama[40];

    int n = snprintf(
        trama,
        sizeof(trama),
        "%d,%d,%d,%d,%d\n",
        (int)puerta_estado,
        brillo_luz,
        (int)luz_vent_estado,
        (int)luz_puerta_estado,
        (int)sistema_estado
    );

    uart_write_bytes(UART_PORT, trama, n);
}


// ======================================================
// RECEPCIÓN UART
// ======================================================

void recibir_estado_uart(void) {

    uint8_t buf[UART_BUF_SIZE];

    int len = uart_read_bytes(
        UART_PORT,
        buf,
        sizeof(buf) - 1,
        pdMS_TO_TICKS(10)
    );

    if (len <= 0) {

        return;
    }

    buf[len] = '\0';

    int p, b, lv, lp, r;

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d", &p, &b, &lv, &lp, &r) == 5) {

        if (p == 0 || p == 1) {

            puerta_estado = (estado_puerta_t)p;
        }

        if (b >= 0 && b <= 100) {

            brillo_luz = b;
        }

        if (lv == 0 || lv == 1) {

            luz_vent_estado = (estado_luz_t)lv;
        }

        if (lp == 0 || lp == 1) {

            luz_puerta_estado = (estado_luz_t)lp;
        }

        if (r == 0 || r == 1) {

            sistema_estado = (estado_reset_t)r;
        }
    }
}


// ======================================================
// CONFIGURACIÓN WIFI
// ======================================================

void wifi_init(void) {

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    esp_wifi_init(&cfg);

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

    ESP_LOGI(TAG, "WiFi listo. Conectate a %s", SSID);
}


// ======================================================
// HANDLER "/"
// ======================================================

esp_err_t root_get(httpd_req_t *req) {

    httpd_resp_set_type(req, "text/html");

    httpd_resp_send(req, html, strlen(html));

    return ESP_OK;
}


// ======================================================
// HANDLER "/set"
// ======================================================

esp_err_t set_get(httpd_req_t *req) {

    char query[64];

    char val[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {

        httpd_resp_sendstr(req, "OK");

        return ESP_OK;
    }


    // ======================================================
    // CONTROL PUERTA
    // ======================================================

    if (httpd_query_key_value(query, "door", val, sizeof(val)) == ESP_OK) {

        if (strcmp(val, "open") == 0) {

            puerta_estado = PUERTA_ABIERTA;
        }

        if (strcmp(val, "close") == 0) {

            puerta_estado = PUERTA_CERRADA;
        }

        enviar_uart();
    }


    // ======================================================
    // CONTROL LEDS VENTANA
    // ======================================================

    if (httpd_query_key_value(query, "light", val, sizeof(val)) == ESP_OK) {

        if (strcmp(val, "on") == 0) {

            luz_vent_estado = LUZ_ENCENDIDA;

            if (brillo_luz == 0) {

                brillo_luz = 50;
            }
        }

        if (strcmp(val, "off") == 0) {

            luz_vent_estado = LUZ_APAGADA;

            brillo_luz = 0;
        }

        enviar_uart();
    }


    // ======================================================
    // CONTROL BRILLO
    // ======================================================

    if (httpd_query_key_value(query, "brightness", val, sizeof(val)) == ESP_OK) {

        brillo_luz = atoi(val);

        if (brillo_luz < 0) {

            brillo_luz = 0;
        }

        if (brillo_luz > 100) {

            brillo_luz = 100;
        }

        luz_vent_estado = (brillo_luz > 0)
                          ? LUZ_ENCENDIDA
                          : LUZ_APAGADA;

        enviar_uart();
    }


    // ======================================================
    // RESET SISTEMA
    // ======================================================

    if (httpd_query_key_value(query, "reset", val, sizeof(val)) == ESP_OK) {

        if (atoi(val) == 1) {

            sistema_estado = SISTEMA_RESET;

            puerta_estado = PUERTA_CERRADA;

            luz_vent_estado = LUZ_APAGADA;

            brillo_luz = 0;

            luz_puerta_estado = LUZ_APAGADA;

            enviar_uart();

            sistema_estado = SISTEMA_NORMAL;
        }
    }

    httpd_resp_sendstr(req, "OK");

    return ESP_OK;
}


// ======================================================
// HANDLER "/state"
// ======================================================

esp_err_t state_get(httpd_req_t *req) {

    recibir_estado_uart();

    char resp[256];

    sprintf(
        resp,
        "{\"puerta\":\"%s\",\"luz_ventana\":\"%s\",\"brillo\":%d,"
        "\"luz_puerta\":\"%s\",\"reset\":\"%s\"}",
        puerta_estado == PUERTA_ABIERTA
            ? "ABIERTA"
            : "CERRADA",

        luz_vent_estado == LUZ_ENCENDIDA
            ? "ENCENDIDOS"
            : "APAGADOS",

        brillo_luz,

        luz_puerta_estado == LUZ_ENCENDIDA
            ? "ENCENDIDA"
            : "APAGADA",

        sistema_estado == SISTEMA_RESET
            ? "RESET"
            : "NORMAL"
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

    httpd_uri_t root = {

        .uri = "/",

        .method = HTTP_GET,

        .handler = root_get
    };

    httpd_uri_t set = {

        .uri = "/set",

        .method = HTTP_GET,

        .handler = set_get
    };

    httpd_uri_t state = {

        .uri = "/state",

        .method = HTTP_GET,

        .handler = state_get
    };

    httpd_register_uri_handler(server, &root);

    httpd_register_uri_handler(server, &set);

    httpd_register_uri_handler(server, &state);
}


// ======================================================
// FUNCIÓN PRINCIPAL
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
