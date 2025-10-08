#ifndef _SIMTEMP_UAPI_H_
#define _SIMTEMP_UAPI_H_

#include <linux/types.h>

/* Flags de estado (MVP: solo usamos ONESHOT_DONE) */
#define SIMTEMP_FLAG_ONESHOT_DONE  (1u << 1) /* one-shot completed on this sample */

/* Nota: mantienes tu nombre y layout; packed fija el ABI binario */
struct stsimptemp_sample_v1 {
    __u64 u64TimeStamp_ns;    /* time stamp (nanoseconds) */
    __u64 u64SequenceNumber;  /* incremental sequence number */
    __u32 u32temperature_mC;  /* 25.0°C = 25000 mC */
    __u32 u32StatusFlags;     /* SIMTEMP_FLAG_* */
} __attribute__((packed));

#endif /* _SIMTEMP_UAPI_H_ */
