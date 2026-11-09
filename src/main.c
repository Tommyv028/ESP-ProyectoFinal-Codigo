#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "config_pines.h"
#include "sensores.h"
#include "logger.h"
#include "wifi_firebase.h"

// Variable global para controlar la ejecución del bucle principal
static volatile bool s_run_loop = true;

static void monitor_task(void *arg) {
    char cmd_buffer[64];
    int index = 0;

    // Pequeña espera para que el monitor serie se estabilice
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("\n--- Consola de Comandos ---\n");
    printf("Escribe 'readlog' para leer el archivo.\n");
    printf("Escribe 'clearlog' para borrar el archivo.\n");
    printf("Escribe 's' para detener la recolección.\n");
    printf("Escribe 'c' para continuar la recolección.\n");
    printf(">");
    fflush(stdout);

    while (1) {
        int c = fgetc(stdin);
        if (c != EOF) {
            
            if (c == '\b' || c == 127) {
                if (index > 0) {
                    index--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (c == '\n' || c == '\r') {
                cmd_buffer[index] = '\0';
                if (strcmp(cmd_buffer, "readlog") == 0) {
                    printf("\n");
                    leer_log();
                } else if (strcmp(cmd_buffer, "clearlog") == 0) {
                    printf("\n");
                    borrar_log();
                } else if (strcmp(cmd_buffer, "s") == 0) {
                    s_run_loop = false;
                    printf("\nRecolección de datos DETENIDA (Stop).");
                } else if (strcmp(cmd_buffer, "c") == 0) {
                    s_run_loop = true;
                    printf("\nRecolección de datos REANUDADA (Continue).");
                } else if (index > 0) {
                    printf("\nComando no reconocido: '%s'", cmd_buffer);
                }
                index = 0;
                printf("\n>");
                fflush(stdout);
            } else if (isprint(c) && index < (sizeof(cmd_buffer) - 1)) {
                printf("%c", c);
                fflush(stdout);
                cmd_buffer[index++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void app_main(void) {
    configurar_pines(); // Inicializamos al configuracion de pines
    iniciar_logger(); // Inicializamos el sistema de logging
    iniciar_sensores(); // Inicializamos sensores
    iniciar_wifi(); // Iniciamos Wi-Fi y NVS

    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 5, NULL);

    while (1) {
        if (s_run_loop) {
            datos_sensores_t datos = leer_sensores(SENSOR_TODOS);
            guardar_datos(datos); // Guardamos localmente
            firebase_enviar_datos(datos); // Enviamos a la nube
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Esperamos 5 segundos para la siguiente lectura
    }   
}