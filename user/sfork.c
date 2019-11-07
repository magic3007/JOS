#include <inc/lib.h>

int a = 1;

void
umain(int argc, char **argv){
	int envid = sfork();
    if(envid < 0)
        panic("%e", envid);
    
    if(envid == 0){
        cprintf("I am child. a = %d\n", a);
    }else{
        cprintf("I am parent. a = %d\n", a);
        a = 2;
    }
}