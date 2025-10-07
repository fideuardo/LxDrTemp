#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

int simtemp_core_init(void)
{
    printk(KERN_INFO "simtemp driver\n");
    printk(KERN_INFO "driver montado\n");

    return 0;
}

void simtemp_core_exit(void)
{
    printk(KERN_INFO "simtemp driver\n");
    printk(KERN_INFO "driver desmontado\n");
}

module_init(simtemp_core_init);
module_exit(simtemp_core_exit);

MODULE_LICENSE("GPL");

