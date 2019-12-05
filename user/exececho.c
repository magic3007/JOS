#include <inc/lib.h>

void
umain(int argc, char **argv){
    cprintf("i am environment %08x from exececho\n", thisenv->env_id);
    for(int i = 0; i < argc; i++)
        cprintf("argv[%d]=%s\n", i, argv[i]);
}
