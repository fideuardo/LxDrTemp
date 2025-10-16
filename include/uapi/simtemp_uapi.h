#ifndef _SIMTEMP_UAPI_H_
#define _SIMTEMP_UAPI_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * ABI v1.0, alineado con MSD_SimTemp.md
 * Esta es la estructura que el usuario recibe al leer /dev/simtemp
 */
struct simtemp_sample_v1 {
	__u64 timestamp_ns;   /* monotonic timestamp */
	__s32 temp_mC;        /* milli-degree Celsius (e.g., 44123 = 44.123 °C) */
	__u32 flags;          /* bit0=OK, bit1=OVERFLOW, bit2=THR_EDGE, bit3=ONESHOT_DONE */
} __attribute__((packed));

/* Flags para el campo 'flags' de la muestra */
#define SIMTEMP_FLAG_OK            (1u << 0) /* Muestra válida */
#define SIMTEMP_FLAG_ONESHOT_DONE  (1u << 3) /* Muestra final de una operación one-shot */
#define SIMTEMP_FLAG_OVERFLOW      (1u << 16) /* Muestra fuera de rango */
#define SIMTEMP_FLAG_THR_EDGE      (1u << 17) /* Umbral de temperatura superado */

/* IOCTL definitions */
#define SIMTEMP_IOC_MAGIC 'T'

/* Control del estado del muestreador */
#define SIMTEMP_IOC_START          _IO(SIMTEMP_IOC_MAGIC,  0x01)
#define SIMTEMP_IOC_STOP           _IO(SIMTEMP_IOC_MAGIC,  0x02)

#define SIMTEMP_IOC_GET_MODE       _IOR(SIMTEMP_IOC_MAGIC, 0x10, __u32)
#define SIMTEMP_IOC_SET_MODE       _IOW(SIMTEMP_IOC_MAGIC, 0x11, __u32)
#define SIMTEMP_IOC_GET_PERIOD     _IOR(SIMTEMP_IOC_MAGIC, 0x20, __u32)
#define SIMTEMP_IOC_SET_PERIOD     _IOW(SIMTEMP_IOC_MAGIC, 0x21, __u32)
#define SIMTEMP_IOC_GET_THRESHOLD  _IOR(SIMTEMP_IOC_MAGIC, 0x30, __s32)
#define SIMTEMP_IOC_SET_THRESHOLD  _IOW(SIMTEMP_IOC_MAGIC, 0x31, __s32)

#endif /* _SIMTEMP_UAPI_H_ */
