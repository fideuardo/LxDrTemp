/*Kernel includes*/
#include <linux/slab.h>
#include <linux/string.h>

/*own includes*/
#include "nxp_simtemp.h"

/*--------------------------------------------------------------------------------------------------------*/
/*int  simtemp_ringbuffer_alloc(struct simtemp_ringbuffer *srRingBuff     -> Ring Buffer structure        */
/*                                                      u32 u32BufferSize -> Buffer Size                  */ 
/*                                                      bool boDropOldest -> Overwritting is enable       */
/*  Return:      0 -> Buffer was initialized properly                                                     */
/*         -EINVAL -> Invalid Argument                                                                    */
/*         -ENOMEM -> Out of memory                                                                       */
/*--------------------------------------------------------------------------------------------------------*/
int nxp_simtemp_ringbuffer_alloc(struct simtemp_ringbuffer *srRingBuff, u32 u32BufferSize, bool boDropOldest)
{
    /*Size validation for ring buffer implementation*/
    if(0 == u32BufferSize )
    {
        return -EINVAL;
    }
    /* Assigning memory address. FIX: Typo en el nombre de la estructura */
    srRingBuff->stBuffer = kmalloc_array(u32BufferSize, sizeof(struct simtemp_sample_v1), GFP_KERNEL | __GFP_ZERO);
    
    /* Validate if memory block was assigned */
    if(NULL == srRingBuff->stBuffer)
    {
        return -ENOMEM;
    }

    /*Adding functional parameters*/

    srRingBuff->u32BufferSize = u32BufferSize;
    srRingBuff->u32head = 0;
    srRingBuff->u32tail = 0;
    srRingBuff->u32OverRuns = 0;
    srRingBuff->boDropOldest = boDropOldest;
    /*Init the spin_lock*/
    spin_lock_init(&srRingBuff->lock);

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
