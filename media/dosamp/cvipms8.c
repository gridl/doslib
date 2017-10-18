
#include <stdio.h>
#ifdef LINUX
#include <endian.h>
#else
#include <hw/cpu/endian.h>
#endif
#ifndef LINUX
#include <conio.h> /* this is where Open Watcom hides the outp() etc. functions */
#include <direct.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <malloc.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <dos.h>

#include <hw/dos/dos.h>
#include <hw/cpu/cpu.h>
#include <hw/8237/8237.h>       /* 8237 DMA */
#include <hw/8254/8254.h>       /* 8254 timer */
#include <hw/8259/8259.h>       /* 8259 PIC interrupts */
#include <hw/sndsb/sndsb.h>
#include <hw/cpu/cpurdtsc.h>
#include <hw/dos/doswin.h>
#include <hw/dos/tgusmega.h>
#include <hw/dos/tgussbos.h>
#include <hw/dos/tgusumid.h>
#include <hw/isapnp/isapnp.h>
#include <hw/sndsb/sndsbpnp.h>

#include "wavefmt.h"
#include "dosamp.h"
#include "timesrc.h"
#include "dosptrnm.h"
#include "filesrc.h"
#include "resample.h"
#include "cvip.h"

uint32_t convert_ip_mono2stereo_u8(uint32_t samples,void dosamp_FAR * const proc_buf,const uint32_t buf_max) {
    /* buffer range check */
    assert((samples * (uint32_t)2U) <= buf_max);

#if defined(__WATCOMC__) && defined(__I86__) && TARGET_MSDOS == 16
    /* DS:SI = proc_buf + samples - 1
     * ES:DI = proc_buf + samples + samples - 2
     * CX = samples
     *
     * ...
     *
     * b = (BYTE)DS:SI      SI--
     * w = b | (b << 8)
     * ES:DI = (WORD)w      DI -= 2
     */
    __asm {
        push    ds
        push    es
        std                         ; scan backwards
        lds     si,proc_buf
        mov     di,si
        push    ds
        pop     es
        mov     cx,word ptr samples
        add     si,cx
        add     di,cx
        add     di,cx
        dec     si
        sub     di,2
l1:     lodsb
        mov     ah,al
        stosw
        loop    l1
        pop     es
        pop     ds
        cld
    }
#else
    {
        /* in-place mono to stereo conversion (up to proc_buf_len)
         * from file_codec channels (1) to play_codec channels (2).
         * due to data expansion we process backwards. */
        uint8_t dosamp_FAR * buf = (uint8_t dosamp_FAR *)proc_buf + (samples * 2UL) - 1;
        uint8_t dosamp_FAR * sp = (uint8_t dosamp_FAR *)proc_buf + samples - 1;
        uint32_t i = samples;

        while (i-- != 0UL) {
            *buf-- = *sp;
            *buf-- = *sp;
            sp--;
        }
    }
#endif

    return (uint32_t)samples * (uint32_t)2U;
}

