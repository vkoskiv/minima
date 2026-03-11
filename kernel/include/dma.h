#ifndef _DMA_H_
#define _DMA_H_

#include <stddef.h>
#include <mm/types.h>

enum dma_dir {
	dma_dir_read = 1,
	dma_dir_write = 2,
};

#define DMA_SLAVE_OFFSET 0xC0
// NOTE: These are adjusted in dma_reg_{write,read}() for slave DMA
// channels 4-7.
#define DMA_WREG_CHAN0_START_ADDR (0x00)
#define DMA_WREG_CHAN0_COUNT      (0x01)
#define DMA_WREG_CHAN1_START_ADDR (0x02)
#define DMA_WREG_CHAN1_COUNT      (0x03)
#define DMA_WREG_CHAN2_START_ADDR (0x04)
#define DMA_WREG_CHAN2_COUNT      (0x05)
#define DMA_WREG_CHAN3_START_ADDR (0x06)
#define DMA_WREG_CHAN3_COUNT      (0x07)
// status: |REQ3|REQ2|REQ1|REQ0|TC3|TC2|TC1|TC0  |
//         |-------------------|=================|
//         |DMA request pending|Transfer Complete|
//         ---------------------------------------
#define DMA_RREG_STATUS           (0x08)
// command:|DACKP|DRQP|EXTW|PRIO|COMP|COND|ADHE|MMT|
// EXTW & COMP: broken, speed up DMA by 25%
// PRIO: broken, 0 == allow rotating DMA priorities
// MMT & ADHE: broken, memory-to-memory DMA transfers.
// COND: When 1, disable DMA controller.
#define DMA_WREG_COMMAND          (0x08)
#define DMA_WREG_REQUEST          (0x09)
#define DMA_WREG_SINGLE_CHAN_MASK (0x0A)
#define DMA_WREG_MODE             (0x0B)
// Docs are unclear if flip-flop resets after sending full 16 bit quantity,
// we reset it before every 16 bit write just to be sure.
#define DMA_WREG_FLIPFLOP_RESET   (0x0C)
#define DMA_RREG_INTERMEDIATE     (0x0D)
// master reset resets flipflop, clears status, sets all mask bits on.
#define DMA_WREG_MASTER_RESET     (0x0D)
// mask reset sets all mask bits off.
#define DMA_WREG_MASK_RESET       (0x0E)
#define DMA_RWREG_MULTICHAN_MASK  (0x0F)
#define DMA_RWREG_CHAN0_PAGE_ADDR (0x87)
#define DMA_RWREG_CHAN1_PAGE_ADDR (0x83)
#define DMA_RWREG_CHAN2_PAGE_ADDR (0x81)
#define DMA_RWREG_CHAN3_PAGE_ADDR (0x82)


//                                      __
#define DMA_MODE_ONDEMAND            (0b00000000)
#define DMA_MODE_SINGLE              (0b01000000)
#define DMA_MODE_BLOCK               (0b10000000)
#define DMA_MODE_CASCADE             (0b11000000)
//                                        _
#define DMA_MODE_DOWN                (0b00100000)
//                                         _
#define DMA_MODE_AUTO                (0b00010000)

void dma_setup(uint8_t channel, uint8_t mode, phys_addr buf, size_t bytes, enum dma_dir dir);

#endif
