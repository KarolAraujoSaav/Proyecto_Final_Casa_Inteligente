// ======================================================
// ESP32_WIFI
//
// Se comunica con el ESP32_MAESTRO mediante UART:
//
// RX_2 ← Recibe estados del maestro.
// TX_4 → Envía comandos al maestro.
//
// Protocolo UART compartido:
//"puerta,brillo,luz_ventana,luz_puerta,reset\n"
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
#include "esp_netif.h"            // Interfaz de red.
#include "esp_wifi.h"             // Control WiFi.
#include "esp_http_server.h"      // Servidor HTTP

#include "driver/gpio.h"
#include "driver/uart.h"          // Comunicación UART.


// ======================================================
// CONFIGURACIÓN UART
// ======================================================

#define UART_PORT UART_NUM_1  // Puerto UART utilizado.

// Pines UART.
#define UART_TX GPIO_NUM_4  // Cambio de pines.
#define UART_RX GPIO_NUM_2

#define UART_BAUD_RATE 9600  // Velocidad de transmisión.
#define UART_BUF_SIZE 256


// ======================================================
// CONFIGURACIÓN WIFI
// ======================================================

#define SSID "CASA_INTELIGENTE"  // Nombre de la red WiFi.
#define PASS "87654321"  // Contraseña de la red (mínimo 8 caracteres). 


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

static estado_puerta_t puerta_estado = PUERTA_CERRADA;  // Estado actual de la puerta.

static estado_luz_t luz_vent_estado = LUZ_APAGADA;

static int brillo_luz = 0;  // Nivel de brillo de LEDs.

static estado_reset_t sistema_estado = SISTEMA_NORMAL;  // Estado general del sistema.

static estado_luz_t luz_puerta_estado = LUZ_APAGADA;  // Estado de luz encima de la puerta.


// ======================================================
// HTML (PÁGINA WEB)
// ======================================================

static const char html[] =

"<!DOCTYPE html>"   // Define el documento como HTML5.
"<html lang='es'>"  // Idioma principal de la página

"<head>"

"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"  // Hace que la página sea adaptable a celulares.

"<title>Casa Inteligente</title>"  // Título mostrado en la pestaña del navegador.

"<style>"

"*{"
"margin:0;"
"padding:0;"
"box-sizing:border-box;"
"font-family:Arial,sans-serif;"
"}"

"body{"
"background:linear-gradient(135deg,#0f172a,#1e293b);"
"color:white;"    // Texto blanco.
"padding:20px;"
"text-align:center;"
"}"

// Estilo del título principal.
"h1{"
"margin-bottom:20px;"
"font-size:2rem;"
"}"

// Contenedor principal de tarjetas.
".container{"
"display:flex;"
"flex-direction:column;"
"gap:20px;"
"max-width:500px;"
"margin:auto;"
"}"

".card{"
"background:#1e293b;"
"padding:20px;"
"border-radius:20px;"
"box-shadow:0 4px 15px rgba(0,0,0,0.4);"
"}"

".card h2{"
"margin-bottom:15px;"
"}"


// Configuración general de botones.
"button{"
"border:none;"
"padding:12px 18px;"
"margin:5px;"
"border-radius:12px;"
"font-size:1rem;"
"font-weight:bold;"
"cursor:pointer;"
"transition:0.3s;"
"}"

// Botón verde para acciones de encendido o apertura.
".on{"
"background:#22c55e;"
"color:white;"
"}"

// Botón rojo para apagar o cerrar.
".off{"
"background:#ef4444;"
"color:white;"
"}"

".warning{"
"background:#f59e0b;"
"color:white;"
"}"

// Efecto al pasar el mouse sobre botones.
"button:hover{"
"transform:scale(1.05);"
"}"

".status{"
"margin-top:10px;"
"font-size:1.1rem;"
"font-weight:bold;"
"}"

// Configuración del slider de brillo.
"input[type=range]{"
"width:100%;"
"margin-top:15px;"
"}"

".value{"
"margin-top:10px;"
"font-size:1.2rem;"
"}"

".door-open{"
"color:#22c55e;"
"}"

".door-close{"
"color:#ef4444;"
"}"

"</style>"

"</head>"

"<body>"

"<h1> CASA INTELIGENTE</h1>"  // Encabezado principal de la página.

"<div class='container'>"


// ======================================================
// CONTROL PUERTA
// ======================================================

"<div class='card'>"

"<h2> Control de la Puerta</h2>"   // Título de sección.

"<button class='on' onclick=\"setDoor('open')\">"  // Botón para abrir la puerta.
"ABRIR"
"</button>"

"<button class='off' onclick=\"setDoor('close')\">" // Botón para cerrar la puerta.
"CERRAR"
"</button>"

"<div class='status' id='estado_puerta'>"  // Texto que muestra el estado actual.
"CERRADA"
"</div>"

"</div>"

// ======================================================
// CONTROL LEDS VENTANA
// ======================================================

"<div class='card'>"

"<h2> Iluminación en ventanas</h2>"  // Título de sección.

"<button class='on' onclick=\"setLight('on')\">"  // Botón para encender LEDs.
"ENCENDER"
"</button>"

"<button class='off' onclick=\"setLight('off')\">"  // Botón para apagar LEDs.
"APAGAR"
"</button>"

// ======================================================
// SLIDER BRILLO
// ======================================================

// Barra deslizante para controlar el brillo.
"<input "
"type='range' "
"min='0' "
"max='100' "
"value='0' "
"id='slider' "
"oninput='setBrightness(this.value)'>"

"<div class='value' id='brillo_txt'>"  // Texto que muestra porcentaje actual.
"0%"
"</div>"

"<div class='status' id='estado_luz'>"  // Texto que indica el estado de los LEDs.
"APAGADOS"
"</div>"

"</div>"

// ======================================================
// ESTADO LUZ PUERTA
// ======================================================

"<div class='card'>"

"<h2> Luz de la Puerta</h2>"  // Título de sección.

"<div class='status' id='estado_luz_puerta'>"
"APAGADA"
"</div>"

"</div>"


// ======================================================
// RESET SISTEMA
// ======================================================

"<div class='card'>"

"<h2>⚙  Sistema</h2>"  // Título de sección.

"<button class='warning' onclick='resetSystem()'>"  // Botón para reiniciar sistema.
"REINICIAR"
"</button>"

"<div class='status' id='estado_reset'>"  // Texto que muestra estado del sistema.
"NORMAL"
"</div>"

"</div>"

"</div>"



// ======================================================
// JAVASCRIPT
// ======================================================


"<script>"

"function setDoor(action){"  // Envía comando para abrir/cerrar puerta.

" fetch('/set?door='+action).then(update);"

"}"

"function setLight(action){"  // Envía comando para LEDs de ventana.

" fetch('/set?light='+action).then(update);"

"}"

"function setBrightness(value){"  // Envía nivel de brillo seleccionado.

" fetch('/set?brightness='+value).then(update);"

"}"

"function resetSystem(){"  // Solicita reinicio del sistema.

" fetch('/set?reset=1').then(update);"

"}"

"function update(){"  // Solicita estados actuales al ESP32.

" fetch('/state')"

" .then(r=>r.json())"

" .then(s=>{"

"   document.getElementById('estado_puerta').innerText=s.puerta;"

"   document.getElementById('estado_luz').innerText=s.luz_ventana;"

"   document.getElementById('brillo_txt').innerText=s.brillo+'%';"

"   document.getElementById('slider').value=s.brillo;"

"   document.getElementById('estado_luz_puerta').innerText=s.luz_puerta;"

"   document.getElementById('estado_reset').innerText=s.reset;"

"   let puerta=document.getElementById('estado_puerta');"

"   if(s.puerta=='ABIERTA'){"  // Cambia color según estado.

"       puerta.className='status door-open';"  // Verde si está abierta.

"   }"

"   else{"

"       puerta.className='status door-close';"  // Rojo si está cerrada.

"   }"

" });"

"}"

"setInterval(update,1000);"  // Actualiza estados automáticamente. Lo cambie a cada segundo en vez se 1.2 s.

"update();"  // Ejecuta actualización inicial al cargar página.

"</script>"

"</body></html>";


// ======================================================
// CONFIGURACIÓN UART
// ======================================================

void uart_init(void) {  // Inicializa la comunicación UART.

    uart_config_t cfg = {

        .baud_rate = UART_BAUD_RATE,

        .data_bits = UART_DATA_8_BITS,

        .parity = UART_PARITY_DISABLE,

        .stop_bits = UART_STOP_BITS_1,

        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_PORT, &cfg);
    
    // Configura pines TX y RX.
    
    uart_set_pin(
        UART_PORT,
        UART_TX,
        UART_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
    
    // Instala driver UART.
    
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

void recibir_estado_uart(void) {  // Recibe estados enviados por el ESP32 maestro.

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

    if (sscanf((char *)buf, "%d,%d,%d,%d,%d", &p, &b, &lv, &lp, &r) == 5) {   // Intenta separar los datos recibidos.

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

void wifi_init(void) {  // Inicializa el ESP32 como Access Point.

    esp_netif_create_default_wifi_ap();  // Crea interfaz WiFi AP.

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

esp_err_t set_get(httpd_req_t *req) {  // Recibe comandos desde la página web.

    char query[64];

    char val[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {  // Obtiene parámetros de URL.

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

esp_err_t state_get(httpd_req_t *req) {  // Envía estados actuales en formato JSON.

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
