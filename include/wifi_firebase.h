#ifndef WIFI_FIREBASE_H
#define WIFI_FIREBASE_H

#include "sensores.h" // Necesitamos esto para conocer el tipo datos_sensores_t

// Inicia la conexión Wi-Fi y espera a que se conecte
void iniciar_wifi(void);

// Envía los datos a Firebase mediante una petición HTTP POST
void firebase_enviar_datos(datos_sensores_t datos);

#endif // WIFI_FIREBASE_H
