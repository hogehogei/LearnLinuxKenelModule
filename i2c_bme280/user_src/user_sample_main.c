#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>

// my driver header file
#include "../i2c_bme280.h"

typedef struct
{
    double temperature;
    double pressure;
    double humidity;
} measured_value;

double bme280_compensate_temp( const i2c_bme280_ioctl_param* param, int32_t* t_fine_out );
double bme280_compensate_pressure( const i2c_bme280_ioctl_param* param, int32_t t_fine );
double bme280_compensate_humidity( const i2c_bme280_ioctl_param* param, int32_t t_fine );

int main( void )
{
    int fd;
    int result;
    i2c_bme280_ioctl_param param;
    measured_value value;
    int32_t t_fine;

    fd = open( "/dev/i2c_bme280", O_RDONLY );
    if( fd < 0 ){
        perror( "open failed." );
        return -1;
    }

    result = ioctl( fd, I2C_BME280_READ_COMPENSATION, &param );
    if( result < 0 ){
        perror( "ioctl read compensation failed." );
        return -1;
    }
    result = ioctl( fd, I2C_BME280_READ_ENV_MEASURED, &param );
    if( result < 0 ){
        perror( "ioctl read measured values failed." );
        return -1;
    }

    value.temperature = bme280_compensate_temp( &param, &t_fine );
    value.pressure    = bme280_compensate_pressure( &param, t_fine );
    value.humidity    = bme280_compensate_humidity( &param, t_fine );

    printf( "BME280 measurement\n" );
    printf( "temperature=%.2lf, pressure=%.2lf, humidity=%.2lf\n", value.temperature, value.pressure, value.humidity );

    if( close(fd) != 0 ){
        perror("close");
        return -1;
    }

    return 0;
}


double bme280_compensate_temp( const i2c_bme280_ioctl_param* param, int32_t* t_fine_out )
{
    const bme280_comp_temperature* dig;
    int32_t adc_T;
    int32_t var1, var2, T;

    dig = &(param->dig_t);
    adc_T = param->temperature;

    var1 = ((((adc_T >> 3) - ((int32_t)dig->t1 << 1))) * ((int32_t)dig->t2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig->t1)) * ((adc_T >> 4) - ((int32_t)dig->t1))) >> 12) * ((int32_t)dig->t3)) >> 14;
    
    *t_fine_out = var1 + var2;
    T = ((*t_fine_out) * 5 + 128) >> 8;

    return T / 100.0;
}

double bme280_compensate_pressure( const i2c_bme280_ioctl_param* param, int32_t t_fine )
{
    const bme280_comp_pressure* dig;
    int32_t adc_P;
    int64_t var1, var2, P;

    dig = &(param->dig_p);
    adc_P = param->pressure;

    var1 = (((int64_t)t_fine) >> 1) - (int64_t)128000;
    var2 = var1 * var1 * (int64_t)dig->p6;
    var2 = var2 + ((var1 * (int64_t)dig->p5) << 17);
    var2 = var2 + (((int64_t)dig->p4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig->p3) >> 8) + ((var1 * (int64_t)dig->p2) << 12);
    var1 = ((((int64_t)1) << 47) + var1) * ((int64_t)dig->p1) >> 33;
    if( var1 == 0 ){
        return 0;
    }

    P = 1048576 - adc_P;
    P = (((P << 31) - var2) * 3125) / var1;

    var1 = (((int64_t)dig->p9) * (P >> 13) * (P >> 13)) >> 25;
    var2 = (((int64_t)dig->p8) * P) >> 19;

    P = ((P + var1 + var2) >> 8) + (((int64_t)dig->p7) << 4);
    return (uint32_t)P / 256.0 / 100.0;     // divide by 100 means Pa -> hPa
}

double bme280_compensate_humidity( const i2c_bme280_ioctl_param* param, int32_t t_fine )
{
    const bme280_comp_humidity* dig;
    int32_t adc_H;
    int32_t v_x1;

    dig = &(param->dig_h);
    adc_H = param->humidity;

    v_x1 = (t_fine - ((int32_t)76800));
    v_x1 = (((((adc_H << 14) -(((int32_t)dig->h4) << 20) - (((int32_t)dig->h5) * v_x1)) + 
              ((int32_t)16384)) >> 15) * (((((((v_x1 * ((int32_t)dig->h6)) >> 10) * 
              (((v_x1 * ((int32_t)dig->h3)) >> 11) + ((int32_t) 32768))) >> 10) + ((int32_t)2097152)) * 
              ((int32_t) dig->h2) + 8192) >> 14));
    v_x1 = (v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)dig->h1)) >> 4));
    v_x1 = (v_x1 < 0 ? 0 : v_x1);
    v_x1 = (v_x1 > 419430400 ? 419430400 : v_x1);

    return ((uint32_t)(v_x1 >> 12)) / 1024.0;   
}