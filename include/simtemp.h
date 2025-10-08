#ifndef _SIMTEMP_H_
#define _SIMTEMP_H_

#include <linux/types.h>

#include <linux/device.h>
#include <linux/miscdevice.h>

#include <uapi/simtemp_uapi.h>

enum ensimtemp_mode {
	SIMTEMP_MODE_ONESHOT = 0,
    SIMTEMP_MODE_CONTINUOUS,
};

struct simtemp_dev{
    /*kernel section */
    struct device *dev;
    struct miscdevice miscdev;
    /*user section */
    enum ensimtemp_mode mode;
    /*data section*/
    struct stsimptemp_sample_v1 stsample;
};

#endif /* _SIMTEMP_H_ */

