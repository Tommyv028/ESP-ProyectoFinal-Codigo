#ifndef SENSORES_H
#define SENSORES_H

#define I2C_MASTER_NUM 0
#define I2C_SCL_IO 22
#define I2C_SDA_IO 21

#define AHT25_ADDRESS 0x38
#define BMP180_ADDRESS 0x77
#define BH1750_ADDRESS 0x23

typedef enum {
    SENSOR_AHT25  = 1 << 0,
    SENSOR_BMP180 = 1 << 1,
    SENSOR_BH1750 = 1 << 2,
    SENSOR_RUIDO  = 1 << 3,
    SENSOR_TOLUENO = 1 << 4,
    SENSOR_TODOS    = SENSOR_AHT25 | SENSOR_BMP180 | SENSOR_BH1750 | SENSOR_RUIDO | SENSOR_TOLUENO,
} seleccion_sensor_t;

typedef struct {
    float temperatura;
    float humedad;
    float presion;
    float luz;
    float ruido;
    float tolueno;
} datos_sensores_t;

void iniciar_sensores(void);

datos_sensores_t leer_sensores(seleccion_sensor_t mascara_sensor);

#endif
