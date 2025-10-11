
/*kernel includes*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/poll.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>


/*OWn includes */
#include "simtemp.h"

/*Header Functions*/
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static unsigned int simtemp_poll(struct file *file, poll_table *wait);


/*Global Variables*/
/* default ring buffer size (samples) */
#define SIMTEMP_DEFAULT_RING_SIZE 16

static const struct file_operations simtemp_fops =
{
    /* data */
    .owner   = THIS_MODULE,
    .open    = simtemp_open,
    .release = simtemp_release,
    .read    = simtemp_read,
    .poll    = simtemp_poll,
    .llseek  = noop_llseek,
};

/* Character device state */
static dev_t simtemp_dev_number;
static struct class *simtemp_class;
static struct cdev simtemp_cdev;

struct simtemp_dev simtemp_DeviceContext; /*Device  Context*/



/************* Functions **************** */

static int __init simtemp_module_init(void)
{
    int intRegisterResult = 0;
    int ret;
    
    memset(&simtemp_DeviceContext, 0, sizeof(simtemp_DeviceContext));
    
    /* Setting initial state*/
    simtemp_DeviceContext.mode = SIMTEMP_MODE_ONESHOT; /*Default Mode: ONESHOT*/

    /* allocate a device number */
    ret = alloc_chrdev_region(&simtemp_dev_number, 0, 1, "simtemp");
    if (ret)
        return ret;

    /* init cdev */
    cdev_init(&simtemp_cdev, &simtemp_fops);
    simtemp_cdev.owner = THIS_MODULE;
    ret = cdev_add(&simtemp_cdev, simtemp_dev_number, 1);
    if (ret)
        goto err_unregister_chrdev;

    /* create class/device to get /dev node */
    simtemp_class = class_create("simtemp");
    if (IS_ERR(simtemp_class)) {
        ret = PTR_ERR(simtemp_class);
        goto err_cdev_del;
    }

    if (IS_ERR(device_create(simtemp_class, NULL, simtemp_dev_number, NULL, "simtemp"))) {
        ret = -EINVAL;
        goto err_class_destroy;
    }

    /* init wait queue (ringbuffer integration on standby) */
    init_waitqueue_head(&simtemp_DeviceContext.read_wait);

    return 0;

err_device_destroy:
    device_destroy(simtemp_class, simtemp_dev_number);
err_class_destroy:
    class_destroy(simtemp_class);
err_cdev_del:
    cdev_del(&simtemp_cdev);
err_unregister_chrdev:
    unregister_chrdev_region(simtemp_dev_number, 1);
    return ret;
}

static void __exit simtemp_module_exit(void)
{
    device_destroy(simtemp_class, simtemp_dev_number);
    class_destroy(simtemp_class);
    cdev_del(&simtemp_cdev);
    unregister_chrdev_region(simtemp_dev_number, 1);
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

    /* store sample in device context (ringbuffer on standby) */
    device->stsample = sample;

    if (copy_to_user(buf, &sample, sizeof(sample))) {
        return -EFAULT;
    }

    *ppos = sizeof(sample);
    return sizeof(sample);
}

static unsigned int simtemp_poll(struct file *file, poll_table *wait)
{
    /* Ringbuffer integration is on standby: poll() currently returns 0 */
    return 0;
}


module_init(simtemp_module_init);
module_exit(simtemp_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fidel Eduardo Cabañas Castillo");
MODULE_DESCRIPTION("simtemp minimal one-shot miscdevice");
MODULE_VERSION("0.1");

