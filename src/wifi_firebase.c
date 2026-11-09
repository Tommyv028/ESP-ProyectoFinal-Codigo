#include "wifi_firebase.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"

// --- TUS CREDENCIALES ---
#define WIFI_SSID      "WiFi-ProyectoFinal"
#define WIFI_PASS      "ProyectoFinal2026"

// --- CREDENCIALES FIREBASE ---
#define FIREBASE_API_KEY      "AIzaSyCNZ4r0KUzA8_vb2U4Hbo8r9cM-xrgZlD8" 
#define FIREBASE_USER_EMAIL   "tomas.valori@gmail.com"                     
#define FIREBASE_USER_PASSWORD "Proyecto2026"                           

// URLs
#define FIREBASE_AUTH_URL     "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY
#define FIREBASE_DB_URL       "https://esp-proyectofinal-default-rtdb.firebaseio.com/sensores.json"

static const char *TAG = "WIFI_FIREBASE";

// Handler "Event group" para sincronizar la conexión
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

// Buffer para Token de autenticación
static char s_id_token[2048] = {0};

// Handler eventos de Wi-Fi e IP
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) { // Reintentar 10 veces
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando conectar al AP...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Fallo la conexion al AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Obtenida IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void iniciar_wifi(void)
{
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "Esperando conexión Wi-Fi...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado al SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Fallo al conectar al SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Evento inesperado");
    }
}

// Handler eventos HTTP
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    return ESP_OK;
}

// Estructura para respuesta del login
typedef struct {
    char *buffer;
    int index;
} response_data_t;

// Handler para recibir el JSON de login
esp_err_t _login_event_handler(esp_http_client_event_t *evt) {
    response_data_t *resp = (response_data_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->index + evt->data_len < 2048) {
            memcpy(resp->buffer + resp->index, evt->data, evt->data_len);
            resp->index += evt->data_len;
            resp->buffer[resp->index] = 0; // Null terminate
        }
    }
    return ESP_OK;
}

void firebase_autenticar(void) {
    ESP_LOGI(TAG, "Iniciando autenticación con Firebase...");
    
    char *response_buf = malloc(2048);
    if (response_buf == NULL) {
        ESP_LOGE(TAG, "No hay memoria para el buffer de login");
        return;
    }
    response_data_t resp_data = { .buffer = response_buf, .index = 0 };

    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",
             FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);

    esp_http_client_config_t config = {
        .url = FIREBASE_AUTH_URL,
        .event_handler = _login_event_handler,
        .user_data = &resp_data,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
        // Parsear JSON para "idToken"
        cJSON *json = cJSON_Parse(response_buf);
        if (json) {
            cJSON *idToken = cJSON_GetObjectItem(json, "idToken");
            if (cJSON_IsString(idToken) && (idToken->valuestring != NULL)) {
                strncpy(s_id_token, idToken->valuestring, sizeof(s_id_token) - 1);
                ESP_LOGI(TAG, "Autenticación exitosa. Token obtenido.");
            }
            cJSON_Delete(json);
        }
    } else {
        ESP_LOGE(TAG, "Error en login. Status: %d", esp_http_client_get_status_code(client));
    }

    esp_http_client_cleanup(client);
    free(response_buf);
}

void firebase_enviar_datos(datos_sensores_t datos) {
    // Verificar conexión
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi no conectado, saltando envio a Firebase");
        return;
    }

    // Sin token
    if (strlen(s_id_token) == 0) {
        firebase_autenticar();
        if (strlen(s_id_token) == 0) return;
    }

    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "{\"temperatura\": %.2f, \"humedad\": %.2f, \"presion\": %.2f, \"luz\": %.2f, \"ruido\": %.0f, \"tolueno\": %.2f, \"timestamp\": {\".sv\": \"timestamp\"}}",
             datos.temperatura, datos.humedad, datos.presion, datos.luz, datos.ruido, datos.tolueno);

    // Construimos la URL
    char *full_url = malloc(2500); 
    if (full_url == NULL) return;
    
    snprintf(full_url, 2500, "%s?auth=%s", FIREBASE_DB_URL, s_id_token);

    esp_http_client_config_t config = {
        .url = full_url,
        .event_handler = _http_event_handler,
        .method = HTTP_METHOD_POST, 
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Configuramos el header para JSON
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Realizamos la petición
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Datos enviados a Firebase. Status = %d, content_length = %lld, Data: %s", 
                status_code,
                esp_http_client_get_content_length(client), post_data);
        
        // Error 401 -> token expirado.
        if (status_code == 401) {
            ESP_LOGW(TAG, "Token expirado o inválido. Reseteando token.");
            memset(s_id_token, 0, sizeof(s_id_token));
        }
    } else {
        ESP_LOGE(TAG, "Error enviando a Firebase: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(full_url);
}
