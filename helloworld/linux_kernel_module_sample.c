#include <linux/module.h>
#include <linux/init.h>

static int __init helloworld_init(void)
{
    pr_info( "Hello world initialization!\n" );
    return 0;
}

static void __exit helloworld_exit(void)
{
    pr_info( "Hello world exit\n" );
}

module_init(helloworld_init);
module_exit(helloworld_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR( "HogeHogei <matsuryo00@gmail.com>" );
MODULE_DESCRIPTION( "Linux kernel module skeleton" );
