#include "logger.h"
#include "esp_spiffs.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "LOGGER";
static SemaphoreHandle_t log_mutex = NULL;

void iniciar_logger(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create log mutex");
    }
        
}

void leer_log(void) {
    if (xSemaphoreTake(log_mutex, portMAX_DELAY) != pdTRUE) return;

    ESP_LOGI(TAG, "--- Leyendo contenido de /spiffs/datos.txt ---");
    FILE *f = fopen("/spiffs/datos.txt", "r");
    if (f != NULL) {
        char linea[128];
        while (fgets(linea, sizeof(linea), f) != NULL) {
            linea[strcspn(linea, "\n")] = 0;
            printf("%s\n", linea);
        }
        fclose(f);
    } else {
        ESP_LOGI(TAG, "No se pudo abrir el archivo para leer.");
    }

    ESP_LOGI(TAG, "--- Fin de la lectura ---");
    xSemaphoreGive(log_mutex);
}

void borrar_log(void) {
    if (xSemaphoreTake(log_mutex, portMAX_DELAY) != pdTRUE) return;

    if (unlink("/spiffs/datos.txt") == 0) {
        ESP_LOGI(TAG, "Archivo /spiffs/datos.txt borrado correctamente.");
    } else {
        ESP_LOGW(TAG, "No se pudo borrar el archivo.");
    }

    xSemaphoreGive(log_mutex);
}

void guardar_datos(datos_sensores_t data) {
   if (xSemaphoreTake(log_mutex, portMAX_DELAY) != pdTRUE) return;

    FILE *f = fopen("/spiffs/datos.txt", "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "No se pudo abrir el archivo para escribir");
        xSemaphoreGive(log_mutex);
        return;
    }

    fprintf(f, "temp:%.2f hum:%.2f pres:%.2f lux:%.2f ruido:%.0f tPPM:%.2f\n",
        data.temperatura, data.humedad, data.presion,
        data.luz, data.ruido, data.tolueno);
    fclose(f);

    xSemaphoreGive(log_mutex);
} 