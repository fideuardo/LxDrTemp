
/*kernel includes*/
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


/*OWn includes */
#include "simtemp.h"
#include <uapi/simtemp_uapi.h>

/*Header Functions*/
/* Funciones de plataforma */
static int simtemp_probe(struct platform_device *pdev);
static int simtemp_remove(struct platform_device *pdev);

/* Funciones de fops */
static int simtemp_open(struct inode *inode, struct file *file);
static int simtemp_release(struct inode *inode, struct file *file);
static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static __poll_t simtemp_poll(struct file *file, poll_table *wait);


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

static struct miscdevice simtemp_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "simtemp",
    .fops = &simtemp_fops,
    .mode = 0666,
};

/* --- Funciones auxiliares para control de estado --- */

static int simtemp_start_sampler(struct simtemp_dev *dev)
{
	ktime_t kt;

	if (dev->state == SIMTEMP_enSTATE_RUN)
		return -EBUSY; /* Ya está corriendo */

	/* Inicia el timer */
	kt = ktime_set(0, (s64)dev->u32Period_ms * 1000000LL);
	hrtimer_start(&dev->timer, kt, HRTIMER_MODE_REL);
	dev->state = SIMTEMP_enSTATE_RUN;

	return 0;
}

static int simtemp_stop_sampler(struct simtemp_dev *dev)
{
	if (dev->state == SIMTEMP_enSTATE_STOP)
		return 0; /* Ya está detenido, no es un error */

	hrtimer_cancel(&dev->timer);
	dev->state = SIMTEMP_enSTATE_STOP;
	return 0;
}

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
    struct simtemp_dev *dev = dev_get_drvdata(stdevice);
    int ret;

    if (sysfs_streq(buffer, "STOP")) {
        ret = simtemp_stop_sampler(dev);
    } else if (sysfs_streq(buffer, "RUN")) {
        ret = simtemp_start_sampler(dev);
    } else {
        ret = -EINVAL;
    }
    return ret ? ret : count;
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

    /* simple deterministic temperature model: base 25000 mC + (seq % 100) */
    sample.TimeStamp_ns = ktime_get_ns();
    sample.Temperature_mC = 25000 + (dev->u64SequenceNumber % 100);
    sample.StatusFlags = SIMTEMP_FLAG_OK;
    dev->u64SequenceNumber++;

    /* Si es one-shot, marca la muestra y no reprogrames el timer */
    if (dev->mode == SIMTEMP_MODE_ONESHOT) {
        dev->stsample.StatusFlags |= SIMTEMP_FLAG_ONESHOT_DONE;
        dev->state = SIMTEMP_enSTATE_STOP; /* Transición de estado */
        simtemp_ringbuffer_write(&dev->stRingBuff, &sample);
        wake_up_interruptible(&dev->read_wait);
        return HRTIMER_NORESTART;
    }

    /* Escribir en el ring buffer. El lock está dentro de la función de escritura. */
    simtemp_ringbuffer_write(&dev->stRingBuff, &sample);

    /* wake readers */
    wake_up_interruptible(&dev->read_wait);

    /* re-arm timer for continuous mode */
    hrtimer_forward_now(timer, ktime_set(0, (s64)dev->u32Period_ms * 1000000LL));
    return HRTIMER_RESTART;
}

/*
 * La función open ahora obtiene el contexto del driver a través del miscdevice,
 * que a su vez lo obtuvo del platform_device en el probe.
 */

static int simtemp_open(struct inode *inode, struct file *file)
{
    file->private_data = container_of(inode->i_cdev, struct miscdevice, cdev)->private_data;
    return 0;
}

static int simtemp_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct simtemp_dev *device = file->private_data;
    struct simtemp_sample_v1 *tmp_buf;
    u32 samples_to_read, samples_read;
    int ret;

    /* Si el modo es non-blocking y no hay datos, retornar -EAGAIN */
    if ((file->f_flags & O_NONBLOCK) && simtemp_ringbuffer_empty(&device->stRingBuff))
        return -EAGAIN;

    /* Espera bloqueante hasta que haya datos disponibles en el ring buffer */
    ret = wait_event_interruptible(device->read_wait, !simtemp_ringbuffer_empty(&device->stRingBuff));
    if (ret)
        return ret; /* Interrumpido por una señal */

    /* Calcular cuántas muestras caben en el buffer del usuario */
    samples_to_read = count / sizeof(struct simtemp_sample_v1);
    if (samples_to_read == 0)
        return -EINVAL; /* El buffer del usuario es demasiado pequeño */

    tmp_buf = kcalloc(samples_to_read, sizeof(struct simtemp_sample_v1), GFP_KERNEL);
    if (!tmp_buf)
        return -ENOMEM;

    samples_read = simtemp_ringbuffer_read(&device->stRingBuff, tmp_buf, samples_to_read);

    if (copy_to_user(buf, tmp_buf, samples_read * sizeof(struct simtemp_sample_v1))) {
        kfree(tmp_buf);
        return -EFAULT;
    }

    kfree(tmp_buf);
    return samples_read * sizeof(struct simtemp_sample_v1);
}

static __poll_t simtemp_poll(struct file *file, poll_table *wait)
{
    struct simtemp_dev *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->read_wait, wait);

    if (!simtemp_ringbuffer_empty(&dev->stRingBuff))
        mask |= POLLIN | POLLRDNORM; /* Hay datos para leer */

    return mask;
}

static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simtemp_dev *dev = file->private_data;
    u32 tmp;    

    switch (cmd) {
    case SIMTEMP_IOC_START:
        return simtemp_start_sampler(dev);

    case SIMTEMP_IOC_STOP:
        return simtemp_stop_sampler(dev);

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
        /* Solo se puede cambiar el modo si está detenido (bloqueo a nivel de política) */
        if (dev->state == SIMTEMP_enSTATE_RUN)
            return -EBUSY;
        dev->mode = (enum simtemp_mode)tmp;
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

/* --- Platform Driver Implementation --- */

static const struct of_device_id simtemp_of_match[] = {
	{ .compatible = "simtemp,collector" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static int simtemp_probe(struct platform_device *pdev)
{
	struct simtemp_dev *sdev;
	int ret;

	/* 1. Asignar memoria para el contexto del dispositivo */
	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	/* 2. Inicializar el contexto del dispositivo */
	sdev->state = SIMTEMP_enSTATE_STOP;
	sdev->mode = SIMTEMP_MODE_CONTINUOUS; /* Default, será sobreescrito por DT */
	sdev->u32Period_ms = 1000;           /* Default, será sobreescrito por DT */
	sdev->u64SequenceNumber = 0;
	sdev->dev = &pdev->dev;

	/* 3. Parsear el Device Tree */
	simtemp_of_parse(&pdev->dev, sdev);

	/* 4. Inicializar componentes del driver */
	ret = simtemp_ringbuffer_alloc(&sdev->stRingBuff, SIMTEMP_DEFAULT_RING_SIZE, true);
	if (ret)
		return ret;

	init_waitqueue_head(&sdev->read_wait);
	hrtimer_init(&sdev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sdev->timer.function = simtemp_timer_cb;

	/* 5. Registrar el miscdevice */
	simtemp_miscdev.private_data = sdev;
	ret = misc_register(&simtemp_miscdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register misc device\n");
		simtemp_ringbuffer_free(&sdev->stRingBuff);
		return ret;
	}

	/* 6. Crear los archivos de sysfs */
	ret = sysfs_create_groups(&simtemp_miscdev.this_device->kobj, simtemp_groups);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs files\n");
		misc_deregister(&simtemp_miscdev);
		simtemp_ringbuffer_free(&sdev->stRingBuff);
		return ret;
	}

	/* 7. Guardar el contexto en el platform_device para usarlo en remove */
	platform_set_drvdata(pdev, sdev);

	pr_info("simtemp: loaded (miscdevice: /dev/simtemp)\n");
	return 0;
}

static int simtemp_remove(struct platform_device *pdev)
{
	struct simtemp_dev *sdev = platform_get_drvdata(pdev);

	/* Limpieza en orden inverso al probe */
	hrtimer_cancel(&sdev->timer);
	sysfs_remove_groups(&simtemp_miscdev.this_device->kobj, simtemp_groups);
	misc_deregister(&simtemp_miscdev);
	simtemp_ringbuffer_free(&sdev->stRingBuff);

	pr_info("simtemp: unloaded\n");
	return 0;
}

static struct platform_driver simtemp_platform_driver = {
	.driver = {
		.name = "simtemp",
		.of_match_table = simtemp_of_match,
	},
	.probe = simtemp_probe,
	.remove = simtemp_remove,
};
module_platform_driver(simtemp_platform_driver);

module_init(simtemp_module_init);
module_exit(simtemp_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fidel Eduardo Cabañas Castillo");
MODULE_DESCRIPTION("simtemp minimal one-shot miscdevice");
MODULE_VERSION("0.1");
