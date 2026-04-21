//
//  kbd.c - PC keyboard driver
//

#include <drv.h>
#include <drivers/kbd.h>
#include <serial_debug.h>
#include <stddef.h>
#include <lib/ringbuf.h>
#include <debug.h>
#include <assert.h>
#include <kprintf.h>
#include <io.h>
#include <x86.h>
#include <sched.h>
#include <fs/dev.h>

struct scancode {
	uint8_t byte[2]; // unshifted, shifted
};

static const struct scancode codes[256] = {
	[0x1E] = { { 'a', 'A' } },
	[0x30] = { { 'b', 'B' } },
	[0x2E] = { { 'c', 'C' } },
	[0x20] = { { 'd', 'D' } },
	[0x12] = { { 'e', 'E' } },
	[0x21] = { { 'f', 'F' } },
	[0x22] = { { 'g', 'G' } },
	[0x23] = { { 'h', 'H' } },
	[0x17] = { { 'i', 'I' } },
	[0x24] = { { 'j', 'J' } },
	[0x25] = { { 'k', 'K' } },
	[0x26] = { { 'l', 'L' } },
	[0x32] = { { 'm', 'M' } },
	[0x31] = { { 'n', 'N' } },
	[0x18] = { { 'o', 'O' } },
	[0x19] = { { 'p', 'P' } },
	[0x10] = { { 'q', 'Q' } },
	[0x13] = { { 'r', 'R' } },
	[0x1F] = { { 's', 'S' } },
	[0x14] = { { 't', 'T' } },
	[0x16] = { { 'u', 'U' } },
	[0x2F] = { { 'v', 'V' } },
	[0x11] = { { 'w', 'W' } },
	[0x2D] = { { 'x', 'X' } },
	[0x15] = { { 'y', 'Y' } },
	[0x2C] = { { 'z', 'Z' } },
	[0x39] = { { ' ', ' ' } },
	[0x02] = { { '1', '!' } },
	[0x03] = { { '2', '"' } },
	[0x04] = { { '3', '#' } },
	[0x05] = { { '4', '$' } },
	[0x06] = { { '5', '%' } },
	[0x07] = { { '6', '&' } },
	[0x08] = { { '7', '/' } },
	[0x09] = { { '8', '(' } },
	[0x0A] = { { '9', ')' } },
	[0x0B] = { { '0', '=' } },
	[0x0E] = { { 0x08, 0x08 } }, // Backspace
	[0x0F] = { { 0x09, 0x09 } }, // Horizontal tab
	[0x1C] = { { '\n', '\n' } },  // Return
	[0x01] = { { 0x1B, 0x1B } }, // ESC
	[0x2B] = { { '\'', '\''} }, // Apostrophe
	[0x33] = { { ',', ';' } },
	[0x34] = { { '.', ':' } },
	[0x35] = { { '-', '_' } },
	[0x28] = { { '\'', '\''} }, // FIXME: same as 0x2b? meant \\?
	[0x53] = { { 0x7F, 0x7F } },
};

#define SCANCODE_COUNT (sizeof(codes) / sizeof(struct scancode))

#define RB_CAP 64
static uint8_t buf[RB_CAP];
static struct ringbuf s_rb = { 0 };

static int kbd_read(struct device *dev, char *out, size_t n) {
	struct ringbuf *rb = dev->ctx;
	size_t bytes = 0;
	while (bytes < n) {
		char c;
		rb_read(rb, &c);
		out[bytes++] = c;
	}
	return bytes;
}

struct dev_char chardev_kbd = {
	.base = {
		.ctx = &s_rb,
		.name = "kbd"
	},
	.read = kbd_read,
	.write = NULL,
};

static const char *dbg_keystrokes = DEBUG_KEYSTROKES;

uint8_t lowercase(uint8_t byte) {
	if (byte > 64 && byte < 91) { // A-Z ASCII
		byte += 32; // Offset to lowercase
	}
	return byte;
}

enum mod_key {
	mod_none = 0,
	mod_shift,
	mod_ctrl,
	mod_alt,
	mod_0x60,
	mod_lmeta,
	mod_rmeta,
};

static uint8_t modifiers[] = {
	[mod_shift] = 0,
	[mod_ctrl]  = 0,
	[mod_alt]   = 0,
	[mod_0x60]  = 0,
	[mod_lmeta] = 0,
	[mod_rmeta] = 0,
};

static uint8_t get_mod(uint8_t scancode) {
	switch (scancode & 0x7f) {
	case 0x2A: case 0x36: return mod_shift;
	case 0x1D: return mod_ctrl;
	case 0x38: return mod_alt;
	case 0x60: return mod_0x60;
	case 0x5B: return mod_lmeta;
	case 0x5C: return mod_rmeta;
	default: return mod_none;
	}
}

void dump_modifiers(void) {
	kprintf("\n[%c%c%c%c%c%c]\n",
	        modifiers[mod_shift] ? 'S' : 's',
	        modifiers[mod_ctrl]  ? 'C' : 'c',
	        modifiers[mod_alt]   ? 'A' : 'a',
	        modifiers[mod_0x60]  ? 'O' : 'o',
	        modifiers[mod_lmeta] ? 'L' : 'l',
	        modifiers[mod_rmeta] ? 'R' : 'r'
	    );
}
static int check_special(uint8_t scancode) {
	// TODO: capslock, scrolllock and numlock
	uint8_t mod = get_mod(scancode);
	if (!mod)
		return 0;
	if (mod == 0x60) {
		assert(!modifiers[mod_0x60]); // assuming for now
		modifiers[mod_0x60] = 1;
	} else {
		// Top bit of scancode tells us if it's a "key up" event
		modifiers[get_mod(scancode)] = !(scancode & 0x80);
		if (modifiers[mod_0x60])
			modifiers[mod_0x60] = 0;
	}
#if DEBUG_DUMP_SCANCODES == 1
	dump_modifiers();
#endif
	return 1;
}

void received_scancode(uint8_t scancode) {
	uint8_t key_code = scancode & 0x7f;
	uint8_t key_up = scancode & 0x80;
	if (DEBUG_DUMP_SCANCODES && !halted)
		kprintf(" (%c%1h) ", key_up ? 'U' : 'D', key_code);
	
	uint8_t byte = codes[key_code].byte[modifiers[mod_shift]];

	if (byte && !key_up) {
		if (!s_rb.n_free.count) {
			// ringbuffer would block, and we are in irq context
			// so drop this scancode to avoid deadlocking the system.
			// This used to be a serial_out_byte() call, but that may lock up
			// the serial ringbuffer, if this interrupt happens right after a task
			// grabs the ringbuffer queue lock, causing the serial ringbuffer write to
			// grab it again, causing a deadlock.
			kput_noserial('!');
			return;
		}
		rb_write(&s_rb, byte);
	}
}

static void kbd_irq(const struct irq_regs *const regs) {
	(void)regs;
	uint8_t scancode = io_in8(0x60);
	if (check_special(scancode))
		return;
	if ((scancode & 0x7F) == 0x53 && modifiers[mod_ctrl] && modifiers[mod_alt])
		reboot();
	if (halted)
		return;
	received_scancode(scancode);
}

static int do_debug_keystrokes(void *ctx) {
	(void)ctx;
	char c;
	while ((c = *dbg_keystrokes++))
		rb_write(&s_rb, c);
	return 0;
}

void keyboard_debug_keystrokes(void) {
	if (dbg_keystrokes[0])
		task_create(do_debug_keystrokes, NULL, "debug_keystrokes", 0);
}

int probe(v_ma *a) {
	(void)a;
	rb_initialize(&s_rb, buf, RB_CAP);
	attach_irq(KBD_IRQ, kbd_irq, "keyboard");
	devfs_register_char(&chardev_kbd);
	return 0;
}

struct driver keyboard = {
	.name = "kbd",
	.probe = probe,
	.deps = { NULL }
};

register_driver(keyboard);
