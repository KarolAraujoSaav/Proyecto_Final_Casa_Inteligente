#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_http_server.h"

#define WIFI_SSID       "FACHADA_WIFI"
#define WIFI_PASS       "12345678"

typedef struct {
    uint8_t comando_puerta;
    uint8_t brillo_leds;
    uint8_t luz_puerta;
    uint8_t reset;
} datos_web_t;

datos_web_t datos_web = {
    .comando_puerta = 0,
    .brillo_leds = 0,
    .luz_puerta = 0,
    .reset = 0
};

//String comando_web = ""; verificar si es o no necesario

uint8_t web_abrir_puerta = 0;
uint8_t web_cerrar_puerta = 0;
uint8_t web_reset = 0;
uint8_t web_luz_puerta = 0;
uint8_t web_brillo_leds = 0;

static const char *TAG = "WIFI_WEB";

void wifi_init_softap(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Red creada: %s", WIFI_SSID);
    ESP_LOGI(TAG, "IP: 192.168.4.1");
}

esp_err_t pagina_principal_handler(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Fachada Inteligente</title>"
        "<style>"
        "body{font-family:Arial;text-align:center;background:#f2f2f2;}"
        "button{width:220px;height:45px;margin:8px;font-size:18px;}"
        "input{width:220px;}"
        ".card{background:white;padding:20px;margin:20px;border-radius:12px;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h2>Fachada Inteligente</h2>"
        "<p>Control remoto del sistema</p>"

        "<a href='/abrir'><button>Abrir puerta</button></a><br>"
        "<a href='/cerrar'><button>Cerrar puerta</button></a><br>"
        "<a href='/luz_on'><button>Encender luz puerta</button></a><br>"
        "<a href='/luz_off'><button>Apagar luz puerta</button></a><br>"
        "<a href='/reset'><button>Reiniciar sistema</button></a><br>"

        "<h3>Brillo LEDs ventana</h3>"
        "<form action='/brillo' method='get'>"
        "<input type='range' name='valor' min='0' max='100' value='50'>"
        "<br><br>"
        "<button type='submit'>Enviar brillo</button>"
        "</form>"

        "</div>"
        "</body>"
        "</html>";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t abrir_handler(httpd_req_t *req) {
    datos_web.comando_puerta = 1;
    datos_web.reset = 0;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t cerrar_handler(httpd_req_t *req) {
    datos_web.comando_puerta = 0;
    datos_web.reset = 0;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t luz_on_handler(httpd_req_t *req) {
    datos_web.luz_puerta = 1;
    datos_web.reset = 0;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t luz_off_handler(httpd_req_t *req) {
    datos_web.luz_puerta = 0;
    datos_web.reset = 0;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t reset_handler(httpd_req_t *req) {
    datos_web.comando_puerta = 0;
    datos_web.brillo_leds = 0;
    datos_web.luz_puerta = 0;
    datos_web.reset = 1;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t brillo_handler(httpd_req_t *req) {
    char query[50];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char valor[10];

        if (httpd_query_key_value(query, "valor", valor, sizeof(valor)) == ESP_OK) {
            int brillo = atoi(valor);

            if (brillo >= 0 && brillo <= 100) {
                datos_web.brillo_leds = brillo;
            }
        }
    }

    datos_web.reset = 0;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t cmd_handler(httpd_req_t *req) {
    char respuesta[50];

    sprintf(
        respuesta,
        "%d,%d,%d,%d",
        datos_web.comando_puerta,
        datos_web.brillo_leds,
        datos_web.luz_puerta,
        datos_web.reset
    );

    httpd_resp_send(req, respuesta, HTTPD_RESP_USE_STRLEN);

    if (datos_web.reset == 1) {
        datos_web.reset = 0;
    }

    return ESP_OK;
}

void iniciar_servidor_web(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    httpd_start(&server, &config);

    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = pagina_principal_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_abrir = {
        .uri = "/abrir",
        .method = HTTP_GET,
        .handler = abrir_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_cerrar = {
        .uri = "/cerrar",
        .method = HTTP_GET,
        .handler = cerrar_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_luz_on = {
        .uri = "/luz_on",
        .method = HTTP_GET,
        .handler = luz_on_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_luz_off = {
        .uri = "/luz_off",
        .method = HTTP_GET,
        .handler = luz_off_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_reset = {
        .uri = "/reset",
        .method = HTTP_GET,
        .handler = reset_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_brillo = {
        .uri = "/brillo",
        .method = HTTP_GET,
        .handler = brillo_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_cmd = {
        .uri = "/cmd",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_abrir);
    httpd_register_uri_handler(server, &uri_cerrar);
    httpd_register_uri_handler(server, &uri_luz_on);
    httpd_register_uri_handler(server, &uri_luz_off);
    httpd_register_uri_handler(server, &uri_reset);
    httpd_register_uri_handler(server, &uri_brillo);
    httpd_register_uri_handler(server, &uri_cmd);
}

void app_main(void) {
    wifi_init_softap();
    iniciar_servidor_web();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}