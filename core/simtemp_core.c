
/* Kernel includes */
#include <linux/miscdevice.h>
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
#include <linux/platform_device.h>
#include <linux/of.h> /* Needed for of_device_id */


/* Own includes */
#include "nxp_simtemp.h"
#include <uapi/simtemp_uapi.h>

/* Function Prototypes */
/* Platform driver functions */
static int nxp_simtemp_probe(struct platform_device *pdev);
static void nxp_simtemp_remove(struct platform_device *pdev);

/* File operations functions */
static int nxp_simtemp_open(struct inode *inode, struct file *file);
static int nxp_simtemp_release(struct inode *inode, struct file *file);
static ssize_t nxp_simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static __poll_t nxp_simtemp_poll(struct file *file, poll_table *wait);


/* Global Variables */
/* default ring buffer size (samples) */
#define SIMTEMP_DEFAULT_RING_SIZE 16

static long nxp_simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations nxp_simtemp_fops =
{
    /* data */
    .owner   = THIS_MODULE,
    .open    = nxp_simtemp_open,
    .release = nxp_simtemp_release,
    .read    = nxp_simtemp_read,
    .poll    = nxp_simtemp_poll,
    .llseek  = noop_llseek,
    .unlocked_ioctl = nxp_simtemp_ioctl,
};

static struct miscdevice nxp_simtemp_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "nxp_simtemp",
    .fops = &nxp_simtemp_fops,
    .mode = 0666,
};

/* --- State control helper functions --- */

static int nxp_simtemp_start_sampler(struct nxp_simtemp_dev *dev)
{
	ktime_t kt;

	if (dev->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY; /* Already running */

	/* Start the timer */
	kt = ktime_set(0, (s64)dev->u32Period_ms * 1000000LL);
	hrtimer_start(&dev->timer, kt, HRTIMER_MODE_REL);
	dev->state = SIMTEMP_enSTATE_RUN;

	return 0;
}

static int nxp_simtemp_stop_sampler(struct nxp_simtemp_dev *dev)
{
	if (dev->state == SIMTEMP_enSTATE_STOP)
		return 0; /* Already stopped, not an error */

	hrtimer_cancel(&dev->timer);
	dev->state = SIMTEMP_enSTATE_STOP;
	return 0;
}

/* --- Sysfs implementation --- */

/*
 * show/store para 'state'
 * Allows reading and writing the driver state
 */
static ssize_t state_show(struct device *stdevice, struct device_attribute *attr, char *buffer)
{
    struct nxp_simtemp_dev *driver_data = dev_get_drvdata(stdevice);
    return sysfs_emit(buffer, "%d\n", driver_data->state);
}


static ssize_t state_store(struct device *stdevice, struct device_attribute *attr, const char *buffer, size_t count)
{
    struct nxp_simtemp_dev *dev = dev_get_drvdata(stdevice);
    int ret;

    if (sysfs_streq(buffer, "STOP")) {
        ret = nxp_simtemp_stop_sampler(dev);
    } else if (sysfs_streq(buffer, "RUN")) {
        ret = nxp_simtemp_start_sampler(dev);
    } else {
        ret = -EINVAL;
    }
    return ret ? ret : count;
}
static DEVICE_ATTR_RW(state);


/*
 * show/store para 'sampling_ms'
 * Allows reading/writing the sampling period in milliseconds.
 * Writing is only allowed if the timer is stopped.
 */
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", sd->u32Period_ms);
}

static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);
	u32 new_period;
	int ret;

	ret = kstrtou32(buf, 10, &new_period);
	if (ret)
		return ret;

	if (new_period == 0)
		return -EINVAL;

	/* UC-03: Reject period change if running */
	if (sd->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY;

	sd->u32Period_ms = new_period;
	return count;
}
static DEVICE_ATTR_RW(sampling_ms);

/*
 * show/store para 'mode' (normal, noisy, ramp)
 * Aligned with MSD v0.3
 */
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);
	const char *mode_str;

	if (sd->sim_mode == SIMTEMP_SIM_NOISY)
		mode_str = "noisy";
	else if (sd->sim_mode == SIMTEMP_SIM_RAMP)
		mode_str = "ramp";
	else
		mode_str = "normal";

	return sysfs_emit(buf, "%s\n", mode_str);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);

	/* Reject mode change if running */
	if (sd->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY;

	if (sysfs_streq(buf, "normal"))
		sd->sim_mode = SIMTEMP_SIM_NORMAL;
	else if (sysfs_streq(buf, "noisy"))
		sd->sim_mode = SIMTEMP_SIM_NOISY;
	else if (sysfs_streq(buf, "ramp"))
		sd->sim_mode = SIMTEMP_SIM_RAMP;
	else
		return -EINVAL;

	return count;
}
static DEVICE_ATTR_RW(mode);

/*
 * show/store para 'operation_mode'
 * Allows reading/writing the operation mode: "continuous" or "one-shot".
 * Writing is only allowed if the timer is stopped.
 */
static ssize_t operation_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%s\n", sd->mode == SIMTEMP_MODE_CONTINUOUS ? "continuous" : "one-shot");
}

static ssize_t operation_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);

	/* Reject mode change if running */
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

/*
 * show para 'stats' (Read-Only)
 * Aligned with MSD v0.3
 */
static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nxp_simtemp_dev *sd = dev_get_drvdata(dev);
	return sysfs_emit(buf, "samples=%llu overruns=%u alerts=%u\n",
			  sd->u64SequenceNumber,
			  sd->stRingBuff.u32OverRuns,
			  sd->u32Alerts);
}
static DEVICE_ATTR_RO(stats);

/* Attribute group to be created/removed together */
static struct attribute *nxp_simtemp_attrs[] = {
    &dev_attr_state.attr,
	&dev_attr_sampling_ms.attr,
	&dev_attr_operation_mode.attr, /* Legacy, should be reviewed */
	&dev_attr_mode.attr,
	&dev_attr_stats.attr,
	NULL,
};
ATTRIBUTE_GROUPS(nxp_simtemp);


/* --- Core Functions --- */

static enum hrtimer_restart nxp_simtemp_timer_cb(struct hrtimer *timer)
{
    struct nxp_simtemp_dev *dev = container_of(timer, struct nxp_simtemp_dev, timer);
    struct simtemp_sample_v1 sample;

    /* simple deterministic temperature model: base 25000 mC + (seq % 100) */
    /* MSD v0.3: Implement normal, noisy, ramp modes */
    sample.timestamp_ns = ktime_get_ns();

    switch (dev->sim_mode) {
    case SIMTEMP_SIM_NOISY:
        /* Placeholder: 25°C +/- 2°C random noise */
        sample.temp_mC = 25000 + (get_random_u32() % 4000) - 2000;
        break;
    case SIMTEMP_SIM_RAMP:
        sample.temp_mC = 20000 + ((dev->u64SequenceNumber * 50) % 20000); /* Ramp 20-40°C */
        break;
    case SIMTEMP_SIM_NORMAL:
    default:
        sample.temp_mC = 25000 + (get_random_u32() % 200) - 100; /* 25°C +/- 0.1°C */
        break;
    }
    
    sample.flags = SIMTEMP_FLAG_OK;
    dev->u64SequenceNumber++;

    /* If one-shot, flag the sample and do not re-arm the timer */
    if (dev->mode == SIMTEMP_MODE_ONESHOT) {
        sample.flags |= SIMTEMP_FLAG_ONESHOT_DONE;
        dev->state = SIMTEMP_enSTATE_STOP; /* State transition */
        nxp_simtemp_ringbuffer_write(&dev->stRingBuff, &sample);
        wake_up_interruptible(&dev->read_wait);
        return HRTIMER_NORESTART;
    }

    /* Write to the ring buffer. The lock is inside the write function. */
    nxp_simtemp_ringbuffer_write(&dev->stRingBuff, &sample);

    /* wake readers */
    wake_up_interruptible(&dev->read_wait);

    /* Re-arm timer for continuous mode */
    hrtimer_forward_now(timer, ktime_set(0, (s64)dev->u32Period_ms * 1000000LL));
    return HRTIMER_RESTART;
}

/*
 * The open function now gets the driver context via the miscdevice,
 * which in turn got it from the platform_device during probe.
 */

static int nxp_simtemp_open(struct inode *inode, struct file *file)
{
	/* Obtenemos el contexto del driver desde el dispositivo misc,
	 * donde fue almacenado durante el probe. */
	struct nxp_simtemp_dev *sdev = dev_get_drvdata(nxp_simtemp_miscdev.this_device);
	file->private_data = sdev;
    return 0;
}

static int nxp_simtemp_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t nxp_simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct nxp_simtemp_dev *device = file->private_data;
    struct simtemp_sample_v1 sample;
	size_t bytes_read = 0;
    u32 samples_to_read, samples_read;
    int ret;

    if ((file->f_flags & O_NONBLOCK) && nxp_simtemp_ringbuffer_empty(&device->stRingBuff))
        return -EAGAIN;

    /* Blocking wait until data is available in the ring buffer */
    ret = wait_event_interruptible(device->read_wait, !nxp_simtemp_ringbuffer_empty(&device->stRingBuff));
    if (ret)
        return ret; /* Interrupted by a signal */

    /* Calculate how many samples fit in the user buffer */
    samples_to_read = count / sizeof(struct simtemp_sample_v1);
    if (samples_to_read == 0)
        return -EINVAL; /* User buffer is too small for even one sample */

    /*
     * To avoid large kernel allocations, read one sample at a time
     * and copy it to the user. This is safer.
     */
    while (bytes_read + sizeof(struct simtemp_sample_v1) <= count) {
        samples_read = nxp_simtemp_ringbuffer_read(&device->stRingBuff, &sample, 1);
        if (samples_read == 0)
            break; /* No more data */

        if (copy_to_user(buf + bytes_read, &sample, sizeof(sample)))
            return -EFAULT;

        bytes_read += sizeof(sample);
    }
    return bytes_read;
}

static __poll_t nxp_simtemp_poll(struct file *file, poll_table *wait)
{
    struct nxp_simtemp_dev *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->read_wait, wait);

    if (!nxp_simtemp_ringbuffer_empty(&dev->stRingBuff))
        mask |= POLLIN | POLLRDNORM; /* Data is available for reading */

    /* If in one-shot mode and the sampler has stopped, it means the single
     * sample was produced and the "stream" is finished. */
    if (dev->mode == SIMTEMP_MODE_ONESHOT && dev->state == SIMTEMP_enSTATE_STOP &&
        nxp_simtemp_ringbuffer_empty(&dev->stRingBuff))
        mask |= POLLHUP;

    return mask;
}

static long nxp_simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct nxp_simtemp_dev *dev = file->private_data;
    u32 tmp;    

    switch (cmd) {
    case SIMTEMP_IOC_START:
        return nxp_simtemp_start_sampler(dev);

    case SIMTEMP_IOC_STOP:
        return nxp_simtemp_stop_sampler(dev);

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
        /* Mode can only be changed when stopped (policy-level lock) */
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY;
        dev->mode = (enum nxp_simtemp_mode)tmp;
        return 0;
    case SIMTEMP_IOC_GET_PERIOD:
        tmp = dev->u32Period_ms;
        if (copy_to_user((void __user *)arg, &tmp, sizeof(tmp)))
            return -EFAULT;
        return 0;
    case SIMTEMP_IOC_SET_PERIOD:
        if (copy_from_user(&tmp, (void __user *)arg, sizeof(tmp)))
            return -EFAULT;
        /* Period cannot be 0 */
        if (tmp == 0)
            return -EINVAL;

        /* UC-03: Reject period change if running */
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY;

        dev->u32Period_ms = tmp;
        return 0;
    default:
        return -ENOTTY;
    }
}

/* --- Platform Driver Implementation --- */

static const struct of_device_id nxp_simtemp_of_match[] = {
	{ .compatible = "nxp,simtemp" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nxp_simtemp_of_match);

static int nxp_simtemp_probe(struct platform_device *pdev)
{
	struct nxp_simtemp_dev *sdev;
	int ret;

	/* 1. Allocate memory for the device context */
	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	/* 2. Initialize the device context */
	sdev->state = SIMTEMP_enSTATE_STOP;
	sdev->mode = SIMTEMP_MODE_CONTINUOUS; /* Default, will be overwritten by DT */
	sdev->u32Period_ms = 1000;           /* Default, will be overwritten by DT */
	sdev->u64SequenceNumber = 0;
	sdev->dev = &pdev->dev;

	/* 3. Parse the Device Tree */
	nxp_simtemp_of_parse(&pdev->dev, sdev);

	/* 4. Initialize driver components */
	ret = nxp_simtemp_rb_init(sdev, SIMTEMP_DEFAULT_RING_SIZE, true);
	if (ret)
		return ret;

	init_waitqueue_head(&sdev->read_wait);
	hrtimer_init(&sdev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sdev->timer.function = nxp_simtemp_timer_cb;

	/* 5. Register the miscdevice */
	ret = misc_register(&nxp_simtemp_miscdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register misc device\n");
		return ret;
	}
	/*
	 * Associate the driver context (sdev) with the 'struct device'
	 * that the miscdevice has just created. This is crucial so that
	 * sysfs and open functions can find the context.
	 */
	dev_set_drvdata(nxp_simtemp_miscdev.this_device, sdev);

	/* 6. Create the sysfs files */
	ret = sysfs_create_groups(&nxp_simtemp_miscdev.this_device->kobj, nxp_simtemp_groups);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs files\n");
		misc_deregister(&nxp_simtemp_miscdev);
		return ret;
	}

	/* 7. Save the context in the platform_device for use in remove() */
	platform_set_drvdata(pdev, sdev);

	pr_info("nxp_simtemp: loaded (miscdevice: /dev/nxp_simtemp)\n");
	return 0;
}

static void nxp_simtemp_remove(struct platform_device *pdev)
{
	struct nxp_simtemp_dev *sdev = platform_get_drvdata(pdev);

	/* Cleanup in reverse order of probe */
	hrtimer_cancel(&sdev->timer);
	sysfs_remove_groups(&nxp_simtemp_miscdev.this_device->kobj, nxp_simtemp_groups);
	misc_deregister(&nxp_simtemp_miscdev);

	pr_info("nxp_simtemp: unloaded\n");
}

static struct platform_driver nxp_simtemp_platform_driver = {
	.driver = {
		.name = "nxp_simtemp",
		.of_match_table = nxp_simtemp_of_match,
	},
	.probe = nxp_simtemp_probe,
	.remove = nxp_simtemp_remove,
};
module_platform_driver(nxp_simtemp_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fidel Eduardo Cabañas Castillo");
MODULE_DESCRIPTION("simtemp minimal one-shot miscdevice");
MODULE_VERSION("0.1");
