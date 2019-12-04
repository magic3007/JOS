#include <inc/lib.h>

void
umain(int argc, char **argv){
    char str[2][256]={
        "echo",
        "I am executed from exec"
    };
    char *argv2[3]={str[0], str[1], 0};
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);
    exec("/echo", (const char **)&argv2); // never return
    panic("exec never return!");
}
