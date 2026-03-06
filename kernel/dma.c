#include <dma.h>
#include <assert.h>
#include <io.h>

/*
	Barebones support for the Intel 8237 DMA controller:
		https://en.wikipedia.org/wiki/Intel_8237

	The FDC uses 8 bit ISA DMA, which this function sets up. Some hardware-imposed
	limitations to consider:
	- Transfers must be <= 64k
	- Transfers must not cross a 64k boundary
	- DMA buffer must live at <16MB in physical memory, I guess because the DMA
	- DMA buffer must be physically contiguous
    controller has a 24 bit address bus?

    For now, dma_buf is statically allocated to guarantee it's contiguous and at
    a low address.
    FIXME: add flags to phys/virtual allocators and then allocate this dynamically
    to shrink kernel image.

    The floppy controller is hard-wired to use DMA channel 2, so that's hard-coded here
    as well.
*/

// NOTE: dma_reg_write() corrects these for slave DMA controller (chan > 3)
static const uint8_t addr_regs[] =  { 0x00, 0x02, 0x04, 0x06 };
static const uint8_t count_regs[] = { 0x01, 0x03, 0x05, 0x07 };
static const uint8_t page_regs[] =  { 0x87, 0x83, 0x81, 0x82 }; // Smells of legacy!

static void dma_reg_write(uint8_t channel, uint8_t reg, uint8_t value) {
	if (channel > 3) {
		if (reg > 0x0F)
			reg = reg + 8;
		else
			reg = (reg * 2) + DMA_SLAVE_OFFSET;
	}
	io_out8(reg, value);
}

static void dma_reg_write16(uint8_t channel, uint8_t reg, uint16_t value) {
	// First reset the flip-flop to ensure it's in a known state. The DMA controller uses
	// a flip flop to select which byte of a 16 bit quantity we're writing to, but the flip-flop
	// is shared between all 16 bit regs, so just reset it every time we write.
	dma_reg_write(channel, DMA_WREG_FLIPFLOP_RESET, 0xFF);
	dma_reg_write(channel, reg, (value >>  0) & 0xFF);
	dma_reg_write(channel, reg, (value >>  8) & 0xFF);
}

static void dma_set_start_addr(uint8_t channel, phys_addr addr) {
	assert(!(addr >> 24)); // Needs to be a 24 bit address

	// Write 16 bits to flip-flop registers, and last 8 bits to page address register
	dma_reg_write16(channel, addr_regs[(channel % 4)], addr & 0x0000FFFF);
	dma_reg_write(channel,   page_regs[(channel % 4)], (addr >> 16) & 0xFF);
}

//                                      __
//      DMA_MODE_ONDEMAND            (0b00000000)
//      DMA_MODE_SINGLE              (0b01000000)
//      DMA_MODE_BLOCK               (0b10000000)
//      DMA_MODE_CASCADE             (0b11000000)
//                                        _
//      DMA_MODE_DOWN                (0b00100000)
//                                         _
//      DMA_MODE_AUTO                (0b00010000)
//                                          __
#define DMA_MODE_TRANSFER_SELFTEST   (0b00000000)
#define DMA_MODE_TRANSFER_DEV_TO_MEM (0b00000100)
#define DMA_MODE_TRANSFER_MEM_TO_DEV (0b00001000)
#define DMA_MODE_TRANSFER_INVALID    (0b00001100)
//                                            __
//      DMA_MODE_CHANNEL_0           (0b00000000)
//      DMA_MODE_CHANNEL_1           (0b00000001)
//      DMA_MODE_CHANNEL_2           (0b00000010)
//      DMA_MODE_CHANNEL_3           (0b00000011)

void dma_setup(uint8_t channel, uint8_t mode, phys_addr buf, size_t bytes, enum dma_dir dir) {
	union {
		uint8_t b[4];
		uint32_t l;
	} addr, count;

	addr.l = buf;
	count.l = bytes - 1;
	if ((addr.l >> 24) || (count.l >> 16) || (((addr.l & 0xFFFF) + count.l) >> 16))
		panic("bad dma buf (%h)\n", buf);

	// uint8_t mode = DMA_MODE_SINGLE | (channel % 4);
	mode |= (channel % 4);
	switch (dir) {
	case dma_dir_read:
		mode |= DMA_MODE_TRANSFER_DEV_TO_MEM;
		break;
	case dma_dir_write:
		mode |= DMA_MODE_TRANSFER_MEM_TO_DEV;
		break;
	default:
		panic("bad dma mode %i\n", dir);
	}
	dma_reg_write(channel, DMA_WREG_SINGLE_CHAN_MASK, 0b00000110);
	dma_set_start_addr(channel, buf);
	dma_reg_write16(channel, count_regs[(channel % 4)], (bytes - 1) & 0x0000FFFF);
	dma_reg_write(channel, DMA_WREG_MODE, mode);
	dma_reg_write(channel, DMA_WREG_SINGLE_CHAN_MASK, 0x02);
}

