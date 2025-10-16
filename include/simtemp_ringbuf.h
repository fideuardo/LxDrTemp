/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SIMTEMP_RINGBUF_H_
#define _SIMTEMP_RINGBUF_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <uapi/simtemp_uapi.h>

/* Forward declaration to avoid implicit declaration warnings and circular includes. */
struct nxp_simtemp_dev;

/**
 * struct simtemp_ringbuffer - Ring buffer for temperature samples.
 * @stBuffer: Pointer to the sample array that makes up the buffer.
 * @lock: Spinlock to protect concurrent access to the buffer.
 *        NOTE: The MSD specifies a SPSC lockless design. This implementation
 *        uses locks and is safe for multiple producers/consumers (MPMC),
 *        but does not comply with the original lockless design.
 * 
 * @u32BufferSize: Total capacity of the buffer (number of samples).
 * @u32head: Index where the next sample will be written (producer).
 * @u32tail: Index from where the next sample will be read (consumer).
 * @u32OverRuns: Counter for overrun/overwrite events.
 * @boDropOldest: Policy for a full buffer.
 *                true: overwrite the oldest sample.
 *                false: discard the new sample.
 */
struct simtemp_ringbuffer{
	struct simtemp_sample_v1 *stBuffer;
	spinlock_t lock;
	u32 u32BufferSize;
	u32 u32head;
	u32 u32tail;
	u32 u32OverRuns;
	bool boDropOldest;
};

bool nxp_simtemp_ringbuffer_write(struct simtemp_ringbuffer *srRingBuff, struct simtemp_sample_v1 *pstSample);
u32  nxp_simtemp_ringbuffer_read(struct simtemp_ringbuffer *srRingBuff, struct simtemp_sample_v1 *pstSample, u32 u32Count);
bool nxp_simtemp_ringbuffer_empty(struct simtemp_ringbuffer *srRingBuff);
u32  nxp_simtemp_ringbuffer_level(struct simtemp_ringbuffer *srRingBuff);
int nxp_simtemp_rb_init(struct nxp_simtemp_dev *sdev, u32 u32size, bool boDropOldest);

#endif /* _SIMTEMP_RINGBUF_H_ */
