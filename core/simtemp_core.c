
/*kernel includes*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/poll.h>
#include <linux/err.h>


/*OWn includes */
#include "simtemp.h"

/*Global Variables*/
static const struct file_operations simtemp_fops =
{
    /* data */
    .owner   = THIS_MODULE,
    .open    = simtemp_open,
    .release = simtemp_release,
    .read    = simtemp_read,
    .poll    = simtemp_poll,
    .llseek  = no_llseek,
};

struct simtemp_dev simtemp_DeviceContext; /*Device  Context*/



/************* Functions **************** */

static init __init simtemp_module_init(void)
{
    int state;
    memset(&simtemp_dev, 0, sizeof(simtemp_dev));
    
    /* Setting initial state*/
    simtemp_DeviceContext.miscdev.name = "simtemp";
    simtemp_DeviceContext.miscdev.minor = MISC_DYNAMIC_MINOR;
    simtemp_DeviceContext.mode = SIMTEMP_MODE_ONESHOT;
}

static void __exit simtemp_module_exit(void)
{
    misc_deregister(&simtemp_DeviceContext.miscdev);
}

static int simtemp_open(struct inode *inode, struct file *file)
{
    file->private_data = &simtemp_DeviceContext;
    return 0;
}

static int simtemp_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct simtemp_dev *device = file->private_data;
    struct stsimptemp_sample_v1 sample;

    /* Exit if not exist data into the buffer*/
    if(*ppos != 0)
    {
        return 0;
    }
    
    /*Buffer size validation*/
    if(count < sizeof(struct stsimptemp_sample_v1))
    {
        return -EINVAL;
    }

    /*Getting information*/
    sample.u64TimeStamp_ns = ktime_get_ns();
    sample.u64SequenceNumber = 0;
    sample.u32temperature_mC = 25000;
    sample.u32StatusFlags = SIMTEMP_FLAG_ONESHOT_DONE;

    /* data to validation */
    device->stsample = sample;
    
    if(copy_to_user(buf, &sample, sizeof(sample)))
    {
        return -EFAULT;
    }

    *ppos = sizeof(sample);
    return sizeof(sample);
}

static unsigned int simtemp_poll(struct file *file, poll_table *wait)
{
    return 0;
}


module_init(simtemp_module_init);
module_exit(simtemp_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fidel Eduardo Cabañas Castillo");
MODULE_DESCRIPTION("simtemp minimal one-shot miscdevice");
MODULE_VERSION("0.1");

