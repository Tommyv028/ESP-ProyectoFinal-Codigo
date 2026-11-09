#include "sensores.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "config_pines.h"

static const char *TAG = "SENSORES";

// --- Constantes para el BMP180 ---
#define BMP180_OSS 0 // Oversampling Setting (0-3). 0 para ultra bajo consumo.

// Un handle para el bus de I2C
i2c_master_bus_handle_t BUS_HANDLE;

// Handles de dispositivo para cada sensor en el bus
i2c_master_dev_handle_t AHT25_HANDLE;
i2c_master_dev_handle_t BMP180_HANDLE;
i2c_master_dev_handle_t BH1750_HANDLE;

// Estructura para almacenar los 11 valores de calibración del BMP180
typedef struct {
    int16_t  ac1;
    int16_t  ac2;
    int16_t  ac3;
    uint16_t ac4;
    uint16_t ac5;
    uint16_t ac6;
    int16_t  b1;
    int16_t  b2;
    int16_t  mb;
    int16_t  mc;
    int16_t  md;
} bmp180_calib_data_t;

// Una variable global para almacenar los datos después de leerlos del sensor
static bmp180_calib_data_t bmp180_calib_data;

// Prototipos para funciones internas
static void bmp180_leer_datos_calibracion(void);
static void leer_aht25(datos_sensores_t *data);
static void leer_bmp180(datos_sensores_t *data);
static void leer_bh1750(datos_sensores_t *data);
static void leer_ruido(datos_sensores_t *data);
static void leer_tolueno(datos_sensores_t *data);



static void i2c_scan(i2c_master_bus_handle_t bus_handle) {
    ESP_LOGI(TAG, "Escaneando bus I2C...");
    uint8_t address;
    esp_err_t err;
    int devices_found = 0;
    for (address = 1; address < 127; address++) {
        // El timeout de -1 usa el default del bus
        err = i2c_master_probe(bus_handle, address, -1);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Dispositivo encontrado en la dirección 0x%02X", address);
            devices_found++;
        } else if (err != ESP_ERR_TIMEOUT) {
            // ESP_ERR_TIMEOUT es normal si no hay dispositivo. Otros errores pueden indicar un problema en el bus.
        }
    }
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No se encontraron dispositivos I2C. Revisa el cableado y las resistencias de pull-up.");
    }
}



void iniciar_sensores(void) {
    
    // Sección para inicializar el bus I2C y los sensores

    i2c_master_bus_config_t I2C_MASTER_CONFIG = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&I2C_MASTER_CONFIG, &BUS_HANDLE));

    // Escaneamos el bus para ver qué dispositivos responden
    i2c_scan(BUS_HANDLE);

    i2c_device_config_t AHT25_CONFIG = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = AHT25_ADDRESS,
        .scl_speed_hz = 400000,
    };

    i2c_device_config_t BMP180_CONFIG = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = BMP180_ADDRESS,
        .scl_speed_hz = 400000,
    };

    i2c_device_config_t BH1750_CONFIG = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = BH1750_ADDRESS,
        .scl_speed_hz = 400000,
    };

    if (i2c_master_bus_add_device(BUS_HANDLE, &AHT25_CONFIG, &AHT25_HANDLE) != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al agregar AHT25 al bus I2C");
        AHT25_HANDLE = NULL;
    }
    if (i2c_master_bus_add_device(BUS_HANDLE, &BMP180_CONFIG, &BMP180_HANDLE) != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al agregar BMP180 al bus I2C");
        BMP180_HANDLE = NULL;
    }
    if (i2c_master_bus_add_device(BUS_HANDLE, &BH1750_CONFIG, &BH1750_HANDLE) != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al agregar BH1750 al bus I2C");
        BH1750_HANDLE = NULL;
    }

    // ----------------------------------------------------------------------------------

    // AHT25: Inicio
    if (AHT25_HANDLE) {
        uint8_t aht25_init_cmd[] = {0xBA}; // Comando de soft reset
        esp_err_t err = i2c_master_transmit(AHT25_HANDLE, aht25_init_cmd, sizeof(aht25_init_cmd), pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fallo de inicialización AHT25: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40)); // Esperamos para estabilizar
    }

    // BH1750: Inicio
    if (BH1750_HANDLE) {
        uint8_t bh1750_power_on_cmd[] = {0x01}; // Power On
        esp_err_t err = i2c_master_transmit(BH1750_HANDLE, bh1750_power_on_cmd, sizeof(bh1750_power_on_cmd), pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fallo de inicialización BH1750: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40)); // Esperamos para estabilizar
    }

    // BMP180: Verificación del handle y Calibración
    // BMP180 no requiere comando de inicio o soft reset.
    bmp180_leer_datos_calibracion();

    // Configuración ADC para sensor de tolueno
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR_TOLUENO_ADC, ADC_ATTEN_DB_12);
}

static void bmp180_leer_datos_calibracion(void) {
    if (!BMP180_HANDLE) {
        ESP_LOGW(TAG, "BMP180 no inicializado, omitiendo lectura de calibración.");
        return;
    }
    uint8_t calib_buffer[22] = {0}; //Tamaño buffer para 11 valores de calibración.
    uint8_t reg_addr = 0xAA; // Dirección de memoria de inicio.

    // Escribimos el comando y leemos los 22 bytes de datos de calibración.
    esp_err_t err = i2c_master_transmit_receive(BMP180_HANDLE, &reg_addr, 1, calib_buffer, sizeof(calib_buffer), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en la lectura de datos de calibración BMP180: %s", esp_err_to_name(err));
        return;
    }

    // Desempaquetar los 22 bytes en la estructura de calibración
    // Escribimos los valores en la estructura de calibración
    bmp180_calib_data.ac1 = (calib_buffer[0]  << 8) | calib_buffer[1];
    bmp180_calib_data.ac2 = (calib_buffer[2]  << 8) | calib_buffer[3];
    bmp180_calib_data.ac3 = (calib_buffer[4]  << 8) | calib_buffer[5];
    bmp180_calib_data.ac4 = (calib_buffer[6]  << 8) | calib_buffer[7];
    bmp180_calib_data.ac5 = (calib_buffer[8]  << 8) | calib_buffer[9];
    bmp180_calib_data.ac6 = (calib_buffer[10] << 8) | calib_buffer[11];
    bmp180_calib_data.b1  = (calib_buffer[12] << 8) | calib_buffer[13];
    bmp180_calib_data.b2  = (calib_buffer[14] << 8) | calib_buffer[15];
    bmp180_calib_data.mb  = (calib_buffer[16] << 8) | calib_buffer[17];
    bmp180_calib_data.mc  = (calib_buffer[18] << 8) | calib_buffer[19];
    bmp180_calib_data.md  = (calib_buffer[20] << 8) | calib_buffer[21];
    ESP_LOGI(TAG, "Lectura de datos de calibración BMP180 completa");
}

static void leer_aht25(datos_sensores_t *datos) {
    if (!AHT25_HANDLE) {
        ESP_LOGW(TAG, "AHT25 no inicializado, omitiendo lectura.");
        return;
    }

    esp_err_t err;
    uint8_t aht25_data[6] = {0};
    uint8_t aht25_trigger_cmd[] = {0xAC, 0x33, 0x00};
    err = i2c_master_transmit(AHT25_HANDLE, aht25_trigger_cmd, sizeof(aht25_trigger_cmd), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en el comando de disparo AHT25");
        return;
    } else {
        ESP_LOGI(TAG, "Comando de disparo AHT25 enviado correctamente");
    }

    vTaskDelay(pdMS_TO_TICKS(80)); // Esperamos a que la medición se complete

    err = i2c_master_receive(AHT25_HANDLE, aht25_data, sizeof(aht25_data), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al leer datos de AHT25: %s", esp_err_to_name(err));
        return;
    } else {
        ESP_LOGI(TAG, "Datos de AHT25 leídos correctamente");
    }

    if ((aht25_data[0] & 0x08) == 0) {
        ESP_LOGW(TAG, "AHT25 no está calibrado");
    }
    uint32_t raw_humidity = ((uint32_t)aht25_data[1] << 12) | ((uint32_t)aht25_data[2] << 4) | (aht25_data[3] >> 4);
    uint32_t raw_temperature = (((uint32_t)aht25_data[3] & 0x0F) << 16) | ((uint32_t)aht25_data[4] << 8) | aht25_data[5];
    datos->temperatura = (float)raw_temperature * 200.0f / 1048576.0f - 50.0f;
    datos->humedad = (float)raw_humidity * 100.0f / 1048576.0f;
}

static void leer_bmp180(datos_sensores_t *datos) {
    if (!BMP180_HANDLE) {
        ESP_LOGW(TAG, "BMP180 no inicializado, omitiendo lectura.");
        return;
    }

    esp_err_t err;
    
    long ut = 0;
    long up = 0;


    // 1. Leer Temperatura Sin Compensar (UT)
    uint8_t cmd_temp[] = {0xF4, 0x2E};
    err = i2c_master_transmit(BMP180_HANDLE, cmd_temp, sizeof(cmd_temp), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en el comando de temperatura BMP180: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t temp_data[2];
    uint8_t temp_reg = 0xF6;
    err = i2c_master_transmit_receive(BMP180_HANDLE, &temp_reg, 1, temp_data, 2, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al leer temperatura BMP180: %s", esp_err_to_name(err));
        return;
    }
    ut = (temp_data[0] << 8) | temp_data[1];

    // 2. Leer Presión Sin Compensar (UP) - OSS = 0 (modo estándar)
    uint8_t cmd_presion[] = {0xF4, 0x34 + (BMP180_OSS << 6)};
    err = i2c_master_transmit(BMP180_HANDLE, cmd_presion, sizeof(cmd_presion), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en el comando de presión BMP180: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t datos_presion[3];
    uint8_t reg_presion = 0xF6;
    err = i2c_master_transmit_receive(BMP180_HANDLE, &reg_presion, 1, datos_presion, 3, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al leer presión BMP180: %s", esp_err_to_name(err));
        return;
    }
    up = ((datos_presion[0] << 16) | (datos_presion[1] << 8) | datos_presion[2]) >> (8 - BMP180_OSS);

    // 3. Calcular Temperatura y Presión Real (usando datos de calibración)
    long x1, x2, b5, b6, b3, p;
    unsigned long b4, b7;
    long x3;

    // Cálculo de temperatura
    x1 = ((ut - (long)bmp180_calib_data.ac6) * (long)bmp180_calib_data.ac5) >> 15; // Usamos UP :|
    long x1_plus_md = x1 + bmp180_calib_data.md;
    if (x1_plus_md == 0) {
        ESP_LOGE(TAG, "Error en el calculo de temperatura BMP180 (división por cero)");
        return;
    }
    x2 = ((long)bmp180_calib_data.mc << 11) / x1_plus_md;
    b5 = x1 + x2;
    // Podemos usar b5 para la lectura de temperatura del BMP (usamos el AHT25 para temperatura, más preciso)
    float bmp_temp = ((b5 + 8) >> 4) / 10.0f;
    ESP_LOGI(TAG, "BMP180 Temperatura calculada: %.2f C", bmp_temp);


    // Cálculo de presión
    b6 = b5 - 4000;
    x1 = ((long)bmp180_calib_data.b2 * (b6 * b6 >> 12)) >> 11;
    x2 = ((long)bmp180_calib_data.ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = ((((long)bmp180_calib_data.ac1 * 4 + x3) + 2) / 4);
    x1 = ((long)bmp180_calib_data.ac3 * b6) >> 13;
    x2 = ((long)bmp180_calib_data.b1 * (b6 * b6 >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((unsigned long)bmp180_calib_data.ac4 * (unsigned long)(x3 + 32768)) >> 15;
    if (b4 == 0) {
        ESP_LOGE(TAG, "Error en el calculo de presión BMP180 (división por cero)");
        return;
    }
    b7 = ((unsigned long)up - b3) * (50000); // Usamos UT :|
    p = (b7 < 0x80000000) ? ((b7 * 2) / b4) : ((b7 / b4) * 2);
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4); // Dato final de presión en Pascales

    datos->presion = p / 100.0f;

    ESP_LOGI(TAG, "BMP180 Presión calculada: %.2f hPa", (p/100.0f));
}

static void leer_bh1750(datos_sensores_t *datos) {
    if (!BH1750_HANDLE) {
        ESP_LOGW(TAG, "BH1750 no inicializado, omitiendo lectura.");
        return;
    }

    esp_err_t err;
    // Comando para iniciar una medición en modo de alta resolución (One-Time H-Resolution Mode)
    uint8_t cmd[] = {0x20};
    err = i2c_master_transmit(BH1750_HANDLE, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar medición en BH1750");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(180));

    uint8_t bh1750_data[2] = {0};
    err = i2c_master_receive(BH1750_HANDLE, bh1750_data, sizeof(bh1750_data), pdMS_TO_TICKS(1000));
    if (err == ESP_OK) {
        uint16_t raw_light = ((uint16_t)bh1750_data[0] << 8) | bh1750_data[1];
        datos->luz = (float)raw_light / 1.2f;
    } else {
        ESP_LOGE(TAG, "Fallo al leer datos de BH1750: %s", esp_err_to_name(err));
    }
}

static void leer_ruido(datos_sensores_t *datos) {
    datos->ruido = (float)gpio_get_level(PIN_RUIDO);
}

static void leer_tolueno(datos_sensores_t *datos) {
    float v_out = (float)adc1_get_raw(SENSOR_TOLUENO_ADC) / 4095.0f * 5.0f;
    float log_ppm = -7.071f + (2.852f * v_out) + (-0.210 * v_out * v_out);
    datos->tolueno = powf(10.0f, log_ppm);
}

datos_sensores_t leer_sensores(seleccion_sensor_t mascara_sensor) {
    datos_sensores_t datos = {0};

    if (mascara_sensor & SENSOR_AHT25) {
        leer_aht25(&datos);
        ESP_LOGI(TAG, "Temperatura AHT25: %.2f C, Humedad: %.2f %%", datos.temperatura, datos.humedad);
    }
    if (mascara_sensor & SENSOR_BMP180) {
        leer_bmp180(&datos);
        ESP_LOGI(TAG, "Presión BMP180: %.2f hPa", datos.presion);
    }
    if (mascara_sensor & SENSOR_BH1750) {
        leer_bh1750(&datos);
        ESP_LOGI(TAG, "Luz BH1750: %.2f lux", datos.luz);
    }
    if (mascara_sensor & SENSOR_RUIDO) {
        leer_ruido(&datos);
        ESP_LOGI(TAG, "Ruido: %.0f", datos.ruido);
    } 
    if (mascara_sensor & SENSOR_TOLUENO) {
        leer_tolueno(&datos);
        ESP_LOGI(TAG, "Tolueno ppm: %.2f ppm", datos.tolueno);
    }

    ESP_LOGI(TAG, "T: %.2f C, H: %.2f %%, P: %.2f hPa, L: %.2f lux, R: %.0f, tPPM: %.2f",
        datos.temperatura, datos.humedad, datos.presion, 
        datos.luz, datos.ruido, datos.tolueno);

    return datos;
}