
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
#include <linux/hrtimer.h>
#include <linux/ioctl.h>


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

/* IOCTL definitions */
#define SIMTEMP_IOC_MAGIC 'T'
#define SIMTEMP_IOC_START          _IO(SIMTEMP_IOC_MAGIC,  0x01)
#define SIMTEMP_IOC_STOP           _IO(SIMTEMP_IOC_MAGIC,  0x02)
#define SIMTEMP_IOC_GET_MODE       _IOR(SIMTEMP_IOC_MAGIC, 0x10, __u32)
#define SIMTEMP_IOC_SET_MODE       _IOW(SIMTEMP_IOC_MAGIC, 0x11, __u32)
#define SIMTEMP_IOC_GET_PERIOD     _IOR(SIMTEMP_IOC_MAGIC, 0x20, __u32)
#define SIMTEMP_IOC_SET_PERIOD     _IOW(SIMTEMP_IOC_MAGIC, 0x21, __u32)

static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations simtemp_fops =
{
    /* data */
    .owner   = THIS_MODULE,
    .open    = simtemp_open,
    .release = simtemp_release,
    .read    = simtemp_read,
    .poll    = simtemp_poll,
    .llseek  = noop_llseek,
    .unlocked_ioctl = simtemp_ioctl,
};

/* Character device state */
static dev_t simtemp_dev_number;
static struct class *simtemp_class;
static struct cdev simtemp_cdev;

/* devnode callback to set device node mode to 0666 */
static char *simtemp_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

struct simtemp_dev simtemp_DeviceContext; /*Device  Context*/



/************* Functions **************** */

static enum hrtimer_restart simtemp_timer_cb(struct hrtimer *timer)
{
    struct simtemp_dev *dev = container_of(timer, struct simtemp_dev, timer);
    ktime_t now = ktime_get();
    struct stsimptemp_sample_v1 sample;
    unsigned long flags;
    u64 seq;

    /* simple deterministic temperature model: base 25000 mC + (seq % 100) */
    spin_lock_irqsave(&dev->sample_lock, flags);
    seq = dev->u64SequenceNumber++;
    sample.u64TimeStamp_ns = ktime_to_ns(now);
    sample.u64SequenceNumber = seq;
    sample.u32temperature_mC = 25000 + (seq % 100);
    sample.u32StatusFlags = 0;
    dev->stsample = sample;
    spin_unlock_irqrestore(&dev->sample_lock, flags);

    /* wake readers */
    wake_up_interruptible(&dev->read_wait);

    /* re-arm timer */
    hrtimer_forward_now(timer, ktime_set(0, (s64)dev->u32Period_ms * 1000000LL));
    return HRTIMER_RESTART;
}

static int __init simtemp_module_init(void)
{
    int ret;
    
    memset(&simtemp_DeviceContext, 0, sizeof(simtemp_DeviceContext));
    
    /* Setting initial state*/
    simtemp_DeviceContext.mode = SIMTEMP_MODE_CONTINUOUS; /*Default Mode: CONTINUOUS*/
    simtemp_DeviceContext.u32Period_ms = 1000; /* default 1000 ms */
    simtemp_DeviceContext.u64SequenceNumber = 0;
    spin_lock_init(&simtemp_DeviceContext.sample_lock);

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

    /* set device node permissions */
#ifdef CONFIG_SYSFS
    simtemp_class->devnode = simtemp_devnode;
#endif

    if (IS_ERR(device_create(simtemp_class, NULL, simtemp_dev_number, NULL, "simtemp"))) {
        ret = -EINVAL;
        goto err_class_destroy;
    }

    /* init wait queue (ringbuffer integration on standby) */
    init_waitqueue_head(&simtemp_DeviceContext.read_wait);

    /* init hrtimer */
    hrtimer_init(&simtemp_DeviceContext.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    simtemp_DeviceContext.timer.function = simtemp_timer_cb;

    /* start timer if in continuous mode */
    if (simtemp_DeviceContext.mode == SIMTEMP_MODE_CONTINUOUS) {
        ktime_t kt = ktime_set(0, (s64)simtemp_DeviceContext.u32Period_ms * 1000000LL);
        hrtimer_start(&simtemp_DeviceContext.timer, kt, HRTIMER_MODE_REL);
    }

    return 0;

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
    /* stop timer */
    hrtimer_cancel(&simtemp_DeviceContext.timer);
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
    unsigned long flags;

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

    /* Copy the last sample produced by the timer */
    spin_lock_irqsave(&device->sample_lock, flags);
    sample = device->stsample;
    spin_unlock_irqrestore(&device->sample_lock, flags);

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

static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simtemp_dev *dev = file->private_data;
    u32 tmp;
    ktime_t kt;

    switch (cmd) {
    case SIMTEMP_IOC_START:
        /* start timer */
        kt = ktime_set(0, (s64)dev->u32Period_ms * 1000000LL);
        hrtimer_start(&dev->timer, kt, HRTIMER_MODE_REL);
        return 0;
    case SIMTEMP_IOC_STOP:
        hrtimer_cancel(&dev->timer);
        return 0;
    case SIMTEMP_IOC_GET_MODE:
        tmp = (u32)dev->mode;
        if (copy_to_user((void __user *)arg, &tmp, sizeof(tmp)))
            return -EFAULT;
        return 0;
    case SIMTEMP_IOC_SET_MODE:
        if (copy_from_user(&tmp, (void __user *)arg, sizeof(tmp)))
            return -EFAULT;
        if (tmp > SIMTEMP_MODE_CONTINUOUS)
            return -EINVAL;
        dev->mode = (enum ensimtemp_mode)tmp;
        return 0;
    case SIMTEMP_IOC_GET_PERIOD:
        tmp = dev->u32Period_ms;
        if (copy_to_user((void __user *)arg, &tmp, sizeof(tmp)))
            return -EFAULT;
        return 0;
    case SIMTEMP_IOC_SET_PERIOD:
        if (copy_from_user(&tmp, (void __user *)arg, sizeof(tmp)))
            return -EFAULT;
        /* El período no puede ser 0 */
        if (tmp == 0)
            return -EINVAL;

        /*
         * Para aplicar el cambio de período de forma segura y atómica:
         * 1. Intentamos cancelar el temporizador. hrtimer_try_to_cancel devuelve
         *    1 si el temporizador estaba activo, 0 si no.
         * 2. Actualizamos el valor del período.
         * 3. Si el temporizador estaba activo, lo reiniciamos inmediatamente
         *    con el nuevo período.
         */
        dev->u32Period_ms = tmp;
        if (hrtimer_try_to_cancel(&dev->timer) == 1) {
            kt = ktime_set(0, (s64)dev->u32Period_ms * 1000000LL);
            hrtimer_start(&dev->timer, kt, HRTIMER_MODE_REL);
        }
        return 0;
    default:
        return -ENOTTY;
    }
}


module_init(simtemp_module_init);
module_exit(simtemp_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fidel Eduardo Cabañas Castillo");
MODULE_DESCRIPTION("simtemp minimal one-shot miscdevice");
MODULE_VERSION("0.1");
