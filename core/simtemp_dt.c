/* SPDX-License-Identifier: GPL-2.0-only */

/* kernel includes */
#include <linux/of.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>

/* own includes */
#include "simtemp.h"

/**
 * simtemp_of_parse() - Parsea las propiedades del Device Tree para el driver.
 * @dev: Puntero al struct device.
 * @sdev: Puntero al contexto del driver (simtemp_dev).
 *
 * Lee las propiedades del nodo del Device Tree asociado con este dispositivo
 * y configura los valores iniciales del driver. Si una propiedad no se
* encuentra, se mantienen los valores por defecto codificados.
 *
 * Contexto: Llamado desde la función probe().
 * Retorna: 0 si tiene éxito, o un código de error negativo.
 */
int simtemp_of_parse(struct device *dev, struct simtemp_dev *sdev)
{
	struct device_node *np = dev->of_node;
	const char *mode_str;
	u32 period_us;
	int ret;

	/* Si no hay nodo de DT, no es un error. Se usarán los defaults. */
	if (!np) {
		dev_info(dev, "No device tree node found, using default settings.\n");
		return 0;
	}

	/* Leer el período de muestreo en microsegundos */
	ret = of_property_read_u32(np, "sample-period-us", &period_us);
	if (ret == 0) {
		/* Validar y convertir a milisegundos */
		if (period_us >= 1000) {
			sdev->u32Period_ms = period_us / 1000;
			dev_info(dev, "DT: Set sampling period to %u ms\n", sdev->u32Period_ms);
		} else {
			dev_warn(dev, "DT: sample-period-us (%u) is too small, ignoring.\n", period_us);
		}
	}

	/* Leer el modo de operación */
	ret = of_property_read_string(np, "operation-mode", &mode_str);
	if (ret == 0) {
		if (strcmp(mode_str, "one-shot") == 0) {
			sdev->mode = SIMTEMP_MODE_ONESHOT;
			dev_info(dev, "DT: Set operation mode to one-shot\n");
		} else if (strcmp(mode_str, "continuous") == 0) {
			sdev->mode = SIMTEMP_MODE_CONTINUOUS;
			dev_info(dev, "DT: Set operation mode to continuous\n");
		}
	}

	/* Placeholder para futuras propiedades como threshold-mC, alert-policy, etc. */

	return 0;
}
EXPORT_SYMBOL_GPL(simtemp_of_parse);