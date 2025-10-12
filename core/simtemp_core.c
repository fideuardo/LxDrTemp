
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


/*OWn includes */
#include "simtemp.h"
#include "uapi/simtemp_uapi.h"

/*Header Functions*/
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static unsigned int simtemp_poll(struct file *file, poll_table *wait);


/*Global Variables*/
/* default ring buffer size (samples) */
#define SIMTEMP_DEFAULT_RING_SIZE 16

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

/* --- Sysfs implementation --- */

/*
 * show/store para 'state'
 * Allos read and write the driver state
 */
static ssize_t state_show(struct device *stdevice, struct device_attribute *attr, char *buffer)
{
    struct simtemp_dev *driver_data = dev_get_drvdata(stdevice);
    return sysfs_emit(buffer, "%d\n", driver_data->state);
}
static ssize_t state_store(struct device *stdevice, struct device_attribute *attr, const char *buffer, size_t count)
{
    struct simtemp_dev *driver_data = dev_get_drvdata(stdevice);
    /* User stop service*/
    if(sysfs_streq(buffer, "STOP"))
    {
        driver_data->state = SIMTEMP_enSTATE_STOP;
        return count;
    }
    /*User Run Service */
    else if(sysfs_streq(buffer, "RUN"))
    {
        driver_data->state = SIMTEMP_enSTATE_RUN;
        return count;
    }
    /*Rest of options are restricted*/
    else
    {
        return -EINVAL;
    }
}
static DEVICE_ATTR_RW(state);


/*
 * show/store para 'sampling_ms'
 * Permite leer/escribir el período de muestreo en milisegundos.
 * La escritura solo se permite si el temporizador está detenido.
 */
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_dev *sd = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", sd->u32Period_ms);
}

static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct simtemp_dev *sd = dev_get_drvdata(dev);
	u32 new_period;
	int ret;

	ret = kstrtou32(buf, 10, &new_period);
	if (ret)
		return ret;

	if (new_period == 0)
		return -EINVAL;

	/* UC-03: Rechazar cambio de período si está en ejecución */
	if (sd->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY;

	sd->u32Period_ms = new_period;
	return count;
}
static DEVICE_ATTR_RW(sampling_ms);

/*
 * show/store para 'operation_mode'
 * Permite leer/escribir el modo de operación: "continuous" o "one-shot".
 * La escritura solo se permite si el temporizador está detenido.
 */
static ssize_t operation_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_dev *sd = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%s\n", sd->mode == SIMTEMP_MODE_CONTINUOUS ? "continuous" : "one-shot");
}

static ssize_t operation_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct simtemp_dev *sd = dev_get_drvdata(dev);

	/* Rechazar cambio de modo si está en ejecución */
	if (sd->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY;

	if (sysfs_streq(buf, "continuous"))
		sd->mode = SIMTEMP_MODE_CONTINUOUS;
	else if (sysfs_streq(buf, "one-shot"))
		sd->mode = SIMTEMP_MODE_ONESHOT;
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(operation_mode);


/* Grupo de atributos para ser creados/eliminados juntos */
static struct attribute *simtemp_attrs[] = {
    &dev_attr_state.attr,
	&dev_attr_sampling_ms.attr,
	&dev_attr_operation_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(simtemp);


/************* Functions **************** */

static enum hrtimer_restart simtemp_timer_cb(struct hrtimer *timer)
{
    struct simtemp_dev *dev = container_of(timer, struct simtemp_dev, timer);
    struct simtemp_sample_v1 sample;
    unsigned long flags;

    /* simple deterministic temperature model: base 25000 mC + (seq % 100) */
    spin_lock_irqsave(&dev->sample_lock, flags);
    sample.TimeStamp_ns = ktime_get_ns();
    sample.Temperature_mC = 25000 + (dev->u64SequenceNumber % 100);
    sample.StatusFlags = SIMTEMP_FLAG_OK;
    dev->u64SequenceNumber++;
    dev->stsample = sample;

    /* Si es one-shot, marca la muestra y no reprogrames el timer */
    if (dev->mode == SIMTEMP_MODE_ONESHOT) {
        dev->stsample.StatusFlags |= SIMTEMP_FLAG_ONESHOT_DONE;
        dev->state = SIMTEMP_enSTATE_STOP; /* Transición de estado */
        spin_unlock_irqrestore(&dev->sample_lock, flags);
        wake_up_interruptible(&dev->read_wait);
        return HRTIMER_NORESTART;
    }

    spin_unlock_irqrestore(&dev->sample_lock, flags);

    /* wake readers */
    wake_up_interruptible(&dev->read_wait);

    /* re-arm timer for continuous mode */
    hrtimer_forward_now(timer, ktime_set(0, (s64)dev->u32Period_ms * 1000000LL));
    return HRTIMER_RESTART;
}

static int __init simtemp_module_init(void)
{
    int ret;
    
    memset(&simtemp_DeviceContext, 0, sizeof(simtemp_DeviceContext));
    
    /* Setting initial state*/
    simtemp_DeviceContext.state = SIMTEMP_enSTATE_STOP; /* Estado inicial: Detenido */
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

    /* set device node permissions user mode is ever enable*/
    simtemp_class->devnode = simtemp_devnode;

    /* Guardamos el puntero al dispositivo para usarlo en sysfs */
    simtemp_DeviceContext.dev = device_create_with_groups(simtemp_class, NULL, simtemp_dev_number,
							  &simtemp_DeviceContext, simtemp_groups, "simtemp");
    if (IS_ERR(simtemp_DeviceContext.dev)) {
        ret = -EINVAL;
        goto err_class_destroy;
    }

    /* init wait queue (ringbuffer integration on standby) */
    init_waitqueue_head(&simtemp_DeviceContext.read_wait);

    /* init hrtimer */
    hrtimer_init(&simtemp_DeviceContext.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    simtemp_DeviceContext.timer.function = simtemp_timer_cb;

    return 0;

err_class_destroy:
    /* device_destroy se llama en el exit, no es necesario aquí si falla create */
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
    device_destroy(simtemp_class, simtemp_DeviceContext.dev->devt);
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
    struct simtemp_sample_v1 sample;
    unsigned long flags;

    /* Exit if not exist data into the buffer*/
    if(*ppos != 0)
    {
        return 0;
    }
    
    /*Buffer size validation*/
    if(count < sizeof(sample))
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
    return sizeof(struct simtemp_sample_v1);
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
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY; /* Ya está corriendo */
        /* start timer */
        kt = ktime_set(0, (s64)dev->u32Period_ms * 1000000LL);
        hrtimer_start(&dev->timer, kt, HRTIMER_MODE_REL);
        dev->state = SIMTEMP_enSTATE_RUN;
        return 0;
    case SIMTEMP_IOC_STOP:
        if (dev->state == SIMTEMP_enSTATE_STOP)
            return 0; /* Ya está detenido, no es un error */
        hrtimer_cancel(&dev->timer);
        dev->state = SIMTEMP_enSTATE_STOP;
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
        /* Solo se puede cambiar el modo si está detenido */
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY;
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

        /* UC-03: Rechazar cambio de período si está en ejecución */
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY;

        dev->u32Period_ms = tmp;
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
