#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <asm/current.h>
#include <asm/uaccess.h>


// 
// define constants
//
#define EEP_DEVICE_NAME "pseudo-eep-mem"
#define EEP_CLASS       "pseudo-eep-class"

// Minor number using this device driver
static const unsigned int MINOR_BASE = 0;
// Minor number counts using this device driver
static const unsigned int EEP_NBANK  = 1;

//
// declare static functions, structs
//
static int pseudo_eep_mem_open(struct inode *inode, struct file *file);
static int pseudo_eep_mem_close(struct inode *inode, struct file *file);
static ssize_t pseudo_eep_mem_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t pseudo_eep_mem_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

typedef struct
{
    u8* memory;
    u32 size;
} pseudo_eep_mem_area;

// 
// define static variables
//
static struct class *s_pseudo_eep_class = NULL;
static struct cdev  s_pseudo_eep_cdev;
static dev_t s_alloced_dev_region;

struct file_operations s_pseudo_eepmem_fops = {
    .open    = pseudo_eep_mem_open,
    .release = pseudo_eep_mem_close,
    .read    = pseudo_eep_mem_read,
    .write   = pseudo_eep_mem_write,
};

static pseudo_eep_mem_area s_pseudo_eepmem = { 
    NULL,               // Need dynamic allocation when load this module
    1024 * 8,           // 8KB
};

static size_t calculate_remain_count( size_t max_size, size_t count, loff_t pos )
{
    size_t remain_count = 0;

    if( max_size <= pos ){
        return 0;
    }
    if( (max_size <= count) || (pos > (max_size - count)) ){
        return max_size - pos;
    }

    return count;
}

// open時に呼ばれる関数
static int pseudo_eep_mem_open(struct inode *inode, struct file *file)
{
    pr_info( "%s", __func__ );
    return 0;
}

// close時に呼ばれる関数
static int pseudo_eep_mem_close(struct inode *inode, struct file *file)
{
    pr_info( "%s", __func__ );
    return 0;
}

// read時に呼ばれる関数
static ssize_t pseudo_eep_mem_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    pr_info( "%s", __func__ );

    size_t read_count = calculate_remain_count( s_pseudo_eepmem.size, count, *f_pos );
    if( read_count == 0 ){
        return 0;
    }

    u8* eepmem_read_start = s_pseudo_eepmem.memory + *f_pos;
    if( copy_to_user( buf, eepmem_read_start, read_count ) != 0 ){
        return -EIO;
    }

    *f_pos += read_count;

    return read_count;
}

// write時に呼ばれる関数
static ssize_t pseudo_eep_mem_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    pr_info( "%s", __func__ );

    size_t write_count = calculate_remain_count( s_pseudo_eepmem.size, count, *f_pos );
    if( write_count == 0 ){
        return 0;
    }

    u8* eepmem_write_start = s_pseudo_eepmem.memory + *f_pos;
    if( copy_from_user( eepmem_write_start, buf, write_count ) != 0 ){
        return -EIO;
    }

    *f_pos += write_count;

    return write_count;
}

static int __init pseudo_eep_mem_init(void)
{
    dev_t curr_dev;
    int result = 0;
    struct device *created_dev = NULL;
    pr_info( "pseudo eep mem device driver initialization.\n" );

    // 空いているメジャー番号を確保
    result = alloc_chrdev_region( &s_alloced_dev_region, MINOR_BASE, EEP_NBANK, EEP_DEVICE_NAME );
    if( result < 0 ){
        pr_err( "%s failed. alloc_chrdev_region = %d\n", __func__, result );
        goto REGION_ERR;
    }

    // デバイスクラス登録  /sys/class に見えるようになる
    s_pseudo_eep_class = class_create( THIS_MODULE, EEP_CLASS );
    if( IS_ERR(s_pseudo_eep_class) ){
        result = PTR_ERR(s_pseudo_eep_class);
        pr_err( "%s failed. class_create = %d\n", __func__, result );
        goto CREATE_CLASS_ERR;
    }

    // ファイル操作関数をバインド
    cdev_init( &s_pseudo_eep_cdev, &s_pseudo_eepmem_fops );
    s_pseudo_eep_cdev.owner = THIS_MODULE;
    // デバイス番号を生成
    curr_dev = MKDEV(MAJOR(s_alloced_dev_region), MINOR(s_alloced_dev_region));
    // このデバイスドライバをカーネルに登録する
    result = cdev_add(&s_pseudo_eep_cdev, curr_dev, 1 );
    if( result != 0 ){
        pr_err( "%s failed. cdev_add = %d\n", __func__, result );
        goto CDEV_ADD_ERR;
    }

    // デバイスノードを作成。作成したノードは/dev以下からアクセス可能
    created_dev = device_create( 
            s_pseudo_eep_class,
            NULL,               // no parent device
            curr_dev,
            NULL,               // no additional data
            EEP_DEVICE_NAME "%d",
            MINOR_BASE );       // pseudo-eep-mem0

    if( IS_ERR(created_dev) ){
        result = PTR_ERR(created_dev);
        pr_err( "%s failed. device_create = %d\n", __func__, result );
        goto DEV_CREATE_ERR;
    }

    // ニセeeprom用メモリ領域確保
    s_pseudo_eepmem.memory = kzalloc( s_pseudo_eepmem.size, GFP_KERNEL );
    if( !s_pseudo_eepmem.memory ){
        pr_err( "%s failed. pseudo_eep_memory area allocation.", __func__ );
        goto PSEUDO_EEP_MEM_ALLOC_ERR;
    }

    pr_info( "%s succeeded", __func__ );

    // initialize succeeded
    return 0;

    // error bailout
PSEUDO_EEP_MEM_ALLOC_ERR:
DEV_CREATE_ERR:
    cdev_del( &s_pseudo_eep_cdev );
CDEV_ADD_ERR:
    class_destroy( s_pseudo_eep_class );
CREATE_CLASS_ERR:
    unregister_chrdev_region( s_alloced_dev_region, EEP_NBANK );
REGION_ERR:
    return -1;
}

static void __exit pseudo_eep_mem_exit(void)
{
    pr_info( "pseudo eep mem device driver exit.\n" );

    dev_t dev = MKDEV(MAJOR(s_alloced_dev_region), MINOR(s_alloced_dev_region));

    // デバイスノード削除
    device_destroy( s_pseudo_eep_class, dev );
    // キャラクターデバイスをKernelから削除
    cdev_del( &s_pseudo_eep_cdev );
    // デバイスのクラス登録を削除
    class_destroy( s_pseudo_eep_class );
    // デバイスが使用していたメジャー番号の登録削除
    unregister_chrdev_region( dev, EEP_NBANK );
    // ニセeepmem領域開放
    kfree( s_pseudo_eepmem.memory );
}

module_init(pseudo_eep_mem_init);
module_exit(pseudo_eep_mem_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR( "HogeHogei <matsuryo00@gmail.com>" );
MODULE_DESCRIPTION( "Pseudo eep device driver for how to make character linux device driver." );
