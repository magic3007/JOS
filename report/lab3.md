# Report for Lab 3

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751

## Part A: User Environments and Exception Handling

### Allocating the Environments Array

> **Exercise 1.** Modify `mem_init()` in `kern/pmap.c` to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in `inc/memlayout.h`) so user processes can read from this array.
>
> You should run your code and make sure `check_kern_pgdir()` succeeds.

In `kern/pmap.c`, malloc physical memory for `env` and and map this physical memory region to `[UENVS, UENVS+PTSIZE)`, just like what we have done for `pages` in lab2. 

```c
	envs = (struct Env *)boot_alloc(sizeof(struct Env) * NENV);
```
```c
	boot_map_region(kern_pgdir, UENVS, PTSIZE, PADDR(envs), PTE_U | PTE_P);
```

### Creating and Running Environments

> **Exercise 2.** In the file `env.c`, finish coding the following functions:
>
> - `env_init()`
>
>   Initialize all of the `Env` structures in the `envs` array and add them to the `env_free_list`. Also calls `env_init_percpu`, which configures the segmentation hardware with separate segments for privilege level 0 (kernel) and privilege level 3 (user).
>
> - `env_setup_vm()`
>
>   Allocate a page directory for a new environment and initialize the kernel portion of the new environment's address space.
>
> - `region_alloc()`
>
>   Allocates and maps physical memory for an environment
>
> - `load_icode()`
>
>   You will need to parse an ELF binary image, much like the boot loader already does, and load its contents into the user address space of a new environment.
>
> - `env_create()`
>
>   Allocate an environment with `env_alloc` and call `load_icode` to load an ELF binary into it.
>
> - `env_run()`
>
>   Start a given environment running in user mode.
>
> As you write these functions, you might find the new `cprintf` verb `%e` useful -- it prints a description corresponding to an error code. For example,
>
> ```c
> 	r = -E_NO_MEM;	
> 	panic("env_alloc: %e", r);
> ```
>
> will panic with the message `env_alloc: out of memory`.

in `kern/env.c`:

```c
void env_init(void){
	for(int i = NENV - 1; i>=0 ;i--){
		envs[i].env_id = 0;
		envs[i].env_link = env_free_list;
		env_free_list = envs+i;
	}
	
	// Per-CPU part of the initialization
	env_init_percpu();
}
```
```c
static int env_setup_vm(struct Env *e){
	int i;
	struct PageInfo *p = NULL;

	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	// increment env_pgdir's pp_ref for env_free to work correctly.	
	p->pp_ref++;

	e->env_pgdir = (pte_t *)page2kva(p);
	memcpy(e->env_pgdir, kern_pgdir, PGSIZE);

	// UVPT maps the env's own page table read-only.
	// Permissions: kernel R, user R
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;
	return 0;
}
```

```c
static void region_alloc(struct Env *e, void *va, size_t len){
	void *begin, *end;
	struct PageInfo *pp;
	int rc;
	if((uintptr_t)va >= VA_UPPER - len)
		panic("region_alloc: va + len is larger than MAX_VA_IDX\n");
	begin = ROUNDDOWN(va, PGSIZE);
	end = ROUNDUP(va + len, PGSIZE);
	for(; begin < end; begin += PGSIZE){
		if((pp = page_alloc(0)) == NULL)
			panic("page_alloc: out of free memory");
		if((rc = page_insert(e->env_pgdir, pp, begin, PTE_W | PTE_U)) != 0)
			panic("page_insert: %e", rc);
	}
}
```

As for `load_icode`, notice that we must switch the page directory before using `memset` and `memmove`, as the allocated memory should be stored in the environment address space.

```c
static void load_icode(struct Env *e, uint8_t *binary){
	struct Proghdr *ph, *eph;
	struct Elf *elf;
	uintptr_t va;
	size_t memsz, filesz, offset;

	elf = (struct Elf *)binary;
	
	if(elf->e_magic!=ELF_MAGIC)
		panic("load_icode: bad elf");
	
	ph = (struct Proghdr *) (binary + elf->e_phoff);
	eph = ph + elf->e_phnum;

	// switch to the environment page directory
	lcr3(PADDR(e->env_pgdir));

	for(;ph < eph; ph++){
		if(ph->p_type == ELF_PROG_LOAD){
			va = ph->p_va;
			memsz = ph->p_memsz;
			filesz = ph->p_filesz;
			offset = ph->p_offset;

			if(filesz > memsz)
				panic("segment file size is larger than segment memory size");
			
			region_alloc(e, (void*)va, memsz);
			memmove((void*)va, binary + offset, filesz);
            memset((void*)(va+filesz), 0, memsz - filesz);
		}
	}

	// switch bach to kernel page directory(unnecessary)
	lcr3(PADDR(kern_pgdir));
	e->env_tf.tf_eip = elf->e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.
	region_alloc(e, (void *)(USTACKTOP - PGSIZE), PGSIZE);

}
```

```c
void env_create(uint8_t *binary, enum EnvType type){
	int rc;
	struct Env *env;
	if((rc = env_alloc(&env, 0)) < 0)
		panic("env_malloc: %e", rc);
	load_icode(env, binary);
	env->env_type = type;
}
```

```c
void env_run(struct Env *e){
	if(curenv && curenv->env_status == ENV_RUNNING)
		curenv->env_status = ENV_RUNNABLE;
	curenv = e;
	curenv->env_status = ENV_RUNNING;
	curenv->env_runs++;
	lcr3(PADDR(e->env_pgdir));
	env_pop_tf(&e->env_tf);	
}
```

### Setting Up the IDT

> **Exercise 4.** Edit `trapentry.S` and `trap.c` and implement the features described above. The macros `TRAPHANDLER` and `TRAPHANDLER_NOEC` in `trapentry.S` should help you, as well as the T_* defines in `inc/trap.h`. You will need to add an entry point in `trapentry.S` (using those macros) for each trap defined in `inc/trap.h`, and you'll have to provide `alltraps` which the `TRAPHANDLER` macros refer to. You will also need to modify `trap_init()` to initialize the `idt` to point to each of these entry points defined in `trapentry.S`; the `SETGATE` macro will be helpful here.
>
> Your `_alltraps` should:
>
> 1. push values to make the stack look like a struct `Trapframe`
> 2. load `GD_KD` into `%ds` and `%es`
> 3. `pushl %esp` to pass a pointer to the `Trapframe` as an argument to trap()
> 4. `call trap` (can `trap` ever return?)
>
> Consider using the `pushal` instruction; it fits nicely with the layout of the `struct Trapframe`.

First of all, we refer to this website https://wiki.osdev.org/Exceptions and decide whether each trap entry will generate error code. Then we add the trap entries defined in `inc/trap.h`:

```c
// Trap numbers
// These are processor defined:
#define T_DIVIDE     0		// divide error
#define T_DEBUG      1		// debug exception
#define T_NMI        2		// non-maskable interrupt
#define T_BRKPT      3		// breakpoint
#define T_OFLOW      4		// overflow
#define T_BOUND      5		// bounds check
#define T_ILLOP      6		// illegal opcode
#define T_DEVICE     7		// device not available
#define T_DBLFLT     8		// double fault
/* #define T_COPROC  9 */	// reserved (not generated by recent processors)
#define T_TSS       10		// invalid task switch segment
#define T_SEGNP     11		// segment not present
#define T_STACK     12		// stack exception
#define T_GPFLT     13		// general protection fault
#define T_PGFLT     14		// page fault
/* #define T_RES    15 */	// reserved
#define T_FPERR     16		// floating point error
#define T_ALIGN     17		// aligment check
#define T_MCHK      18		// machine check
#define T_SIMDERR   19		// SIMD floating point error
```

in `kern/trapentry.S`:

```assembly
/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
 	.globl name;		/* define global symbol for 'name' */	\
 	.type name, @function;	/* symbol type is function */		\
 	.align 2;		/* align function definition */		\
 	name:			/* function starts here */		\
 	pushl $(num);							\
 	jmp _alltraps


/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
   	.globl name;							\
  	.type name, @function;						\
  	.align 2;							\
  	name:								\
  	pushl $0;							\
  	pushl $(num);							\
  	jmp _alltraps

TRAPHANDLER_NOEC(INT_HANDLER_0, T_DIVIDE)
TRAPHANDLER_NOEC(INT_HANDLER_1, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_2, T_NMI)
TRAPHANDLER_NOEC(INT_HANDLER_3, T_BRKPT)
TRAPHANDLER_NOEC(INT_HANDLER_4, T_OFLOW)
TRAPHANDLER_NOEC(INT_HANDLER_5, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_6, T_ILLOP)
TRAPHANDLER_NOEC(INT_HANDLER_7, T_DEVICE)
TRAPHANDLER(INT_HANDLER_8, T_DBLFLT)

TRAPHANDLER(INT_HANDLER_10, T_TSS)
TRAPHANDLER(INT_HANDLER_11, T_SEGNP)
TRAPHANDLER(INT_HANDLER_12, T_STACK)
TRAPHANDLER(INT_HANDLER_13, T_GPFLT)
TRAPHANDLER(INT_HANDLER_14, T_PGFLT)

TRAPHANDLER_NOEC(INT_HANDLER_16, T_FPERR)
TRAPHANDLER(INT_HANDLER_17, T_ALIGN)
TRAPHANDLER_NOEC(INT_HANDLER_18, T_MCHK)
TRAPHANDLER_NOEC(INT_HANDLER_19, T_SIMDERR)
					
TRAPHANDLER_NOEC(INT_HANDLER_48, T_SYSCALL)
```

Also, we need to analysis the struct of `trapFrame` in `inc/trap.c`, understand which registers have been pushed into the kernel stack by x86 hardware and and push the remaining parts in trap entries and their common part `inc/trapentry.S:_alltraps`:

```c
struct Trapframe {
	struct PushRegs tf_regs;
	uint16_t tf_es;
	uint16_t tf_padding1;
	uint16_t tf_ds;
	uint16_t tf_padding2;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding3;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding4;
} __attribute__((packed));
```

```assembly
_alltraps:
	/* push %ds and %es register on kernel stack */
	pushl %ds;
	pushl %es;

	/* push all general-purpose registers */
	pushal;
	
	/* assign %ds and %es to kernel data segment selector */
	movl $GD_KD, %eax;
	movw %ax, %ds;
	movw %ax, %es;
	
	/* pass a pointer to the Trapframe as an argument to trap() */
	pushl %esp
	
    /* call trap() and never return */
	call trap
```

Finally, declare the corresponding C function in `kern/trap.c:trap_init` and register the trap entries in IDT.

```c
void
void trap_init(void){
	extern struct Segdesc gdt[];

	extern void INT_HANDLER_0();
	extern void INT_HANDLER_1();
	extern void INT_HANDLER_2();
	extern void INT_HANDLER_3();
	extern void INT_HANDLER_4();
	extern void INT_HANDLER_5();
	extern void INT_HANDLER_6();
	extern void INT_HANDLER_7();
	extern void INT_HANDLER_8();
	extern void INT_HANDLER_10();
	extern void INT_HANDLER_11();
	extern void INT_HANDLER_12();
	extern void INT_HANDLER_13();
	extern void INT_HANDLER_14();
	extern void INT_HANDLER_16();
	extern void INT_HANDLER_17();
	extern void INT_HANDLER_18();
	extern void INT_HANDLER_19();
	extern void INT_HANDLER_48();


	SETGATE(idt[0], 0, GD_KT, INT_HANDLER_0, 0);
	SETGATE(idt[1], 0, GD_KT, INT_HANDLER_1, 0);
	SETGATE(idt[2], 0, GD_KT, INT_HANDLER_2, 0);
	SETGATE(idt[3], 0, GD_KT, INT_HANDLER_3, 0);
	SETGATE(idt[4], 0, GD_KT, INT_HANDLER_4, 0);
	SETGATE(idt[5], 0, GD_KT, INT_HANDLER_5, 0);
	SETGATE(idt[6], 0, GD_KT, INT_HANDLER_6, 0);
	SETGATE(idt[7], 0, GD_KT, INT_HANDLER_7, 0);
	SETGATE(idt[8], 0, GD_KT, INT_HANDLER_8, 0);

	SETGATE(idt[10], 0, GD_KT, INT_HANDLER_10, 0);
	SETGATE(idt[11], 0, GD_KT, INT_HANDLER_11, 0);
	SETGATE(idt[12], 0, GD_KT, INT_HANDLER_12, 0);
	SETGATE(idt[13], 0, GD_KT, INT_HANDLER_13, 0);
	SETGATE(idt[14], 0, GD_KT, INT_HANDLER_14, 0);
	
	SETGATE(idt[16], 0, GD_KT, INT_HANDLER_16, 0);
	SETGATE(idt[17], 0, GD_KT, INT_HANDLER_17, 0);
	SETGATE(idt[18], 0, GD_KT, INT_HANDLER_18, 0);
	SETGATE(idt[19], 0, GD_KT, INT_HANDLER_19, 0);

	SETGATE(idt[48], 1, GD_KT, INT_HANDLER_48, 3);
	
	// Per-CPU setup 
	trap_init_percpu();
}
```

> **Questions**
>
> Answer the following questions in your `answers-lab3.txt`:
>
> 1. What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)
> 2. Did you have to do anything to make the `user/softint` program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says `int $14`. *Why* should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

1. If we just use one handler for all exceptions/interrupts, we will have no idea about what kind of exception/interrupt trigger this handler. Meanwhile, x86 hardware's behaviors also varies from different exceptions/interrupts with regard to whether pushing error node on kernel stack.

2. However, the user space program is not allowed to trigger page fault(trap 14), as we have assigned to DPL to 0 at the interrupt gate of page fault. When the CPU discovers that it is not set up to handle this system call interrupt, it will generate a general protection exception(trap 13), that is why is actually produce a generally protection call(trap 13).

   Note that a potentially invalid value might be stored in `cr2`, which is the virtual address which caused the fault, if the kernel allow the user space program to trigger `softint`'s `int $14` and invoke the kernel's page fault handler(trap 14). This can potentially cause allocation of pages that the user process is not otherwise authorized to do so.

>  *Challenge!* You probably have a lot of very similar code right now, between the lists of `TRAPHANDLER` in `trapentry.S` and their installations in `trap.c`. Clean this up. Change the macros in `trapentry.S` to automatically generate a table for `trap.c` to use. Note that you can switch between laying down code and data in the assembler by using the directives `.text` and `.data`. 

We can use function pointer array to address this challenger. In `kern/trap.c`:

```c
void trap_init(void)
{
	extern struct Segdesc gdt[];
    
	typedef void (*int_fun_t)();
	extern int_fun_t int_funs[];
    
	for(int i = 0; i <=19; i++)
		if(i==T_BRKPT){
			SETGATE(idt[i], 1, GD_KT, int_funs[i], 3);
		}else if(i!=9 && i!=15)
			SETGATE(idt[i], 0, GD_KT, int_funs[i], 0);

	SETGATE(idt[T_SYSCALL], 1, GD_KT, int_funs[T_SYSCALL], 3);
	
	// Per-CPU setup 
	trap_init_percpu();
}
```

The function pointer array is stored in `.data` segment consecutively, and we need to initial it with the entry address of each exception/interrupt handler in `.text`. To learn more about *Arrays in Assembly Language*, please cite [here](http://www.cs.uwm.edu/classes/cs315/Bacon/Lecture/HTML/ch12s04.html). 

In `kern/trapentry.S`, we modify the macro `TRAPHANDLER_NOEC`/`TRAPHANDLER` and add the entry address to `.data` segment.

```assembly
 #define TRAPHANDLER(name, num)						\
 .data;												\
 	.long name;									\
 .text;											\
 	.globl name;		/* define global symbol for 'name' */	\
 	.type name, @function;	/* symbol type is function */		\
 	.align 2;		/* align function definition */		\
 	name:			/* function starts here */		\
 	pushl $(num);							\
 	jmp _alltraps
 
 #define TRAPHANDLER_NOEC(name, num)					\
 .data; 										  \
 	.long name;								\
 .text;										\
 	.globl name;							\
 	.type name, @function;						\
 	.align 2;							\
 	name:								\
 	pushl $0;							\
 	pushl $(num);							\
 	jmp _alltraps
 
 #define padding(num)\
.data;				\
	.space   (num)*4

.data
	.align 2;		
	.global int_funs;
int_funs:


TRAPHANDLER_NOEC(INT_HANDLER_0, T_DIVIDE)
TRAPHANDLER_NOEC(INT_HANDLER_1, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_2, T_NMI)
TRAPHANDLER_NOEC(INT_HANDLER_3, T_BRKPT)
TRAPHANDLER_NOEC(INT_HANDLER_4, T_OFLOW)
TRAPHANDLER_NOEC(INT_HANDLER_5, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_6, T_ILLOP)
TRAPHANDLER_NOEC(INT_HANDLER_7, T_DEVICE)
TRAPHANDLER(INT_HANDLER_8, T_DBLFLT)
padding(1)
TRAPHANDLER(INT_HANDLER_10, T_TSS)
TRAPHANDLER(INT_HANDLER_11, T_SEGNP)
TRAPHANDLER(INT_HANDLER_12, T_STACK)
TRAPHANDLER(INT_HANDLER_13, T_GPFLT)
TRAPHANDLER(INT_HANDLER_14, T_PGFLT)
padding(1)
TRAPHANDLER_NOEC(INT_HANDLER_16, T_FPERR)
TRAPHANDLER(INT_HANDLER_17, T_ALIGN)
TRAPHANDLER_NOEC(INT_HANDLER_18, T_MCHK)
TRAPHANDLER_NOEC(INT_HANDLER_19, T_SIMDERR)
padding(28)							
TRAPHANDLER_NOEC(INT_HANDLER_48, T_SYSCALL)
```

## Part B: Page Faults, Breakpoints Exceptions, and System Calls

### Handling Page Faults

>  **Exercise 5.** Modify `trap_dispatch()` to dispatch page fault exceptions to `page_fault_handler()`. You should now be able to get make grade to succeed on the `faultread`, `faultreadkernel`, `faultwrite`, and `faultwritekernel` tests. If any of them don't work, figure out why and fix them. Remember that you can boot JOS into a particular user program using `make run-*x*` or `make run-*x*-nox`. For instance, make `run-hello-nox` runs the *hello* user program. 

When a trap occurs, dispatch the trap handler based on the trap number, which have been pushed onto the kernel stack by the trap entry we added in previous exercise.

In `kern/trap.c`:

```c
static void trap_dispatch(struct Trapframe *tf){
	// Handle processor exceptions.
	switch (tf->tf_trapno){
		case T_PGFLT:
			page_fault_handler(tf);
			return;
		case T_BRKPT:
			monitor(tf);
			return;
		case T_SYSCALL:
			tf->tf_regs.reg_eax = syscall(tf->tf_regs.reg_eax, 
				tf->tf_regs.reg_edx, 
				tf->tf_regs.reg_ecx,
				tf->tf_regs.reg_ebx, 
				tf->tf_regs.reg_edi, 
				tf->tf_regs.reg_esi);
			return;
	}
	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}
```

### The Breakpoint Exception

>  **Exercise 6.** Modify `trap_dispatch()` to make breakpoint exceptions invoke the kernel monitor. You should now be able to get make grade to succeed on the `breakpoint` test. 

 This usage of `int 3` is actually somewhat appropriate if we think of the JOS kernel monitor as a primitive debugger. The code is shown in previous code block.

> **Questions**
>
> 3. The break point test case will either generate a break point exception or a general protection fault depending on how you initialized the break point entry in the IDT (i.e., your call to `SETGATE` from `trap_init`). Why? How do you need to set it up in order to get the breakpoint exception to work as specified above and what incorrect setup would cause it to trigger a general protection fault?
> 4. What do you think is the point of these mechanisms, particularly in light of what the `user/softint` test program does?

3. The difference that determine whether to generate a break point exception or a general protection fault is the `DPL` of `T_BRKPT` entry in the IDT. If the `DPL` is 0, it will generate a general protection fault, as user-mode program has no privilege to trigger this entry, which will otherwise trigger general protection fault. On the other hand, if the `DPL` is 3, it will generate a break point exception as expected. Thus following is my implementation in `kern/trap.c:trap_init`:

```c
for(int i = 0; i <=19; i++)
		if(i==T_BRKPT){
			SETGATE(idt[i], 1, GD_KT, int_funs[i], 3);
		}else if(i!=9 && i!=15)
			SETGATE(idt[i], 0, GD_KT, int_funs[i], 0);
```

4. The propose of these mechanisms is to provide the user-mode program access to trap into the kernel-mode in a legal and harmless manner.

### System calls

> **Exercise 7.** Add a handler in the kernel for interrupt vector `T_SYSCALL`. You will have to edit `kern/trapentry.S` and `kern/trap.c`'s `trap_init()`. You also need to change `trap_dispatch()` to handle the system call interrupt by calling `syscall()` (defined in `kern/syscall.c`) with the appropriate arguments, and then arranging for the return value to be passed back to the user process in `%eax`. Finally, you need to implement `syscall()` in `kern/syscall.c`. Make sure `syscall()` returns `-E_INVAL` if the system call number is invalid. You should read and understand `lib/syscall.c` (especially the inline assembly routine) in order to confirm your understanding of the system call interface. Handle all the system calls listed in `inc/syscall.h` by invoking the corresponding kernel function for each call.
>
> Run the `user/hello` program under your kernel (make run-hello). It should print "`hello, world`" on the console and then cause a page fault in user mode. If this does not happen, it probably means your system call handler isn't quite right. You should also now be able to get make grade to succeed on the `testbss` test.

​	in `kern/trapentyry.S`:

```c
#define padding(num)\
.data;				\
	.space   (num)*4

.data
	.align 2;		
	.global int_funs;
int_funs:


TRAPHANDLER_NOEC(INT_HANDLER_0, T_DIVIDE)
TRAPHANDLER_NOEC(INT_HANDLER_1, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_2, T_NMI)
TRAPHANDLER_NOEC(INT_HANDLER_3, T_BRKPT)
TRAPHANDLER_NOEC(INT_HANDLER_4, T_OFLOW)
TRAPHANDLER_NOEC(INT_HANDLER_5, T_DEBUG)
TRAPHANDLER_NOEC(INT_HANDLER_6, T_ILLOP)
TRAPHANDLER_NOEC(INT_HANDLER_7, T_DEVICE)
TRAPHANDLER(INT_HANDLER_8, T_DBLFLT)
padding(1)
TRAPHANDLER(INT_HANDLER_10, T_TSS)
TRAPHANDLER(INT_HANDLER_11, T_SEGNP)
TRAPHANDLER(INT_HANDLER_12, T_STACK)
TRAPHANDLER(INT_HANDLER_13, T_GPFLT)
TRAPHANDLER(INT_HANDLER_14, T_PGFLT)
padding(1)
TRAPHANDLER_NOEC(INT_HANDLER_16, T_FPERR)
TRAPHANDLER(INT_HANDLER_17, T_ALIGN)
TRAPHANDLER_NOEC(INT_HANDLER_18, T_MCHK)
TRAPHANDLER_NOEC(INT_HANDLER_19, T_SIMDERR)
padding(28)							
TRAPHANDLER_NOEC(INT_HANDLER_48, T_SYSCALL)
```

Note that some trap entries haven't been implemented, thus e need to pad the space.

in `kern/trap.c:trap_init`:

 ```c
void trap_init(void){
	extern struct Segdesc gdt[];
    
	typedef void (*int_fun_t)();
	extern int_fun_t int_funs[];

	for(int i = 0; i <=19; i++)
		if(i==T_BRKPT){
			SETGATE(idt[i], 1, GD_KT, int_funs[i], 3);
		}else if(i!=9 && i!=15)
			SETGATE(idt[i], 0, GD_KT, int_funs[i], 0);

	SETGATE(idt[T_SYSCALL], 1, GD_KT, int_funs[T_SYSCALL], 3);
	
	// Per-CPU setup 
	trap_init_percpu();
}
 ```

in `kern/trap.c:trap_dispatch`:

```c
static void
trap_dispatch(struct Trapframe *tf){
	// Handle processor exceptions.
	switch (tf->tf_trapno){
		case T_PGFLT:
			page_fault_handler(tf);
			return;
		case T_BRKPT:
			monitor(tf);
			return;
		case T_SYSCALL:
			tf->tf_regs.reg_eax = syscall(tf->tf_regs.reg_eax, 
				tf->tf_regs.reg_edx, 
				tf->tf_regs.reg_ecx,
				tf->tf_regs.reg_ebx, 
				tf->tf_regs.reg_edi, 
				tf->tf_regs.reg_esi);
			return;
	}
	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}
```

Meanwhile, we could find the appropriate arguments in `lib/syscall.c`:

```c
static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		     : "=a" (ret)
		     : "i" (T_SYSCALL),
		       "a" (num),
		       "d" (a1),
		       "c" (a2),
		       "b" (a3),
		       "D" (a4),
		       "S" (a5)
		     : "cc", "memory");

	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);

	return ret;
}
```

in `kern/syscall.c:`, we implement `syscall()`:

```c
// Dispatches to the correct kernel function, passing the arguments.
int32_t syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	int ret = 0;
	
	switch (syscallno) {
	case SYS_cputs: 
			sys_cputs((char*)a1, a2);
			ret = 0;
			break;
		case SYS_cgetc:
			ret = sys_cgetc();
			break;
		case SYS_getenvid:
			ret = sys_getenvid();
			break;
		case SYS_env_destroy:
			ret = sys_env_destroy(a1);
			break;
		default:
			ret = -E_INVAL;
	}

	return ret;	
}

```

Also, we find the appropriate augments in `lib/syscall.c`:

```c
void
sys_cputs(const char *s, size_t len)
{
	syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}

int
sys_cgetc(void)
{
	return syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int
sys_env_destroy(envid_t envid)
{
	return syscall(SYS_env_destroy, 1, envid, 0, 0, 0, 0);
}

envid_t
sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}
```

>  **Exercise 8.** Add the required code to the user library, then boot your kernel. You should see `user/hello` print "`hello, world`" and then print "`i am environment 00001000`".`user/hello` then attempts to "exit" by calling `sys_env_destroy()` (see `lib/libmain.c` and `lib/exit.c`). Since the kernel currently only supports one user environment, it should report that it has destroyed the only environment and then drop into the kernel monitor. You should be able to get make grade to succeed on the `hello` test. 

in `lib/libmain.c`, just follow the hint and initialize `thisenv`.

```c
void libmain(int argc, char **argv)
{
	// set thisenv to point at our Env structure in envs[].
	thisenv = envs+ENVX(sys_getenvid());

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}	
```

> **Exercise 9.** Change `kern/trap.c` to panic if a page fault happens in kernel mode.
>
> Hint: to determine whether a fault happened in user mode or in kernel mode, check the low bits of the `tf_cs`.
>
> Read `user_mem_assert` in `kern/pmap.c` and implement `user_mem_check` in that same file.
>
> Change `kern/syscall.c` to sanity check arguments to system calls.
>
> Boot your kernel, running `user/buggyhello`. The environment should be destroyed, and the kernel should *not* panic. You should see:
>
> ```
> 	[00001000] user_mem_check assertion failure for va 00000001
> 	[00001000] free env 00001000
> 	Destroyed the only environment - nothing more to do!
> ```
>
> Finally, change `debuginfo_eip` in `kern/kdebug.c` to call `user_mem_check` on `usd`, `stabs`, and `stabstr`. If you now run `user/breakpoint`, you should be able to run `backtrace` from the kernel monitor and see the `backtrace` traverse into `lib/libmain.c` before the kernel panics with a page fault. What causes this page fault? You don't need to fix it, but you should understand why it happens.

in `kern/trap.c:page_fault_handler`:

```c
void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if ((tf->tf_cs&3) == 0)
		panic("Kernel page fault at 0x%08x", fault_va);
	
	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}
```

in `kern/pmap.c`:

```c
int user_mem_check(struct Env *env, const void *va, size_t len, int perm){
	// LAB 3: Your code here.
	uintptr_t begin,end;

	begin = (uintptr_t)ROUNDDOWN(va, PGSIZE); 
	end = (uintptr_t)ROUNDUP(va+len, PGSIZE);

	for (uintptr_t i = begin; i < end; i+=PGSIZE) {
		pte_t *pte = pgdir_walk(env->env_pgdir, (void*)i, 0);
		if ((i>=ULIM) || !pte || !(*pte & PTE_P) || ((*pte & perm) != perm)) {
			user_mem_check_addr = i<(uintptr_t)va ? (uintptr_t)va : i;
			return -E_FAULT;
		}
	}
	return 0;
}
```

in `kern/syscall.c`:

```c
static void sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	struct Env *e;
	envid2env(sys_getenvid(), &e, 1);
	user_mem_assert(e, s, len, PTE_U);
	
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}
```

Boot the kernel and run `user/buggyhello`, we get following output:

```bash
$ make run-buggyhello-nox
make[1]: Entering directory '/mnt/d/Desktop/Desktop/JOS/lab'
+ cc kern/init.c
+ as kern/trapentry.S
+ cc[USER] lib/libmain.c
+ ar obj/lib/libjos.a
+ ld obj/user/hello
+ ld obj/user/buggyhello
+ ld obj/user/buggyhello2
+ ld obj/user/evilhello
+ ld obj/user/testbss
+ ld obj/user/divzero
+ ld obj/user/breakpoint
+ ld obj/user/softint
+ ld obj/user/badsegment
+ ld obj/user/faultread
+ ld obj/user/faultreadkernel
+ ld obj/user/faultwrite
+ ld obj/user/faultwritekernel
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/mnt/d/Desktop/Desktop/JOS/lab'
/mnt/d/Desktop/Desktop/JOS/qemu_build/bin/qemu-system-x86_64 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
[00001000] user_mem_check assertion failure for va 00000001
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
```

In `kern/kdebug.c`:

```c
// Find the relevant set of stabs
	if (addr >= ULIM) {
		stabs = __STAB_BEGIN__;
		stab_end = __STAB_END__;
		stabstr = __STABSTR_BEGIN__;
		stabstr_end = __STABSTR_END__;
	} else {
		// The user-application linker script, user/user.ld,
		// puts information about the application's stabs (equivalent
		// to __STAB_BEGIN__, __STAB_END__, __STABSTR_BEGIN__, and
		// __STABSTR_END__) in a structure located at virtual address
		// USTABDATA.
		const struct UserStabData *usd = (const struct UserStabData *) USTABDATA;

		// Make sure this memory is valid.
		// Return -1 if it is not.  Hint: Call user_mem_check.
		// LAB 3: Your code here.
		if (user_mem_check(curenv, usd, sizeof(struct UserStabData), PTE_U))
			return -1;


		stabs = usd->stabs;
		stab_end = usd->stab_end;
		stabstr = usd->stabstr;
		stabstr_end = usd->stabstr_end;

		// Make sure the STABS and string table memory is valid.
		// LAB 3: Your code here.
		if (user_mem_check(curenv, stabs, sizeof(struct Stab), PTE_U))
			return -1;
		if (user_mem_check(curenv, stabstr, stabstr_end-stabstr, PTE_U))
			return -1;
	}
```

Boot the kernel and follow the instructions in exercise statement:

```bash
$ make run-breakpoint-nox
make[1]: Entering directory '/mnt/d/Desktop/Desktop/JOS/lab'
+ cc kern/init.c
+ cc kern/trap.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/mnt/d/Desktop/Desktop/JOS/lab'
/mnt/d/Desktop/Desktop/JOS/qemu_build/bin/qemu-system-x86_64 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
 A  B  C  D  E  F  G  H
 A  B  C  D  E  F  G  H
TRAP frame at 0xf01d3000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfc0
  oesp 0xefffffdc
  ebx  0x00802000
  edx  0x0080202c
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x00000003 Breakpoint
  err  0x00000000
  eip  0x00800037
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfc0
  ss   0x----0023
K> backtrace
Stack backtrace:
  ebp efffff00  eip f0101127  args 00000001 efffff28 f01d3000 f0107486 f011bf48
         kern/monitor.c:271: monitor+392
  ebp efffff80  eip f0104999  args f01d3000 efffffbc f0104212 00000092 f0193000
         kern/trap.c:205: trap+300
  ebp efffffb0  eip f01049f6  args efffffbc 00000000 00000000 eebfdfc0 efffffdc
         kern/trapentry.S:104: <unknown>+0
  ebp eebfdfc0  eip 00800087  args 00000000 00000000 eebfdff0 00800058 00000000
         lib/libmain.c:26: libmain+78
  ebp eebfdff0  eip 00800031  args 00000000 00000000Incoming TRAP frame at 0xeffffe64
kernel panic at kern/trap.c:277: Kernel page fault at 0xeebfe000
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.                                                                                                                           
```

 When we call `backtrace` we get a `PAGE FAULT` in kernel mode. Notice that `0xeebfe000` is actually the value of USTACKTOP. The reason why we get a page fault is that we reached the top of the user stack.

> **Exercise 10.** Boot your kernel, running `user/evilhello`. The environment should be destroyed, and the kernel should not panic. You should see:
>
> ```
> 	[00000000] new env 00001000
> 	...
> 	[00001000] user_mem_check assertion failure for va f010000c
> 	[00001000] free env 00001000
> ```

Boot the kernel and run `user/evilhello`:

```bash
$ make run-evilhello-nox
make[1]: Entering directory '/mnt/d/Desktop/Desktop/JOS/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/mnt/d/Desktop/Desktop/JOS/lab'
/mnt/d/Desktop/Desktop/JOS/qemu_build/bin/qemu-system-x86_64 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
[00001000] user_mem_check assertion failure for va f010000c
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
```

Finally, we pass all tests:

```bash
divzero: OK (2.0s)
softint: OK (1.7s)
badsegment: OK (1.3s)
Part A score: 30/30

faultread: OK (1.6s)
faultreadkernel: OK (1.3s)
faultwrite: OK (1.8s)
faultwritekernel: OK (1.3s)
breakpoint: OK (1.7s)
testbss: OK (1.3s)
hello: OK (1.7s)
buggyhello: OK (1.2s)
buggyhello2: OK (1.8s)
evilhello: OK (1.2s)
Part B score: 50/50

Score: 80/80
```

