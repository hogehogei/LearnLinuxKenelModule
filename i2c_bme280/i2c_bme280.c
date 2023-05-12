#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <asm/current.h>
#include <asm/uaccess.h>

// なぜか <stdint.h> をインクルードできないので
// とりあえず自前で定義
typedef s8  int8_t;
typedef u8  uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
#include "i2c_bme280.h"



// 
// define constants
//
#define DRIVER_NAME   "i2c_bme280"
#define DRIVER_CLASS  "i2c_bme280_class"

// Minor number using this device driver
static const unsigned int MINOR_BASE = 0;
// Minor number counts using this device driver
static const unsigned int I2C_BANK  = 1;

//
// declare static functions, structs
//
// 各I2Cデバイス(client)に紐づけ。probe時に i2c_set_clientdata で設定
typedef struct
{
    struct cdev        cdev;
    dev_t              alloced_device_region;
    struct class*      class;
    struct i2c_client* client;          
} i2c_bme280_device_private;

static int i2c_bme280_create_cdev( i2c_bme280_device_private* dev_info );
static void i2c_bme280_remove_cdev( i2c_bme280_device_private* dev_info );

static int i2c_bme280_probe( struct i2c_client *client, const struct i2c_device_id *id );
static int i2c_bme280_remove( struct i2c_client *client);
static int i2c_bmc280_init_reg( struct i2c_client *client );

static int i2c_bme280_open( struct inode *inode, struct file *file );
static int i2c_bme280_close( struct inode *inode, struct file *file );
static ssize_t i2c_bme280_read( struct file *filp, char __user *buf, size_t count, loff_t *f_pos );
static ssize_t i2c_bme280_write( struct file *filp, const char __user *buf, size_t count, loff_t *f_pos );
static long i2c_bme280_ioctl( struct file *filp, unsigned int cmd, unsigned long arg );

static int i2c_bme280_read_env_measured( struct file *filp, i2c_bme280_ioctl_param __user* param );
static int i2c_bme280_read_compensation( struct file *filp, i2c_bme280_ioctl_param __user* param );

static int i2c_bme280_read_regs_data( struct i2c_client* client, const u8* regs, u8* dst, size_t count );

// 
// define static variables
//

// このデバイスドライバで取り扱うデバイスを識別するテーブル
static struct i2c_device_id i2c_bme280_idtable[] = {
    { "i2c_bme280", 0 },
    {},
};
MODULE_DEVICE_TABLE(i2c, i2c_bme280_idtable);

static struct i2c_driver i2c_bme280_driver = {
    .driver = {
        .name  = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .id_table  = i2c_bme280_idtable,
    .probe     = i2c_bme280_probe,
    .remove    = i2c_bme280_remove,
};

static struct file_operations s_bme280_driver_fops = {
    .open    = i2c_bme280_open,
    .release = i2c_bme280_close,
    .read    = i2c_bme280_read,
    .write   = i2c_bme280_write,
    .unlocked_ioctl = i2c_bme280_ioctl,
    .compat_ioctl = i2c_bme280_ioctl,
};


//
// define static const variables
//

// BME280 temperature compensation registers "dig_T*"
static const u8 sk_bme280_temperature_comp_reg[] = {
    0x88, 0x89, 
    0x8A, 0x8B,
    0x8C, 0x8D
};
#define I2C_BME280_DIGT_REG_NUM ((int)sizeof(sk_bme280_temperature_comp_reg))

// BME280 pressure compensation registers "dig_P*"
static const u8 sk_bme280_pressure_comp_reg[] = {
    0x8E, 0x8F,
    0x90, 0x91,
    0x92, 0x93,
    0x94, 0x95,
    0x96, 0x97,
    0x98, 0x99,
    0x9A, 0x9B,
    0x9C, 0x9D,
    0x9E, 0x9F,
};
#define I2C_BME280_DIGP_REG_NUM ((int)sizeof(sk_bme280_pressure_comp_reg))

// BME280 humidity compensation registers "dig_H*"
static const u8 sk_bme280_humidity_comp_reg[] = {
    0xA1,
    0xE1, 0xE2,
    0xE3,
    0xE4, 0xE5, 0xE6,
    0xE7,
};
#define I2C_BME280_DIGH_REG_NUM ((int)sizeof(sk_bme280_humidity_comp_reg))

// BME280 read pressure register "press"
static const u8 sk_bme280_press_reg[] = {
    0xF7, 0xF8, 0xF9,
};
#define I2C_BME280_PRESS_REG_NUM ((int)sizeof(sk_bme280_press_reg))

// BME280 read temperature register "temp"
static const u8 sk_bme280_temp_reg[] = {
    0xFA, 0xFB, 0xFC,
};
#define I2C_BME280_TEMP_REG_NUM ((int)sizeof(sk_bme280_temp_reg))

// BME280 read humidity register "hum"
static const u8 sk_bme280_hum_reg[] = {
    0xFD, 0xFE,
};
#define I2C_BME280_HUM_REG_NUM ((int)sizeof(sk_bme280_hum_reg))

static int i2c_bme280_create_cdev( i2c_bme280_device_private* dev_info )
{
    dev_t curr_dev;
    int result = 0;
    struct device *created_dev = NULL;

    // 空いているメジャー番号を確保
    result = alloc_chrdev_region( &(dev_info->alloced_device_region), MINOR_BASE, I2C_BANK, DRIVER_NAME );
    if( result < 0 ){
        pr_err( "%s failed. alloc_chrdev_region = %d\n", __func__, result );
        goto REGION_ERR;
    }

    // デバイスクラス登録  /sys/class に見えるようになる
    dev_info->class = class_create( THIS_MODULE, DRIVER_CLASS );
    if( IS_ERR(dev_info->class) ){
        result = PTR_ERR( dev_info->class );
        pr_err( "%s failed. class_create = %d\n", __func__, result );
        goto CREATE_CLASS_ERR;
    }

    // ファイル操作関数をバインド
    cdev_init( &(dev_info->cdev), &s_bme280_driver_fops );
    dev_info->cdev.owner = THIS_MODULE;
    // デバイス番号を生成
    curr_dev = MKDEV(MAJOR(dev_info->alloced_device_region), MINOR(dev_info->alloced_device_region));
    // このデバイスドライバをカーネルに登録する
    result = cdev_add( &(dev_info->cdev), curr_dev, 1 );
    if( result != 0 ){
        pr_err( "%s failed. cdev_add = %d\n", __func__, result );
        goto CDEV_ADD_ERR;
    }

    // デバイスノードを作成。作成したノードは/dev以下からアクセス可能
    created_dev = device_create( 
            dev_info->class,
            NULL,               // no parent device
            curr_dev,
            NULL,               // no additional data
            DRIVER_NAME );      // i2c_bme280


    if( IS_ERR(created_dev) ){
        result = PTR_ERR( created_dev );
        pr_err( "%s failed. device_create = %d\n", __func__, result );
        goto DEV_CREATE_ERR;
    }

    pr_info( "%s succeeded", __func__ );

    // initialize succeeded
    return 0;

    // error bailout
DEV_CREATE_ERR:
    cdev_del( &dev_info->cdev );
CDEV_ADD_ERR:
    class_destroy( dev_info->class );
CREATE_CLASS_ERR:
    unregister_chrdev_region( dev_info->alloced_device_region, I2C_BANK );
REGION_ERR:
    return -ENXIO;
}

static void i2c_bme280_remove_cdev( i2c_bme280_device_private* dev_info )
{
    dev_t dev;
    dev = MKDEV(MAJOR(dev_info->alloced_device_region), MINOR(dev_info->alloced_device_region));

    // デバイスノード削除
    device_destroy( dev_info->class, dev );
    // キャラクターデバイスをKernelから削除
    cdev_del( &(dev_info->cdev) );
    // デバイスのクラス登録を削除
    class_destroy( dev_info->class );
    // デバイスが使用していたメジャー番号の登録削除
    unregister_chrdev_region( dev, I2C_BANK );
}

static int i2c_bme280_probe( struct i2c_client *client, const struct i2c_device_id *id )
{
    int chipid;
    i2c_bme280_device_private* dev_info;

    pr_info( "%s\n", __func__ );
    pr_info( "id.name = %s, id.driver_data = %ld\n", id->name, id->driver_data );
    pr_info( "slave address = 0x%02X\n", client->addr );

    // check functionallity smbus read
    if( !i2c_check_functionality( client->adapter, I2C_FUNC_SMBUS_BYTE_DATA )){
        return -EIO;
    }

    // check connected device is bme280 or not
    // read chipid
    chipid = i2c_smbus_read_byte_data( client, 0xD0 );
    if( chipid != 0x60 ){
        pr_err( "connected device is not bme280! chipid = 0x%02X\n", chipid );
        return -ENODEV;
    }

    // コンフィギュレーションレジスタの設定
    if( i2c_bmc280_init_reg( client ) != 0 ){
        return -ENODEV;
    }

    // デバイスに紐づけてメモリ確保、アンロード時に自動開放
    // devm_kzalloc は probe 時に使用することを想定しているらしい
    dev_info = (i2c_bme280_device_private*)devm_kzalloc(&client->dev, sizeof(i2c_bme280_device_private), GFP_KERNEL);
    dev_info->client = client;
    i2c_set_clientdata( client, dev_info );

    pr_info( "detected bme280. chipid = 0x%02X\n", chipid );
    if( i2c_bme280_create_cdev(dev_info) != 0 ){
        return -ENXIO;
    }

    return 0;
}

static int i2c_bme280_remove( struct i2c_client *client )
{
    i2c_bme280_device_private* dev_info;
    pr_info( "%s\n", __func__ );

    dev_info = i2c_get_clientdata( client );
    i2c_bme280_remove_cdev( dev_info );

    return 0;
}

static int i2c_bmc280_init_reg( struct i2c_client *client )
{  
    u8 reg;
    u8 value;

    // set "config(0xF5)" register
    // t_sb[2:0]   = 0.5ms(000)
    // filter[2:0] = filter x16(100)
    // spi3w_en[0] = 3wire SPI(0)
    // value = |000|100|*|0|
    reg = 0xF5;
    value = 0x10;
    pr_info( "%s set reg=0x%02X, value=0x%02X", __func__, reg, value );
    if( i2c_smbus_write_byte_data( client, reg, value ) != 0 ){
        goto I2C_BMC280_INIT_REG_BAILOUT;
    }

    // set "ctrl_meas(0xF4)" register
    // osrs_t[2:0]     = oversamplingx2(010)
    // osrs_p[2:0]     = oversamplingx16(101)
    // mode[1:0]       = normal mode(11)
    // value = |010|101|11|
    reg = 0xF4;
    value = 0x57;
    pr_info( "%s set reg=0x%02X, value=0x%02X", __func__, reg, value );
    if( i2c_smbus_write_byte_data( client, reg, value ) != 0 ){
        goto I2C_BMC280_INIT_REG_BAILOUT;
    }

    // set "ctrl_hum(0xF2)" register
    // osrs_h[2:0]  = oversamplingx1(001)
    // value = |*****|001|
    reg = 0xF2;
    value = 0x01;
    pr_info( "%s set reg=0x%02X, value=0x%02X", __func__, reg, value );
    if( i2c_smbus_write_byte_data( client, reg, value ) != 0 ){
        goto I2C_BMC280_INIT_REG_BAILOUT;
    }

    return 0;

I2C_BMC280_INIT_REG_BAILOUT:
    pr_err( "%s command failed reg=0x%02X, value=0x%02X", __func__, reg, value );
    return -ENODEV;
}

// open時に呼ばれる関数
static int i2c_bme280_open( struct inode *inode, struct file *filp )
{
    i2c_bme280_device_private* dev_info;
    pr_debug( "%s", __func__ );

    dev_info = container_of(inode->i_cdev, i2c_bme280_device_private, cdev);
    if( dev_info == NULL || dev_info->client == NULL ){
        pr_err( "%s container_of() failed\n", __func__ );
        return -EFAULT;
    }

    filp->private_data = dev_info;
    pr_debug( "i2c address = 0x%02X", dev_info->client->addr );

    return 0;
}

// close時に呼ばれる関数
static int i2c_bme280_close( struct inode *inode, struct file *filp )
{
    pr_debug( "%s", __func__ );
    return 0;
}

// read時に呼ばれる関数
static ssize_t i2c_bme280_read( struct file* filp, char __user *buf, size_t count, loff_t *f_pos )
{
    pr_debug( "%s", __func__ );
    return 0;
}

// write時に呼ばれる関数
static ssize_t i2c_bme280_write( struct file *filp, const char __user *buf, size_t count, loff_t *f_pos )
{
    pr_debug( "%s", __func__ );
    return 0;
}

static long i2c_bme280_ioctl( struct file *filp, unsigned int cmd, unsigned long arg )
{
    i2c_bme280_ioctl_param __user* param;
    param = (i2c_bme280_ioctl_param __user*)arg;

    pr_debug( "%s", __func__ );

    switch( cmd ){
    case I2C_BME280_READ_ENV_MEASURED:
        return i2c_bme280_read_env_measured( filp, param );
    case I2C_BME280_READ_COMPENSATION:
        return i2c_bme280_read_compensation( filp, param );
    default:
        pr_warn( "unsupported command %d\n", cmd );
        return -EINVAL;
    }

    return 0;
}

static int i2c_bme280_read_env_measured( struct file *filp, i2c_bme280_ioctl_param __user* param )
{
    u8 reg_press[I2C_BME280_PRESS_REG_NUM];
    u8 reg_temp[I2C_BME280_TEMP_REG_NUM];
    u8 reg_hum[I2C_BME280_HUM_REG_NUM];

    s32 pressure;
    s32 temperature;
    s32 humidity;

    i2c_bme280_device_private* dev_info;
    struct i2c_client* client;

    dev_info = (i2c_bme280_device_private*)filp->private_data;
    client = dev_info->client;

    // read pressure data
    i2c_bme280_read_regs_data( client, sk_bme280_press_reg, reg_press, I2C_BME280_PRESS_REG_NUM );
    // read temperature data
    i2c_bme280_read_regs_data( client, sk_bme280_temp_reg, reg_temp, I2C_BME280_TEMP_REG_NUM );
    // read humidity data
    i2c_bme280_read_regs_data( client, sk_bme280_hum_reg, reg_hum, I2C_BME280_HUM_REG_NUM );

    pressure = (u32)reg_press[0] << 16 | (u32)reg_press[1] << 8 | (u32)reg_press[2];
    pressure >>= 4;
    temperature = (u32)reg_temp[0] << 16 | (u32)reg_temp[1] << 8 | (u32)reg_temp[2];
    temperature >>= 4;
    humidity = (u32)reg_hum[0] << 8 | (u32)reg_hum[1];

    // copy to user space
    if( copy_to_user( (void __user*)&(param->pressure), &pressure, sizeof(pressure)) != 0 ){
        pr_err( "%s copy_to_user pressure failed.", __func__ );
        return -EIO;
    }
    if( copy_to_user( (void __user*)&(param->temperature), &temperature, sizeof(temperature)) != 0 ){
        pr_err( "%s copy_to_user temperature failed.", __func__ );
        return -EIO;
    }
    if( copy_to_user( (void __user*)&(param->humidity), &humidity, sizeof(humidity)) != 0 ){
        pr_err( "%s copy_to_user humidity failed.", __func__ );
        return -EIO;
    }

    return 0;
}

static int i2c_bme280_read_compensation( struct file *filp, i2c_bme280_ioctl_param __user* param )
{
    u8 reg_t[I2C_BME280_DIGT_REG_NUM];
    u8 reg_p[I2C_BME280_DIGP_REG_NUM];
    u8 reg_h[I2C_BME280_DIGH_REG_NUM];

    bme280_comp_temperature dig_t;
    bme280_comp_pressure    dig_p;
    bme280_comp_humidity    dig_h;

    i2c_bme280_device_private* dev_info;
    struct i2c_client* client;

    dev_info = (i2c_bme280_device_private*)filp->private_data;
    client = dev_info->client;

    // read temperature compensation data
    if( i2c_bme280_read_regs_data( client, sk_bme280_temperature_comp_reg, reg_t, I2C_BME280_DIGT_REG_NUM ) != 0 ){
        return -ENODEV;
    }
    // read pressure compensation data
    if( i2c_bme280_read_regs_data( client, sk_bme280_pressure_comp_reg, reg_p, I2C_BME280_DIGP_REG_NUM ) != 0 ){
        return -ENODEV;
    }
    // read humidity compensation data
    if( i2c_bme280_read_regs_data( client, sk_bme280_humidity_comp_reg, reg_h, I2C_BME280_DIGH_REG_NUM ) != 0 ){
        return -ENODEV;
    }

    // ok. format compensation data.
    dig_t.t1 =       (u16)reg_t[0] | ((u16)reg_t[1] << 8);
    dig_t.t2 = (s16)((u16)reg_t[2] | ((u16)reg_t[3] << 8));
    dig_t.t3 = (s16)((u16)reg_t[4] | ((u16)reg_t[5] << 8));

    dig_p.p1 =       (u16)reg_p[0] | ((u16)reg_p[1] << 8);
    dig_p.p2 = (s16)((u16)reg_p[2] | ((u16)reg_p[3] << 8));
    dig_p.p3 = (s16)((u16)reg_p[4] | ((u16)reg_p[5] << 8));
    dig_p.p4 = (s16)((u16)reg_p[6] | ((u16)reg_p[7] << 8));
    dig_p.p5 = (s16)((u16)reg_p[8] | ((u16)reg_p[9] << 8));
    dig_p.p6 = (s16)((u16)reg_p[10] | ((u16)reg_p[11] << 8));
    dig_p.p7 = (s16)((u16)reg_p[12] | ((u16)reg_p[13] << 8));
    dig_p.p8 = (s16)((u16)reg_p[14] | ((u16)reg_p[15] << 8));
    dig_p.p9 = (s16)((u16)reg_p[16] | ((u16)reg_p[17] << 8));

    dig_h.h1 = reg_h[0];
    dig_h.h2 = (s16)((u16)reg_h[1] | ((u16)reg_h[2] << 8));
    dig_h.h3 = reg_h[3];
    dig_h.h4 = (s16)(((u16)reg_h[4] << 4) | (u16)(reg_h[5] & 0x0F));
    dig_h.h5 = (s16)(((u16)reg_h[5] >> 4) | ((u16)reg_h[6] << 8));
    dig_h.h6 = reg_h[7];

    // copy to user space
    if( copy_to_user( (void __user*)&(param->dig_t), &dig_t, sizeof(dig_t)) != 0 ){
        pr_err( "%s copy_to_user dig_t failed.", __func__ );
        return -EIO;
    }
    if( copy_to_user( (void __user*)&(param->dig_p), &dig_p, sizeof(dig_p)) != 0 ){
        pr_err( "%s copy_to_user dig_p failed.", __func__ );
        return -EIO;
    }
    if( copy_to_user( (void __user*)&(param->dig_h), &dig_h, sizeof(dig_h)) != 0 ){
        pr_err( "%s copy_to_user dig_h failed.", __func__ );
        return -EIO;
    }

    // succeeded
    return 0;
}

static int i2c_bme280_read_regs_data( struct i2c_client* client, const u8* regs, u8* dst, size_t count )
{
    s32 result;
    int i;

    for( i = 0; i < count; ++i ){
        result = i2c_smbus_read_byte_data( client, regs[i] );
        
        if( result < 0 ){
            pr_err( "%s i2c_smbus_read_byte_data() failed. reg=0x%02X, error=%d\n", __func__, regs[i], result );
            return -ENODEV;
        }

        dst[i] = result;
    }

    return 0;
}

static int __init i2c_bme280_init(void)
{
    pr_info( "i2c_bme280 device driver initialization.\n" );

    i2c_add_driver( &i2c_bme280_driver );
    return 0;
}

static void __exit i2c_bme280_exit(void)
{
    pr_info( "i2c_bme280 device driver exit.\n" );

    i2c_del_driver( &i2c_bme280_driver );
}

module_init(i2c_bme280_init);
module_exit(i2c_bme280_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR( "HogeHogei <matsuryo00@gmail.com>" );
MODULE_DESCRIPTION( "bme280 driver sample with i2c" );
