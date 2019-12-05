#include <inc/lib.h>
#include <inc/elf.h>


#define EXECTEMP 0xe0000000


int
exec(const char *prog, const char **argv){
    int r, fd;
    struct Stat st;
    
    /* read the state information of this elf file */
    if((r = stat(prog, &st)) < 0)
        return r;

    if((fd = open(prog, O_RDONLY)) < 0)
        return fd;
    
    void *va = (void *)EXECTEMP;
    void *end = (void*)(EXECTEMP + st.st_size);

    /* read the raw ELF file at EXECTEMP in memory */
    do{
        if((r = sys_page_alloc(0, va, PTE_U | PTE_P | PTE_W)) < 0)
            goto error;
        size_t n = MIN(PGSIZE, end - va);
        if((r = readn(fd, va, n)) < 0)
            goto error;
        va += n;
    }while(va < end);

    if((r = sys_exec((uint8_t*)EXECTEMP, argv))<0) /* If on success, it will never return. */
        goto error;
    
error:
    close(fd);
    return r;
}

// Exec, taking command-line arguments array directly on the stack.
// NOTE: Must have a sentinal of NULL at the end of the args
// (none of the args may be NULL).
int
execl(const char *prog, const char *arg0, ...){
    // We calculate argc by advancing the args until we hit NULL.
	// The contract of the function guarantees that the last
	// argument will always be NULL, and that none of the other
	// arguments will be NULL.
    int argc = 0;
    va_list vl;
    va_start(vl, arg0);
    while(va_arg(vl, void *) != NULL)
        argc++;
    va_end(vl);

    // Now that we have the size of the args, do a second pass
	// and store the values in a VLA, which has the format of argv
    const char *argv[argc+2];
    argv[0] = arg0;
    argv[argc+1] = NULL;

    va_start(vl, arg0);
    for(unsigned i = 0; i < argc; i++)
        argv[i + 1] = va_arg(vl, const char *);
    va_end(vl);

    return exec(prog, argv);
}