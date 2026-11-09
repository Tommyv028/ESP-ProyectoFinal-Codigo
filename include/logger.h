#ifndef LOGGER_H
#define LOGGER_H

#include "sensores.h"

void iniciar_logger(void);
void leer_log(void);
void borrar_log(void);
void guardar_datos(datos_sensores_t data);

#endif
