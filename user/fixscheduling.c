#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	envid_t env;

	cprintf("I am the parent.  Forking the child...\n");
	if ((env = fork()) == 0) {
		cprintf("I am the child.  Reset my priority and let it lower than that of the parent.\n");
        envid_t me = sys_getenvid();
        sys_env_set_priority(me, -1);
        sys_yield();
        panic("Never reach here!");
	}

	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();
	cprintf("I am the parent.  Try to Run the child...\n");
	sys_yield();

	cprintf("I am the parent.  Killing the child...\n");
	sys_env_destroy(env);
}

