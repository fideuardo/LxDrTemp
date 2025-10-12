#ifndef _SIMTEMP_UAPI_H_
#define _SIMTEMP_UAPI_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * ABI v1.0, alineado con MSD_SimTemp.md
 * Esta es la estructura que el usuario recibe al leer /dev/simtemp
 */
struct simtemp_sample_v1 {
	__u64 TimeStamp_ns;           /* Time Stamp */
	__u64 Temperature_mC;         /* p.ej. 25000 = 25.0°C */
	__u64 StatusFlags;           /* bit0=OK, bit1=OVERFLOW, bit2=THR_EDGE, bit3=ONESHOT_DONE */
} __attribute__((packed));

/* Flags para el campo 'flags' de la muestra */
#define SIMTEMP_FLAG_OK            (1u << 0) /* Muestra válida */
#define SIMTEMP_FLAG_ONESHOT_DONE  (1u << 3) /* Muestra final de una operación one-shot */

/* IOCTL definitions */
#define SIMTEMP_IOC_MAGIC 'T'

/* Control del estado del muestreador */
#define SIMTEMP_IOC_START          _IO(SIMTEMP_IOC_MAGIC,  0x01)
#define SIMTEMP_IOC_STOP           _IO(SIMTEMP_IOC_MAGIC,  0x02)

#define SIMTEMP_IOC_GET_MODE       _IOR(SIMTEMP_IOC_MAGIC, 0x10, __u32)
#define SIMTEMP_IOC_SET_MODE       _IOW(SIMTEMP_IOC_MAGIC, 0x11, __u32)
#define SIMTEMP_IOC_GET_PERIOD     _IOR(SIMTEMP_IOC_MAGIC, 0x20, __u32)
#define SIMTEMP_IOC_SET_PERIOD     _IOW(SIMTEMP_IOC_MAGIC, 0x21, __u32)

#endif /* _SIMTEMP_UAPI_H_ */
