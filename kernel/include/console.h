#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <vkern.h>

#define TASK(task_entry) (task_entry), #task_entry

struct cmd {
	v_ilist tids;
	int is_user;
	int max_tids; // <-- 0 == unlimited tasks, killable with shortcut_kill
	void *ctx;
	int (*task_entry)(void *);
	const char *name;
	const char *descr;
	const char shortcut_spawn;
	const char shortcut_kill;
};

struct cmd_list {
	const char *name;
	struct cmd cmds[]; // <-- Null-terminated
};

int enter_cmdlist(void *ctx);

#endif

