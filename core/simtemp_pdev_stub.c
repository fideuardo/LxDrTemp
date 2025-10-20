// SPDX-License-Identifier: GPL-2.0-only
/*
 * Lightweight platform_device stub to allow loading nxp_simtemp
 * on systems without Device Tree (e.g. x86 dev boxes).
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define SIMTEMP_DRV_NAME "nxp_simtemp"

static struct platform_device *simtemp_pdev;

static int __init simtemp_pdev_init(void)
{
	int ret;

	simtemp_pdev = platform_device_alloc(SIMTEMP_DRV_NAME, PLATFORM_DEVID_NONE);
	if (!simtemp_pdev)
		return -ENOMEM;

	ret = platform_device_add(simtemp_pdev);
	if (ret) {
		platform_device_put(simtemp_pdev);
		return ret;
	}

	pr_info("simtemp_pdev: registered platform device '%s'\n", SIMTEMP_DRV_NAME);
	return 0;
}

static void __exit simtemp_pdev_exit(void)
{
	if (!simtemp_pdev)
		return;

	platform_device_unregister(simtemp_pdev);
	pr_info("simtemp_pdev: unregistered platform device\n");
}

module_init(simtemp_pdev_init);
module_exit(simtemp_pdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex Assistant");
MODULE_DESCRIPTION("Stub platform_device for nxp_simtemp on non-DT systems");
