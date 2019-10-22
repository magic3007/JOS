// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/types.h>
#include <inc/memlayout.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
<<<<<<< HEAD
#include <kern/pmap.h>
=======
>>>>>>> mit/lab3
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace","Display a backtrace for the calling program", mon_backtrace },
	{ "showmappings", "Display all of the physical page mappings given a particular range of virtual/linear addresses", mon_showmappings},
	{ "setpageperm", "Explicitly set, clear, or change the permissions of any mapping in the current address space", mon_setpageperm},
	{ "memdisp", "Display the contents of a range of memory given either a virtual or physical address range.", mon_memdisp}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp = read_ebp();
	uintptr_t *p = (uintptr_t *)ebp;
	
	
	struct Eipdebuginfo info;

	int i;
	uintptr_t eip;
	
	cprintf("Stack backtrace:\n");
	
	while(p){
		
		eip = *(p + 1);
		debuginfo_eip(eip, &info);
		
		cprintf("  ebp %08x  eip %08x  args", p, eip);
		for(i = 0; i < 5; i++)
			cprintf(" %08x", *(p + 2 + i));
		cprintf("\n");
		cprintf("         %s:%d: ", info.eip_file, info.eip_line);
        cprintf("%.*s", info.eip_fn_namelen, info.eip_fn_name);
        cprintf("+%d\n", eip - info.eip_fn_addr);
		
		p = (uintptr_t *)*p;

	}

	return 0;
}


/***** Implementations of memory management commands *****/

void printfperm(pte_t *pte){
	cprintf("%c%c%c", (*pte & PTE_P)?'P':'-', (*pte & PTE_W)?'W':'-', (*pte & PTE_U)?'U':'-');
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf){
	if(argc != 3){
		cprintf("Format error. Usage: showmappings [hexadecimal begin_addr] [hexadecimal end_addr]\n");
		return 0;
	}
	uintptr_t begin_addr = strtol(argv[1], NULL, 16), end_addr = strtol(argv[2], NULL, 16);
	if(begin_addr>=end_addr){
		cprintf("begin address shoule less than end address.");
		return 0;
	}
	pde_t *pgdir = KADDR(rcr3());
	for(uintptr_t va = begin_addr; va < end_addr; va+=PGSIZE) {
		pte_t *pte = pgdir_walk(pgdir, (const void *)va, 0);
		if(pte && (*pte & PTE_P)){ // page mapping exists
			physaddr_t pa = PTE_ADDR(*pte);
			cprintf("va:%08p-%08p\tpa:%08p-%08p\t", va, va +PGSIZE - 1, pa, pa + PGSIZE - 1);
			printfperm(pte);
			cprintf("\n");
		}else
			cprintf("va:%08p-%08p page mapping does not exist.\n", va, va +PGSIZE - 1);
	}
	return 0;
}

int
mon_setpageperm(int argc, char **argv, struct Trapframe *tf){
	if(argc != 4 || !(strcmp(argv[2], "clear")==0 || strcmp(argv[2], "set")==0)){
		cprintf("Format error. Usage: setpageperm [hexadecimal address] [clear|set] [W|U]\n");
		return 0;
	}
	uintptr_t va = strtol(argv[1], NULL, 16);
	pde_t *pgdir = KADDR(rcr3());
	pte_t *pte = pgdir_walk(pgdir, (const char*)va, 0);
	if(pte && (*pte & PTE_P)){
		uint16_t perm;
		switch (argv[3][0]) {
			case 'W': perm = PTE_W; break;
			case 'U': perm = PTE_U; break;
			default: perm = 0;
		}
		cprintf("change va:%08p-%08p page mapping permissions.\n", va, va+PGSIZE-1);
		cprintf("\tBefore: ");
		printfperm(pte);
		cprintf("\n");
		if(strcmp(argv[2], "clear")==0)
			*pte = *pte & ~perm;
		else if (strcmp(argv[2], "set")==0)
			*pte = *pte | perm;
		cprintf("\tAfter:  ");
		printfperm(pte);
		cprintf("\n");
	}else
		cprintf("va:%08p-%08p page mapping does not exist.\n", va, va + PGSIZE - 1);
	return 0;
}

int
mon_memdisp(int argc, char **argv, struct Trapframe *tf){
	if(argc!=4){
mon_memdisp_format_error:
		cprintf("Format error. Usage: memdump [P|V: physical address or virtual address] [hexadecimal begin_addr] [hexadecimal end_addr]\n");
		return 0;
	}
	uintptr_t begin_addr = strtol(argv[2], NULL, 16), end_addr = strtol(argv[3], NULL, 16);
	if(begin_addr>=end_addr){
		cprintf("begin address shoule be less than end address.\n");
		return 0;
	}
	pde_t *pgdir = KADDR(rcr3());
	if(strcmp(argv[1], "V") == 0){
		for(uintptr_t va = begin_addr; va < end_addr; va++){
			pte_t* pte = pgdir_walk(pgdir, (const void*)va, 0);
			if (pte && (*pte & PTE_P))
				cprintf("va: %08p\tcontent: %2x\n", va, *(unsigned char*)va);
			else
				cprintf("va: %08p\tMapping does not exist.\n", va);
		}
    }else if(strcmp(argv[1], "P") == 0){
		for(physaddr_t pa = begin_addr; pa < end_addr; pa++){
			void *va = KADDR(pa);
			pte_t* pte = pgdir_walk(pgdir, va, 0); // use page tablee to determine whether the physical address exists.
			if (!pte)
				cprintf("pa: %08p\tPhysical address does not exist.\n", pa);
			else
				cprintf("pa: %08p\tcontent: %2x\n", pa, *(unsigned char*)va);
		}
	}else
		goto mon_memdisp_format_error;
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	
	// Test text color
	cprintf("\x001b[30m A \x001b[31m B \x001b[32m C \x001b[33m D \x001b[34m E \x001b[35m F \x001b[36m G \x001b[37m H");
	cprintf("\x001b[0m\n");
	cprintf("\x001b[42;30m A \x001b[42;31m B \x001b[42;32m C \x001b[42;33m D \x001b[42;34m E \x001b[42;35m F \x001b[42;36m G \x001b[42;37m H");
	cprintf("\x001b[0m\n");
	
	if (tf != NULL)
		print_trapframe(tf);

	// Test showmappings
	runcmd("setpageperm 0xf0000000 set U", tf);
	runcmd("showmappings 0xEFFFC000 0xf0010000", tf);
	runcmd("memdisp V 0xf0000000 0xf0000010", tf);
	runcmd("memdisp P 0x00000000 0x00000010", tf);
	
	
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
