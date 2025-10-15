/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NXP_SIMTEMP_H_
#define _NXP_SIMTEMP_H_

/* Kernel includes */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

/* UAPI include */
#include <uapi/simtemp_uapi.h>
#include "simtemp_ringbuf.h"


/*
 * Internal driver enumerations.
 * These are NOT part of the UAPI contract as they represent internal state
 * managed by the driver. User-space interacts with these states via
 * sysfs strings (e.g., "RUN", "normal") or ioctl commands.
 */

/* Internal driver state */
enum nxp_simtemp_state {
	SIMTEMP_enSTATE_STOP = 0,
	SIMTEMP_enSTATE_RUN = 1,
};

/* Legacy operation mode */
enum nxp_simtemp_mode {
	SIMTEMP_MODE_ONESHOT = 0,
	SIMTEMP_MODE_CONTINUOUS = 1,
};

/* Simulation waveform mode (from MSD) */
enum nxp_simtemp_sim_mode {
	SIMTEMP_SIM_NORMAL = 0,
	SIMTEMP_SIM_NOISY = 1,
	SIMTEMP_SIM_RAMP = 2,
};

/* Main driver context structure */
struct nxp_simtemp_dev {
	struct device *dev;
	enum nxp_simtemp_state state;
	enum nxp_simtemp_mode mode;
	enum nxp_simtemp_sim_mode sim_mode;
	u32 u32Period_ms;
	u64 u64SequenceNumber;
	u32 u32Alerts;
	struct hrtimer timer;
	struct simtemp_ringbuffer stRingBuff;
	wait_queue_head_t read_wait;
};

int nxp_simtemp_of_parse(struct device *dev, struct nxp_simtemp_dev *sdev);

#endif /* _NXP_SIMTEMP_H_ */