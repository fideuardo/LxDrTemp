#ifndef _SIMTEMP_H_
#define _SIMTEMP_H_
/*types*/
#include <linux/types.h>

/*Kernel libraries*/
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

/*Own libraries*/
#include <simtemp_ringbuf.h>
#include <uapi/simtemp_uapi.h>

/*Operation Modes */
enum simtemp_mode {
	SIMTEMP_MODE_ONESHOT = 0,
    SIMTEMP_MODE_CONTINUOUS,
};

/* Driver States */
enum simtemp_state {
	SIMTEMP_enSTATE_STOP = 0,
	SIMTEMP_enSTATE_RUN,
	SIMTEMP_STATE_ERROR, /*Pending to add more states*/
};

/*Device Context */
struct simtemp_dev {
    /*kernel section */
    struct device *dev;
    /*Control elements*/
    enum simtemp_state state; /*Driver State: STOPPED, RUNNING, ERROR*/

    wait_queue_head_t read_wait; /* Wait queue for polling */
    /* high resolution timer for periodic sampling */
    struct hrtimer timer;
    /* period in milliseconds */
    u32 u32Period_ms;
    /* sequence counter for samples */
    u64 u64SequenceNumber;
    /* lock protecting stsample and sequence */
    bool data_ready; /* Flag to signal new data for poll/read */
    spinlock_t sample_lock;
    /*user section */
    enum simtemp_mode mode;
    /*data section*/
    struct simtemp_sample_v1 stsample; /* Current sample */
    struct simtemp_ringbuffer stRingBuff; /* Ring buffer instance */
    
};

/* --- Funciones exportadas desde otros archivos --- */
int simtemp_of_parse(struct device *dev, struct simtemp_dev *sdev);


#endif /* _SIMTEMP_H_ */
