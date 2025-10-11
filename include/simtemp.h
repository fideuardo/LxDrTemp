#ifndef _SIMTEMP_H_
#define _SIMTEMP_H_

#include <linux/types.h>

#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <uapi/simtemp_uapi.h>

/*Operation Modes */
enum ensimtemp_mode {
	SIMTEMP_MODE_ONESHOT = 0,
    SIMTEMP_MODE_CONTINUOUS,
};
/* Ring buffer */

struct simtemp_ringbuffer{
    struct stsimptemp_sample_v1 *stBuffer; /*Buffer pointer*/
    spinlock_t lock;    /*Lock*/
    u32 u32BufferSize;  /*Buffer size*/
    u32 u32head;        /*Head pointer*/
    u32 u32tail;        /*Tail pointer*/
    u32 u32OverRuns;    /*Overruns*/
    bool boDropOldest;  /*True: overwrite; false: discard */
};

/*Device Context */
struct simtemp_dev{
    /*kernel section */
    struct device *dev;
    struct miscdevice miscdev;
    wait_queue_head_t read_wait; /* Wait queue for polling */
    /* high resolution timer for periodic sampling */
    struct hrtimer timer;
    /* period in milliseconds */
    u32 u32Period_ms;
    /* sequence counter for samples */
    u64 u64SequenceNumber;
    /* lock protecting stsample and sequence */
    spinlock_t sample_lock;
    /*user section */
    enum ensimtemp_mode mode;
    /*data section*/
    struct stsimptemp_sample_v1 stsample; /* Current sample */
    struct simtemp_ringbuffer stRingBuff; /* Ring buffer instance */
};

int  simtemp_ringbuffer_alloc(struct simtemp_ringbuffer *srRingBuff, u32 u32BufferSize, bool boDropOldest);
void simtemp_ringbuffer_free(struct simtemp_ringbuffer *srRingBuff);
bool simtemp_ringbuffer_write(struct simtemp_ringbuffer *srRingBuff, struct stsimptemp_sample_v1 *pstSample);
u32  simtemp_ringbuffer_read(struct simtemp_ringbuffer *srRingBuff, struct stsimptemp_sample_v1 *pstSample, u32 u32Count);
bool simtemp_ringbuffer_empty(struct simtemp_ringbuffer *srRingBuff);
u32  simtemp_ringbuffer_level(struct simtemp_ringbuffer *srRingBuff);


#endif /* _SIMTEMP_H_ */

