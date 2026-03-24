// Enable assert() everywhere.
#define DEBUG_ENABLE_ASSERTIONS 1

// Dump task byte for every tick to serial, disable other serial output
#define DEBUG_SCHED 0

// Dump every task switch
#define DEBUG_TASK_SWITCH 0

// Dump message every time task starts/stops
#define DEBUG_TASK_START_STOP 0

// Dump very verbose and cryptic floppy debug info
#define DEBUG_FLOPPY 0

// Dump semaphore debug info
#define DEBUG_SYNC 0

// Dump slab allocator debug info
#define DEBUG_SLAB 0

// Dump raw keyboard scancodes & decoded byte
#define DEBUG_DUMP_SCANCODES 0

// Add keystrokes to the keyboard ringbuffer to automate test sequences
// NOTE: add \x1B to type ESC
#define DEBUG_KEYSTROKES ""
