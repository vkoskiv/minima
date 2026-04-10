#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <vkern.h>

#define TASK(task_entry) (task_entry), #task_entry

// TODO: kill, killall, etc.
// Name should have one letter prefixed with '&', and that is
// the key that invokes that thing.
// CMD blocks until return. JOB spawns func as bg task and returns immediately.
int cmd_list(void *ctx);
int cmd_exit(void *ctx);
int cmd_enter_menu(void *ctx);
int cmd_run_task(void *ctx);
int cmd_spawn_job(void *ctx);

struct cmd_arg {
	int (*fn)(void *ctx);
	void *ctx;
	const char *name;
};

// Call function with argument
#define FUNC(arg_name, arg_fn, arg_ctx) \
	{ \
		.name = (arg_name), \
		.fn = arg_fn, \
		.ctx = arg_ctx, \
		.cmds = NULL, \
	}

// Spawn task to run function with argument and wait for it to finish
#define CMD(arg_name, arg_fn, arg_ctx) \
	{ \
		.name = (arg_name), \
		.fn = cmd_run_task, \
		.ctx = &(struct cmd_arg){ .name = (#arg_fn), .fn = (arg_fn), .ctx = (arg_ctx)}, \
		.cmds = NULL, \
	}

// Spawn background task to run function with argument
#define JOB(arg_name, arg_fn, arg_ctx) \
	{ \
		.name = (arg_name), \
		.fn = cmd_spawn_job, \
		.ctx = &(struct cmd_arg){ .name = (#arg_fn), .fn = (arg_fn), .ctx = (arg_ctx)}, \
		.cmds = NULL, \
	}

// Create submenu to organize commands.
#define SUBMENU(arg_name, ...) \
	{ \
		.name = (arg_name), \
		.fn = cmd_enter_menu, \
		.ctx = NULL, \
		.cmds = (const struct command[]){ \
		    FUNC("&0 List Commands", cmd_list, NULL), \
		    FUNC("&\x1B Exit", cmd_exit, NULL), \
			__VA_ARGS__ \
			{ 0 }, \
		} \
	}

// Root menu, no exit option.
#define MENU(arg_name, ...) \
	{ \
		.name = (arg_name), \
		.fn = cmd_enter_menu, \
		.ctx = NULL, \
		.cmds = (const struct command[]){ \
		    FUNC("&0 List Commands", cmd_list, NULL), \
			__VA_ARGS__ \
			{ 0 }, \
		} \
	}

struct command {
	const char *name;
	int (*fn)(void *ctx);
	void *ctx; // if NULL, ctx is struct command *
	const struct command *cmds;
};

#endif

