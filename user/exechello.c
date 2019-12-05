#include <inc/lib.h>

void
umain(int argc, char **argv){
    int r;
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);
    char paras[4][356]={
        "exececho",
        "hello",
        "I am executed from exec!"
    }; // make sure that these strings are stored on stack.
    for(int i = 0; i < 3; i++)
        cprintf("address of para[%d]: %p\n", i, paras[i]);
    if((r =  execl("exececho", paras[0], paras[1], paras[2], 0)) < 0) // never return
        panic("exec never return!: %e", r);
}
