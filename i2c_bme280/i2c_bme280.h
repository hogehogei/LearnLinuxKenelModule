#ifndef I2C_BME280_H_INCLUDED
#define I2C_BME280_H_INCLUDED

#include <linux/ioctl.h>

typedef struct bme280_comp_temperature_t
{
    uint16_t t1;
    int16_t t2;
    int16_t t3;
} bme280_comp_temperature;

typedef struct bme280_comp_pressure_t
{
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
} bme280_comp_pressure;

typedef struct bme280_comp_humidity_t
{
    uint8_t h1;
    int16_t h2;
    uint8_t h3;
    int16_t h4;
    int16_t h5;
    int8_t  h6;
} bme280_comp_humidity;

// ioctl用パラメータ
typedef struct i2c_bme280_ioctl_param_t
{
    int32_t pressure;
    int32_t temperature;
    int32_t humidity;
    bme280_comp_temperature dig_t;
    bme280_comp_pressure    dig_p;
    bme280_comp_humidity    dig_h;
} i2c_bme280_ioctl_param;


#define BME280_IOC_TYPE 'M'
// ioctl コマンド
// 1:   環境測定データ読み取り
//      tempareture, humidity, pressure に読み取りデータを格納
//      読み取るのは未校正の生値のため、呼び出し側で別途補正計算を行うこと
//      補正値は I2C_BME280_READ_COMPENSATION で読みだしておくこと
#define I2C_BME280_READ_ENV_MEASURED    _IOR(BME280_IOC_TYPE, 1, i2c_bme280_ioctl_param)
// 2:   校正データ読み取り
//      compensation_* に校正値を読み取り。この校正値を使って環境測定データの読み取りを行うこと
#define I2C_BME280_READ_COMPENSATION    _IOR(BME280_IOC_TYPE, 2, i2c_bme280_ioctl_param)

#endif      // I2C_BME280_H_INCLUDED