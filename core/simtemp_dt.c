/* SPDX-License-Identifier: GPL-2.0-only */

/* kernel includes */
#include <linux/of.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>

/* own includes */
#include "nxp_simtemp.h"

/**
 * nxp_simtemp_of_parse() - Parsea las propiedades del Device Tree para el driver.
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
int nxp_simtemp_of_parse(struct device *dev, struct nxp_simtemp_dev *sdev)
{
	struct device_node *np = dev->of_node;
	const char *mode_str;
	u32 period_ms;
	s32 threshold_mc;
	int ret;

	/* Si no hay nodo de DT, no es un error. Se usarán los defaults. */
	if (!np) {
		dev_info(dev, "No device tree node found, using default settings.\n");
		return 0;
	}

	/* Leer el período de muestreo en milisegundos, según MSD v0.3 */
	ret = of_property_read_u32(np, "sampling-ms", &period_ms);
	if (ret == 0) {
		/* Validar período (MSD: 5-5000ms) */
		if (period_ms >= 5 && period_ms <= 5000) {
			sdev->u32Period_ms = period_ms;
			dev_info(dev, "DT: Set sampling period to %u ms\n", sdev->u32Period_ms);
		} else {
			dev_warn(dev, "DT: sampling-ms=%u is out of range [5, 5000]. Using default %u ms.\n",
				 period_ms, sdev->u32Period_ms);
		}

	}

	/* Leer el umbral de temperatura en miligrados Celsius */
	ret = of_property_read_s32(np, "threshold-mC", &threshold_mc);
	if (ret == 0) {
		if (threshold_mc >= 0 && threshold_mc <= 150000) {
			sdev->s32Threshold_mC = threshold_mc;
			dev_info(dev, "DT: Set threshold to %d mC\n", sdev->s32Threshold_mC);
		} else {
			dev_warn(dev, "DT: threshold-mC=%d out of range [0, 150000]. Using default %d mC.\n",
				 threshold_mc, sdev->s32Threshold_mC);
		}
	}

	/* Leer el modo de operación (ahora 'mode' con 'normal', 'noisy', 'ramp') */
	/* Placeholder para la nueva lógica de modos */
	ret = of_property_read_string(np, "operation-mode", &mode_str);
	if (ret == 0) {
		if (strcmp(mode_str, "one-shot") == 0) {
			sdev->mode = SIMTEMP_MODE_ONESHOT;
			dev_info(dev, "DT: Set legacy operation mode to one-shot\n");
		} else if (strcmp(mode_str, "continuous") == 0) {
			sdev->mode = SIMTEMP_MODE_CONTINUOUS;
			dev_info(dev, "DT: Set legacy operation mode to continuous\n");
		}
	}

	/* Placeholder para futuras propiedades como threshold-mC, alert-policy, etc. */

	return 0;
}
EXPORT_SYMBOL_GPL(nxp_simtemp_of_parse);
