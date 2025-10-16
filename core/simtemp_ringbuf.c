/*Kernel includes*/
#include <linux/slab.h>
#include <linux/string.h>

/*own includes*/
#include "nxp_simtemp.h"

/*--------------------------------------------------------------------------------------------------------*/
/*int  nxp_simtemp_rb_init(struct nxp_simtemp_dev *sdev -> Main device context                           */
/*                         u32 u32size              -> Buffer Size                                     */
/*                         bool boDropOldest         -> Overwritting is enable                          */
/*  Return:      0 -> Buffer was initialized properly                                                     */
/*         -EINVAL -> Invalid Argument                                                                    */
/*         -ENOMEM -> Out of memory                                                                       */
/*--------------------------------------------------------------------------------------------------------*/
int nxp_simtemp_rb_init(struct nxp_simtemp_dev *sdev, u32 u32size, bool boDropOldest)
{
	struct simtemp_ringbuffer *sRingBuff = &sdev->stRingBuff;

	/* Size validation for ring buffer implementation */
	if (u32size < 2)
		return -EINVAL;

	/*
	 * Allocate the buffer using device-managed memory. The kernel will
	 * automatically free this when the driver is removed.
	 */
	sRingBuff->stBuffer = devm_kcalloc(sdev->dev, u32size, sizeof(*sRingBuff->stBuffer), GFP_KERNEL);
	if (!sRingBuff->stBuffer)
		return -ENOMEM;

	/* Init the spin_lock and other parameters */
	spin_lock_init(&sRingBuff->lock);
	sRingBuff->u32BufferSize = u32size;
	sRingBuff->u32head = 0;
	sRingBuff->u32tail = 0;
	sRingBuff->u32OverRuns = 0;
	sRingBuff->boDropOldest = boDropOldest;
	return 0;
}

void nxp_simtemp_ringbuffer_free(struct simtemp_ringbuffer *srRingBuff)
{
    kfree(srRingBuff->stBuffer);
    srRingBuff->stBuffer = NULL;
    srRingBuff->u32BufferSize = 0;
}

static u32 simtemp__u32levellock(struct simtemp_ringbuffer *srRingBuff)
{
    u32 u32value;
    u32value = srRingBuff->u32head - srRingBuff->u32tail;
    return u32value;
}

/* FIX: Typo en el nombre de la estructura */
bool nxp_simtemp_ringbuffer_write(struct simtemp_ringbuffer *srRingBuff, struct simtemp_sample_v1 *pstSample)
{
    unsigned long ulflags;
    u32 u32bufferused;
    u32 u32next;
    bool status = true;

    /*lock spin: start security zone*/
    /* NOTE: El MSD especifica un diseño SPSC lockless. Esta implementación usa spinlocks.
     * Para un diseño lockless, se usarían barreras de memoria como smp_store_release() aquí.
     */
    spin_lock_irqsave(&srRingBuff->lock, ulflags);
    u32bufferused = srRingBuff->u32head - srRingBuff->u32tail;

    /*available space*/
    if(u32bufferused < srRingBuff->u32BufferSize)
    {
       u32next = srRingBuff->u32head % srRingBuff->u32BufferSize;
       srRingBuff->stBuffer[u32next] = *pstSample;
       srRingBuff->u32head++;
    }
    else
    {
         /*Oldest element can be deletedd*/
        if(srRingBuff->boDropOldest)
        {
            /* CORRECCIÓN: Sobrescribir la muestra más antigua avanzando el tail */
            u32next = srRingBuff->u32head % srRingBuff->u32BufferSize;
            srRingBuff->stBuffer[u32next] = *pstSample;
            srRingBuff->u32head++;
            srRingBuff->u32tail++; /* <-- Esto es crucial, se avanza el puntero de lectura */
            srRingBuff->u32OverRuns++;
        }
        else
        {
            srRingBuff->u32OverRuns++;
            status = false;
        }
    }
    spin_unlock_irqrestore(&srRingBuff->lock, ulflags);
    return status;
}

/* FIX: Typo en el nombre de la estructura */
u32 nxp_simtemp_ringbuffer_read(struct simtemp_ringbuffer *srRingBuff, struct simtemp_sample_v1 *pstSample, u32 u32Count)
{
    unsigned long ulflags;
    u32 u32bufferused;
    u32 u32next;
    u32 u32readcount = 0;

    spin_lock_irqsave(&srRingBuff->lock, ulflags);
    /* NOTE: Para un diseño lockless, se usaría smp_load_acquire() para leer el 'head'. */

    u32bufferused = srRingBuff->u32head - srRingBuff->u32tail;

    /* CORRECCIÓN: Limitar la lectura a la cantidad de datos disponibles o al espacio del usuario,
     * lo que sea menor.
     */
    if (u32Count > u32bufferused)
    {
        u32Count = u32bufferused;
    }

    /* Copiar los datos al buffer del usuario */
    for (u32readcount = 0; u32readcount < u32Count; u32readcount++)
    {
        u32next = srRingBuff->u32tail % srRingBuff->u32BufferSize;
        pstSample[u32readcount] = srRingBuff->stBuffer[u32next];
        srRingBuff->u32tail++;
    }

    spin_unlock_irqrestore(&srRingBuff->lock, ulflags);
    return u32readcount;
}


bool nxp_simtemp_ringbuffer_empty(struct simtemp_ringbuffer *srRingBuff)
{
    unsigned long ulflags;
    bool status = false;
    spin_lock_irqsave(&srRingBuff->lock, ulflags);
    if(srRingBuff->u32head == srRingBuff->u32tail)
    {
        status = true;
    }
    spin_unlock_irqrestore(&srRingBuff->lock, ulflags);
    return status;
}

u32 nxp_simtemp_ringbuffer_level(struct simtemp_ringbuffer *srRingBuff)
{
    unsigned long ulflags;
    u32 u32value;
    spin_lock_irqsave(&srRingBuff->lock, ulflags);
    u32value = simtemp__u32levellock(srRingBuff);
    spin_unlock_irqrestore(&srRingBuff->lock, ulflags);
    return u32value;
}
