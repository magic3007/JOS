# JOS Lab 4

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751



## Part A: Multiprocessor Support and Cooperative Multitasking

> **Exercise 1.** Implement `mmio_map_region` in `kern/pmap.c`. To see how this is used, look at the beginning of `lapic_init` in `kern/lapic.c`. You'll have to do the next exercise, too, before the tests for `mmio_map_region` will run. 

In `kern/pmap.c`, just map the virtual address `[base, base+size)` to physical address `[pa,pa+size)`, and remember to check the boundary.

```c
void * mmio_map_region(physaddr_t pa, size_t size) {
	static uintptr_t base = MMIOBASE;
	uintptr_t upper;
	void *ret;
	upper = ROUNDUP(base + size, PGSIZE);
	if(upper > MMIOLIM)
		panic("mmio_map_region would overflow MMIOLIM");
	boot_map_region(kern_pgdir, base, upper - base, pa, PTE_PCD | PTE_PWT | PTE_W);
	ret = (void *) base;
	base = upper;
	return ret;
}
```

>  **Exercise 2.** Read `boot_aps()` and `mp_main()` in `kern/init.c`, and the assembly code in `kern/mpentry.S`. Make sure you understand the control flow transfer during the bootstrap of APs. Then modify your implementation of `page_init()` in `kern/pmap.c` to avoid adding the page at `MPENTRY_PADDR` to the free list, so that we can safely copy and run AP bootstrap code at that physical address. Your code should pass the updated `check_page_free_list()` test (but might fail the updated `check_kern_pgdir()` test, which we will fix soon). 

In `kern/pmap.c:page_init()`, avoid adding the physical page at `MPENTRY_PADDR` to the free list:

```c
void page_init(void) {
	size_t i;
	physaddr_t curupper = (physaddr_t)PADDR(boot_alloc(0));// next free page in physical memory

	page_free_list = NULL;

	for (i = 0; i < npages; i++) {
		physaddr_t ptr = i * PGSIZE; // the beginning physical address of page i

		pages[i].pp_ref = 0;
		
		if(ptr == MPENTRY_PADDR)	// the physical page at MPENTRY_PADDR is in use.
			continue;
		if((0 < i && i < npages_basemem)		// the rest of base memory is free.
			|| (ptr >= EXTPHYSMEM && ptr >= curupper)){			// free extended memory
			pages[i].pp_link = page_free_list;
			page_free_list = &pages[i];
		}
	}
}
```

> **Question**
>
> 1. Compare `kern/mpentry.S` side by side with `boot/boot.S`. Bearing in mind that `kern/mpentry.S` is compiled and linked to run above `KERNBASE` just like everything else in the kernel, what is the purpose of macro `MPBOOTPHYS`? Why is it necessary in `kern/mpentry.S` but not in `boot/boot.S`? In other words, what could go wrong if it were omitted in `kern/mpentry.S`? 
>    Hint: recall the differences between the link address and the load address that we have discussed in Lab 1.

1. To purpose of macro `MPBOOTPHYS` is to calculate actual addresses of its symbols when the code `kern/mpentry.S` is loaded at physical address `MPENTRY_PADDR`. Note that `kern/mpentry.S` is compiled and linked to run above `KERNBASE` just like everything else in the kernel(we can affirm it in `obj/kern/kernel.sym`). On the other hand, the symbols' addresses in `boot/boot.S` are at low addresses. 

   If it were omitted in `kern/mpentry.S`, notice that non-boot CPU ("AP") hasn't set up the the page translation mechanism yet, it will go wrong at the instruction `  lgdt  MPBOOTPHYS(gdtdesc)`.

>  **Exercise 3.** Modify `mem_init_mp()` (in `kern/pmap.c`) to map per-CPU stacks starting at `KSTACKTOP`, as shown in `inc/memlayout.h`. The size of each stack is `KSTKSIZE` bytes plus `KSTKGAP` bytes of unmapped guard pages. Your code should pass the new check in `check_kern_pgdir()`. 

```c
static void mem_init_mp(void)
{
	for(int i = 0; i < NCPU; i++){
		uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
		boot_map_region(kern_pgdir, kstacktop_i - KSTKSIZE, KSTKSIZE, PADDR(percpu_kstacks[i]), PTE_W);
	}
}
```

>  **Exercise 4.** The code in `trap_init_percpu()` (`kern/trap.c`) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs. (Note: your new code should not use the global `ts` variable any more.) 

```c
void trap_init_percpu(void){
	int i;

	i = cpunum();

	thiscpu->cpu_ts.ts_esp0 =  KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
	thiscpu->cpu_ts.ts_ss0 = GD_KD;
	thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

	// Initialize the TSS slot of the gdt.
	gdt[(GD_TSS0 >> 3) + i] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
					sizeof(struct Taskstate) - 1, 0);
	gdt[(GD_TSS0 >> 3) + i].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0 + (i<<3));

	// Load the IDT
	lidt(&idt_pd);
}
```

> What is the usage of `cpu_ts.ts_iomb`?

`cpu_ts.ts_iomb` is the address of I/O permission bit map. When we use sensitive IO instructions, like `IN`, `OUT` and etc, the processor first checks whether `CPL <= IOPL`.  If this condition is true, the I/O operation may proceed. If not true, the processor checks the I/O permission map.  The I/O map base field is 16 bits wide and contains the offset of the beginning of the I/O permission map. thus `cpu_ts.ts_iomb `should not less than `sizeof(*struct* Taskstate)`.

 ![img](lab4.assets/fig8-2.gif) 

Cited form  https://cs.nyu.edu/~mwalfish/classes/15fa/ref/i386/s08_03.htm.

>  **Exercise 5.** Apply the big kernel lock as described above, by calling `lock_kernel()` and `unlock_kernel()` at the proper locations. 

Just add `lock_kernel()` or `unlock_kernel()` as description. in `env_run()`, to avoid experiencing races or deadlocks, release big kernel lock after all the writing instruction  about shared objects between different processors.

```c
void env_run(struct Env *e){
	if(curenv && curenv->env_status == ENV_RUNNING)
		curenv->env_status = ENV_RUNNABLE;
	curenv = e;
	curenv->env_status = ENV_RUNNING;
	curenv->env_runs++;
	lcr3(PADDR(e->env_pgdir));
    unlock_kernel();
	env_pop_tf(&e->env_tf);	
}
```

> **Question**
>
> 2. It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.

A scenario is that when an exception/interrupt occurs, the hardware may automatically push `eflags`, `cs`, `eip` and etc.(see `inc/trap.h:struct Trapframe` for detail) into kernel stack without checking the big kernel lock, which is a behavior of software.

> **Exercise 6.** Implement round-robin scheduling in `sched_yield()` as described above. Don't forget to modify `syscall()` to dispatch `sys_yield()`.
>
> Make sure to invoke `sched_yield()` in `mp_main`.
>
> Modify `kern/init.c` to create three (or more!) environments that all run the program `user/yield.c`.
>
> Run `make qemu`. You should see the environments switch back and forth between each other five times before terminating, like below.
>
> Test also with several CPUS: `make qemu CPUS=2`.
>
> ```
> ...
> Hello, I am environment 00001000.
> Hello, I am environment 00001001.
> Hello, I am environment 00001002.
> Back in environment 00001000, iteration 0.
> Back in environment 00001001, iteration 0.
> Back in environment 00001002, iteration 0.
> Back in environment 00001000, iteration 1.
> Back in environment 00001001, iteration 1.
> Back in environment 00001002, iteration 1.
> ...
> ```
>
> After the `yield` programs exit, there will be no runnable environment in the system, the scheduler should invoke the JOS kernel monitor. If any of this does not happen, then fix your code before proceeding.

In `kern/sched.c`, implement the RR scheduling algorithm.

```c
void sched_yield(void)
{
	struct Env *idle;
	struct Env *env;
	int i;

	env = thiscpu->cpu_env;
	if(!env) env = envs + (NENV - 1);
	for(int i = 0; i < NENV; i++){
		env++;
		if(env == envs + NENV) env = envs;
		if(env->env_status == ENV_RUNNABLE)
			env_run(env);
	}
	if(thiscpu->cpu_env && thiscpu->cpu_env->env_status == ENV_RUNNING)
		env_run(thiscpu->cpu_env);

	// sched_halt never returns
	sched_halt();
}
```

Test with 2 CPUs, and the program acts as expected.

```bash
6828 decimal is 15254 octal!                                       
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!                                  
check_page_alloc() succeeded!                                      
check_page() succeeded!                                            
check_kern_pgdir() succeeded!                                      
check_page_free_list() succeeded!                                  
check_page_installed_pgdir() succeeded!                            
SMP: CPU 0 found 2 CPU(s)                                          
enabled interrupts: 1 2                                            
SMP: CPU 1 starting                                                
[00000000] new env 00001000                                        
[00000000] new env 00001001                                        
[00000000] new env 00001002                                        
Hello, I am environment 00001000.                                  
Hello, I am environment 00001001.                                  
Hello, I am environment 00001002.                                  
Back in environment 00001000, iteration 0.                         
Back in environment 00001001, iteration 0.                         
Back in environment 00001002, iteration 0.                         
Back in environment 00001000, iteration 1.                         
Back in environment 00001001, iteration 1.                         
Back in environment 00001002, iteration 1.                         
Back in environment 00001000, iteration 2.                         
Back in environment 00001001, iteration 2.                         
Back in environment 00001002, iteration 2.                         
Back in environment 00001001, iteration 3.                         
Back in environment 00001000, iteration 3.                         
Back in environment 00001002, iteration 3.                         
Back in environment 00001001, iteration 4.                         
Back in environment 00001000, iteration 4.                         
All done in environment 00001001.                                  
All done in environment 00001000.                                  
[00001001] exiting gracefully                                      
[00001001] free env 00001001                                       
[00001000] exiting gracefully                                      
[00001000] free env 00001000                                       
Back in environment 00001002, iteration 4.                         
All done in environment 00001002.                                  
[00001002] exiting gracefully                                      
[00001002] free env 00001002                                       
No runnable environments in the system!                            
Welcome to the JOS kernel monitor!                                 
Type 'help' for a list of commands.                                                              K>                                                                
```

> **Question**
>
> 3. In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch?
> 4. Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?

3. Notice that the virtual address pointed by `e` is located above `KERNBASE`, which is in the kernel space, thus the page tables used before and after `lcr3()` both translate this virtual address to the identical physical address.

   ```bash
   (gdb) b kern/env.c:575
   Breakpoint 1 at 0xf0103b64: file kern/env.c, line 575.
   (gdb) c
   Continuing.
   The target architecture is assumed to be i386
   => 0xf0103b64 <env_run+103>:    call   0xf01058bf <cpunum>
   
   Breakpoint 1, env_run (e=0xf02b0000) at kern/env.c:575
   575             curenv->env_runs++;
   (gdb) b kern/env.c:577
   Breakpoint 2 at 0xf0103bac: file kern/env.c, line 577.
   (gdb) p cpunum()
   $1 = 0
   (gdb) disp e
   1: e = (struct Env *) 0xf02b0000
   (gdb) c
   Continuing.
   => 0xf0103bac <env_run+175>:    mov    %ebx,(%esp)
   
   Breakpoint 2, env_run (e=0xf02b0000) at kern/env.c:578
   578             env_pop_tf(&e->env_tf);
   1: e = (struct Env *) 0xf02b0000
   ```
   
4. The purpose of saving and restoring the old environment context is to implement the context switching and multiprogramming technology, which could run program concurrently. 

   The context switching is achieved by the exception/interrupt mechanism. The storage of environment context takes places when an exception/interrupt happens.

   1. The `ss` and `esp` of the current CPU's kernel stack is stored at TSS segment(see `kern/trap.c:trap_init_percpu`),and the TSS selector is loaded into  task register by `ltr()`.
   2. When an exception/interrupt occurs, the processor firstly finds the location of the kernel stack(i.e., kernel stack's `ss` and `esp`) of current CPU by accessing the TSS segment, and then push the old registers `ss`, `esp`, `eflags`, `cs`, `eip`  and error code onto kernel stack by hardware.
   3. Then the processor push the exception/interrupt number, `ds`, `es` and all the general registers into kernel stack by software.(in `kern/trapentry.S`).
   4. If we the switch from user-mode to kernel mode, we also need to copy trap frame (which is currently on the kernel stack) into the environment array `envs[]` , so that running the environment, will restart at the trap point.
   
   By this routine, the environment context is stored in the environment array.
   
   On the other hand, he recovery of context switching happens in `kern/env.c:env_run` and `kern/env.c:env_pop_tf`, which restores the general registers, `es`, `ds` by software instruction, and `eip`, `cs`, `eflags`, `esp`, `ss` by `iret`.
   

> *Challenge!* Add a less trivial scheduling policy to the kernel, such as a fixed-priority scheduler that allows each environment to be assigned a priority and ensures that higher-priority environments are always chosen in preference to lower-priority environments. If you're feeling really adventurous, try implementing a Unix-style adjustable-priority scheduler or even a lottery or stride scheduler. (Look up "lottery scheduling" and "stride scheduling" in Google.)
>
> Write a test program or two that verifies that your scheduling algorithm is working correctly (i.e., the right environments get run in the right order). It may be easier to write these test programs once you have implemented `fork()` and IPC in parts B and C of this lab.

Add the attribute `env_priority` to `Struct Env`(in `inc/env.h`):

```c++
struct Env {
	struct Trapframe env_tf;	// Saved registers
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run
	int env_cpunum;			// The CPU that the env is running on
	int env_priority; 		// The priority of current enviroment


	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir

	// Exception handling
	void *env_pgfault_upcall;	// Page fault upcall entry point

	// Lab 4 IPC
	bool env_ipc_recving;		// Env is blocked receiving
	void *env_ipc_dstva;		// VA at which to map received page
	uint32_t env_ipc_value;		// Data value sent to us
	envid_t env_ipc_from;		// envid of the sender
	int env_ipc_perm;		// Perm of page mapping received
};
```

Use system call to modify the priority(in `kern/syscall.c`):

```c
static int
sys_env_set_priority(envid_t envid, int priority){
	int rc;
	struct Env *env;
	
	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	env->env_priority = priority;
	return 0;
}
```

Implement fixed-priority scheduling in `kern/sched.c`:

```c
struct Env *env;
	int highest_priority = 0, i;

	for(int i = 0; i < NENV; i++)
		if(envs[i].env_status == ENV_RUNNABLE && envs[i].env_priority > highest_priority)
			highest_priority = envs[i].env_priority;
	
	env = thiscpu->cpu_env;
	if(!env) env = envs + (NENV - 1);
	for(int i = 0; i < NENV; i++){
		env++;
		if(env == envs + NENV) env = envs;
		if(env->env_status == ENV_RUNNABLE && env->env_priority == highest_priority)
			env_run(env);
	}

	if(thiscpu->cpu_env && thiscpu->cpu_env->env_status == ENV_RUNNING)
		env_run(thiscpu->cpu_env);

	// sched_halt never returns
	sched_halt();
```

We also implement a test program or two that verifies that your scheduling algorithm(in `user/fixscheduling.c`), and the program works as expected.

```c
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
```

>  *Challenge!* The JOS kernel currently does not allow applications to use the x86 processor's x87 floating-point unit (FPU), MMX instructions, or Streaming SIMD Extensions (SSE). Extend the `Env` structure to provide a save area for the processor's floating point state, and extend the context switching code to save and restore this state properly when switching from one environment to another. The `FXSAVE` and `FXRSTOR` instructions may be useful, but note that these are not in the old i386 user's manual because they were introduced in more recent processors. Write a user-level test program that does something cool with floating-point. 

 The `FXSAVE` and `FXRSTOR` instructions save and restore a 512-byte data structure, the first byte of which must be aligned on a 16-byte boundary.

In `inc/trap.h`, add the save area of the x87 FPU, MMX, XMM, and MXCSR register state into struct `Trapframe`:

```c
struct Trapframe {
	char fxsave_region[512]; // the save area of the x87 FPU, MMX, XMM, and MXCSR register state.
	struct PushRegs tf_regs;
	uint16_t tf_es;
```

In `kern/trapentry.S:_alltraps` and `kern/env.c:env_pop_tf`, save and restore the floating register state during context switching:

```c
void
env_pop_tf(struct Trapframe *tf)
{
	// Record the CPU we are running on for user-space debugging
	curenv->env_cpunum = cpunum();

	asm volatile(
		"\tmovl %0,%%esp\n"
		"\tmovl %%esp, %%eax\n"
		"\taddl $0xf, %%eax\n"
		"\tandl $0xfffffff0, %%eax\n"
		"\tfxrstor (%%eax)\n"
		"\taddl $528, %%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret\n"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}
```

```asm
_alltraps:
	/* push %ds and %es register on kernel stack */
	pushl %ds;
	pushl %es;

	/* push all general-purpose registers */
	pushal;

	/* push floating registers state */
	subl $528, %esp;		/* allocate 512+16 bytes for alignment */
	movl %esp, %eax; 		/* 16-byte alignment */
	addl $0xf, %eax;		/* round up */
	andl $0xfffffff0, %eax; 
	fxsave (%eax);

	/* assign %ds and %es to kernel data segment selector */
	movl $GD_KD, %eax;
	movw %ax, %ds;
	movw %ax, %es;
	
	/* pass a pointer to the Trapframe as an argument to trap() */
	pushl %esp
	
    /* call trap() and never return */
	call trap
```



> ​    **Exercise 7.** Implement the system calls described above in `kern/syscall.c` and make sure `syscall()` calls them. You will need to use various functions in `kern/pmap.c` and `kern/env.c`, particularly `envid2env()`. For now, whenever you call `envid2env()`, pass 1 in the `checkperm` parameter. Be sure you check for any invalid system call arguments, returning `-E_INVAL` in that case. Test your JOS kernel with `user/dumbfork` and make sure it works before proceeding. 


Follow the guide in `kern/syscall.c` and fill in the function ` sys_exofork `, ` sys_env_set_status `, `sys_page_alloc`, `sys_page_map` and `sys_page_unmap`. And then add their dispatchers in `kern/syscall.c:syscall`. 

One tricky thing in `sys_exofork` is that how we achieve the the goal that the parent process and the child process get different return value of `sys_exofork`. Notice that it is the register`%eax` that holds the return value.in `syscall.c:syscall`:

```c
static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;
    
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

So we modify the stored `%eax` value in the trap frame of new environment. By this mean, the modified return value will be returned in child environment as mention above.

in `kern/syscall.c`:

```c
static envid_t sys_exofork(void){
	envid_t rc = 0, parent_id;
	struct Env *child_env;

	assert(curenv);
	parent_id = curenv->env_id;
	if((rc = env_alloc(&child_env, parent_id)) < 0)
		return rc;
	child_env->env_status = ENV_NOT_RUNNABLE;
	child_env->env_tf = curenv->env_tf;
	child_env->env_tf.tf_regs.reg_eax = 0; // tweaked so sys_exofork will return 0 for child process
	return child_env->env_id;
}
```

## Part B: Copy-on-Write Fork

>  **Exercise 8.** Implement the `sys_env_set_pgfault_upcall` system call. Be sure to enable permission checking when looking up the environment ID of the target environment, since this is a "dangerous" system call. 

When we call the function `sys_env_set_pgfault_upcall` , it will return `-E_BAD_ENV` if the caller doesn't have permission to look up the environment.

```c
static int sys_env_set_pgfault_upcall(envid_t envid, void *func){
	struct Env *env;
	int rc;
	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	env->env_pgfault_upcall = func;
	return 0;
}
```

>  **Exercise 9.** Implement the code in `page_fault_handler` in `kern/trap.c` required to dispatch page faults to the user-mode handler. Be sure to take appropriate precautions when writing into the exception stack. (What happens if the user environment runs out of space on the exception stack?) 

In `kern/trap.c:page_fault_handler`, if the environment's page fault upcall doesn't exist, or this page fault happen just with in the user exception stack, go to destroy this environment.

```c
	if(!curenv->env_pgfault_upcall){
		cprintf("Page fault upcall doesn't exist!.\n");
		goto done;
	}

	if(UXSTACKTOP - PGSIZE <= fault_va  && fault_va < UXSTACKTOP){
		cprintf("Page fault happens within the user exception stack.\n");
		goto done;
	}
	
......
    
    done:
	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}
```

The location of new `Utrapframe` depends on whether this page fault happens during trap-time state. In the recursive case, we have to leave an extra word between the current top of the exception stack.

```c
if((curenv->env_tf.tf_esp < UXSTACKTOP) && (curenv->env_tf.tf_esp >= UXSTACKTOP - PGSIZE))
		utrapframe = (struct UTrapframe*)(curenv->env_tf.tf_esp - 4 - sizeof(struct UTrapframe));
	else
		utrapframe = (struct UTrapframe*)(UXSTACKTOP - sizeof(struct UTrapframe));
```

if the exception stack overflows, destroy this environment. Modify the `eip` and `esp` of `curenv->env_tf`, so as to trap into user-level page fault upcall by `env_run`.

```c
	user_mem_assert(curenv, utrapframe, sizeof(struct UTrapframe), PTE_W);

	utrapframe->utf_esp = curenv->env_tf.tf_esp;
	utrapframe->utf_eflags = curenv->env_tf.tf_eflags;
	utrapframe->utf_eip = curenv->env_tf.tf_eip;
	utrapframe->utf_regs = curenv->env_tf.tf_regs;
	utrapframe->utf_err = curenv->env_tf.tf_err;
	utrapframe->utf_fault_va = fault_va;
	
	/*
	* Modify the eip and esp of `curenv->env_tf`, so as to trap 
	* into user-level page fault upcall by `env_run`
	*/
	curenv->env_tf.tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
	curenv->env_tf.tf_esp = (uintptr_t)utrapframe;

	env_run(curenv); // never return
```




>  **Exercise 10.** Implement the `_pgfault_upcall` routine in `lib/pfentry.S`. The interesting part is returning to the original point in the user code that caused the page fault. You'll return directly there, without going back through the kernel. The hard part is simultaneously switching stacks and re-loading the EIP. 

in `lib/pfentry.S`, we restore one word space in the trap-time stack, and fill it with the return address. Meanwhile, the stack pointer `utf_esp` needs to be modified. By this mean, we can simply return to re-execute the instruction the faulted by `ret`.

```asm
	subl $4, 0x30(%esp)		// tf_esp minus 4, so as to reserve the space that hold the return address
	movl 0x30(%esp), %ebx 	// move modified %esp to %ebx
	movl 0x28(%esp), %eax 	// move trap-time %eip to %eax
	movl %eax, (%ebx)		// move trap-time %eip to the top of trap-time stack.

	// Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here.
	addl $8, %esp		// skip added utf_fault_va and utf_err
	popal				// restore general-purpose registers

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	addl $4, %esp 		// skip utf_eip
	popfl				// restore eflags


	// Switch back to the adjusted trap-time stack.
	popl %esp 			// pop modified utf_esp to %esp

	// Return to re-execute the instruction that faulted.
	ret					// pop the return address from the trap stack 
```

>  **Exercise 11.** Finish `set_pgfault_handler()` in `lib/pgfault.c`. 

in `lib/pgfault.c`, set `_pgfault_handler` passed by user and page fault upcall simultaneously.

```c
void set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		// panic("set_pgfault_handler not implemented");
		if((r = sys_page_alloc(0, (void*)(UXSTACKTOP - PGSIZE), PTE_SYSCALL)) < 0)
			panic("set_pgfault_handler: sys_page_alloc error. %e\n", r);
		if((r = sys_env_set_pgfault_upcall(0, _pgfault_upcall)) < 0)
			panic("set_pgfault_handler: sys_env_set_pgfault_upcall error. %e\n", r);
	}
	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
```

> **Exercise 12.** Implement `fork`, `duppage` and `pgfault` in `lib/fork.c`.
>
> Test your code with the `forktree` program. It should produce the following messages, with interspersed 'new env', 'free env', and 'exiting gracefully' messages. The messages may not appear in this order, and the environment IDs may be different.
>
> ```
> 	1000: I am ''
> 	1001: I am '0'
> 	2000: I am '00'
> 	2001: I am '000'
> 	1002: I am '1'
> 	3000: I am '11'
> 	3001: I am '10'
> 	4000: I am '100'
> 	1003: I am '01'
> 	5000: I am '010'
> 	4001: I am '011'
> 	2002: I am '110'
> 	1004: I am '001'
> 	1005: I am '111'
> 	1006: I am '101'
> ```

In `lib/fork.c:pgfault`, firstly we check that the faulting access was a write, and is to a copy-on-write page. Only under this situation could we copy this page. Then we allocate a new page, map it at PFTEMP, and remap the allocated page to the old page's address.

```c
static void pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	pte_t pte;
	int rc;
	
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	pte = uvpt[PGNUM(addr)];
	if(!(FEC_WR & err))
		panic("In pgfault: the faulting access is not a write.");
	if(!(pte & PTE_COW))
		panic("In pgfault: the faulting access is not to a copy-on-write page.");
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// allocate a new page, map it at PFTEMP
	if((rc = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W)) < 0)
		panic("In pgfault: sys_page_alloc error. %e", rc);

	// copy the data from the old page to the new page
	memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	// remap the allocated page to the old page's address
    // decrease the reference counter instead of directly free it.
	if((rc = sys_page_map(0, PFTEMP, 0, 
		ROUNDDOWN(addr, PGSIZE), PTE_P | PTE_U | PTE_W))<0)
			panic("In pgfault: sys_page_map error. %e", rc);

}
```

In `lib/fork.c:duppage`, map our virtual page into the target environment. If the page is writable or copy-on-write, the new mapping must be created copy-on-write, and then our mapping must be marked copy-on-write as well. 

> Why do we need to mark ours copy-on-write again if it was already copy-on-write?

The reason why do we need to mark ours copy-on-write again if it was already copy-on-write is that 

> We should map the page copy-on-write into the address space of the child and then *remap* the page copy-on-write in its own address space.  The ordering here (i.e., marking a page as COW in the child before marking it in the parent) actually matters! Can you see why? 

The order is significant.  Notice that `sys_page_map` will call `page_remove` after several calls to give up exciting mapping, which will decrease the reference counter of the old page. However, If we map the page copy-on-write into the address space of the parent firstly and the reference counter of this page is 1, this old page will be free. Thus we need to increase the reference counter of this page by mapping the page copy-on-write into the address space of the child firstly.

```c
static int cduppage(envid_t envid, unsigned pn){
	int r;
	pte_t pte;
	void *addr  = (void*)(pn << PGSHIFT);

	pte = uvpt[pn];
	if((pte & PTE_W) || (pte & PTE_COW)){
		if((r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P | PTE_COW))<0)
			return r;
		if((r = sys_page_map(0, addr, 0, addr, PTE_U | PTE_P | PTE_COW))<0)
			return r;
	}
	else{
		if((r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P))<0)
			return r;
	}
	return 0;
}
```

In `lib/fork.c:fork`, for each writable or copy-on-write page in its address space below UTOP, the parent map the page copy-on-write into the address space of the child. Notice that the page fault handler which is called by page fault upcall are in the form of function pointer, thus we can simply set the function  pointer of page fault upcall in `envs[]` arrays.

```c
envid_t fork(void){
	// LAB 4: Your code here.
	int rc = -1;
	envid_t envid;
	int pdi, pti, pn;

	// Set up our page fault handler
	set_pgfault_handler(pgfault);

	// Create a child.
	if((envid = sys_exofork()) < 0) return envid;
	
	// children environment
	if(envid==0){
		// fix thisenv
		thisenv=&envs[ENVX(sys_getenvid())];
		return 0;
	}

	// Copy parent's address space
	for(pdi = 0; pdi < PDX(UTOP); pdi++){
		if(!(uvpd[pdi] & PTE_P))
			continue;
		for(pti = 0; pti < NPDENTRIES; pti++){

			// page index
			pn = (pdi << 10) + pti;

			// user exception stack should ever be marked copy-on-write
			if(pn == PGNUM(UXSTACKTOP  - PGSIZE)){
				if((rc = sys_page_alloc(envid, (void*)(pn * PGSIZE), 
					PTE_P | PTE_U | PTE_W)) < 0)
						goto destroy;
				continue;
			}

			if(uvpt[pn] & PTE_P)
				if((rc = duppage(envid, pn))<0) goto destroy;
		}
	}
	
	
	/*
	* set page fault upcall for child.
	*/
	extern void _pgfault_upcall (void);
	if((rc = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		goto destroy;
	
	// mark the child as runnable
	if((rc =sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		goto destroy;

	rc = envid;
	goto done;
destroy:
	sys_env_destroy(envid);
done:
	return rc;
}
```

> What is principle of `UVPT` and `UVPD`?

`UVPT` and `UVPD` is a trick named *Page Table Self Mapping*. In JOS, the page directory is the `0x3BD` in virtual memory.

- `UVPT = 0x3BD<<22=0xEF400000`

- `UVPD=(0x3BD<<22 | 0x3BD<<12)=UVPT | (UVPT>>10) = ‭0xEF40EF40000‬`
- What is the physical address of page directory?
  - The virtual address that contains page directory's physical address is `0xef7bdef4 = [PDX(UVPT), PDX(UVPT), PDX(UVPT), 00] = UVPT + (UVPT >> 10) + (UVPT >> 20)`
- What is the physical address of page directory of `va`?
  - The virtual address the contains `va`'s physical address in page directory is `[PDX(UVPT), PDX(UVPT), PDX(va), 00]`
  - i.e. `[PGNUM(UVPD), PDX(va), 00]`
- What is the physical address of page table of `va`?
  - The virtual address the contains `va`'s physical address in page table is `[PDX(UVPT), PDX(va), PTX(va),00]`
- The PTE for page number N is stored in `uvpt[N]`
- The PDE for the Nth entry in page directory is `uvpd[N]`

>  *Challenge!* Implement a shared-memory `fork()` called `sfork()`. This version should have the parent and child *share* all their memory pages (so writes in one environment appear in the other) except for pages in the stack area, which should be treated in the usual copy-on-write manner. Modify `user/forktree.c` to use `sfork()` instead of regular `fork()`. Also, once you have finished implementing IPC in part C, use your `sfork()` to run `user/pingpongs`. You will have to find a new way to provide the functionality of the global `thisenv` pointer. 

`sfork()` is similar to `fork()` except for the normal user stack, which should by treated in usual copy-on-write manner. As for user exception stack, we also act as `fork()`.

```c
envid_t sfork(void){
	int rc = -1;
	envid_t envid;
	int pdi, pti, pn;
	uintptr_t addr;

	// Set up our page fault handler
	set_pgfault_handler(pgfault);

	// Create a child.
	if((envid = sys_exofork()) < 0) return envid;
	
	// children environment
	if(envid==0){
		// fix thisenv
		thisenv=&envs[ENVX(sys_getenvid())];
		return 0;
	}

	// Copy parent's address space
	// Pages in normal user stack area should be copy-on-write.
	int is_stackarea = 1;
	for(addr = USTACKTOP - PGSIZE; addr>=UTEXT; addr-=PGSIZE){
		// find a page mapping 
		if((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U)){
			if((rc = shared_duppage(envid, PGNUM(addr), is_stackarea))<0)
				goto destroy;
		}else{
			is_stackarea = 0;
		}
	}

	// user exception stack should never be marked copy-on-write
	if((rc = sys_page_alloc(envid, (void*)(UXSTACKTOP  - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		goto destroy;

	extern void _pgfault_upcall (void);

	if((rc = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		goto destroy;
	
	// mark the child as runnable
	if((rc =sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		goto destroy;

	rc = envid;
	goto done;
destroy:
	sys_env_destroy(envid);
done:
	return rc;
}
```

At the same, we also adjust `duppage()` to a modified version `shared_duppage()`. If this page is within the normal user stack or is marked COW, `shared_duppage()` acts the same as `duppage()`. Otherwise, just map it as usual.

```c
static int shared_duppage(envid_t envid, unsigned pn, int is_cow){
	int r;
	pte_t pte = uvpt[pn];
	void *addr  = (void*)(pn << PGSHIFT);

	// If is_cow is true or the source page is cow, act the same as duppage.
	if(is_cow || (pte&PTE_COW))
		return duppage(envid, pn);
	
	// child environment should have the same permission as the parent environment.
	if((r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P | (pte & PTE_W)))<0)
		return r;
	
	return 0;
}
```

The we run `make run-pingpongs-nox`, and get following result:

````bash
[00000000] new env 00001000                                                                   
[00001000] new env 00001001                                                                   
i am 00001000; thisenv is 0xeec00000                                                          
send 0 from 1000 to 1001                                                                      
1001 got 0 from 1000 (thisenv is 0xeec0007c 1001)                                             
1000 got 1 from 1000 (thisenv is 0xeec0007c 1001)                                             
[00001000] user panic in <unknown> at lib/ipc.c:51: ipc_send: cannot send message to itself.  
Welcome to the JOS kernel monitor!                                                            
Type 'help' for a list of commands.                                                           
TRAP frame at 0xf02be000 from CPU 0                                                           
  edi  0x00000000                                                                             
  esi  0x00801596                                                                             
  ebp  0xeebfdf50                                                                             
  oesp 0xefffffdc                                                                             
  ebx  0xeebfdf64                                                                             
  edx  0xeebfde08                                                                             
  ecx  0x00000001                                                                             
  eax  0x00000001                                                                             
  es   0x----0023                                                                             
  ds   0x----0023                                                                             
  trap 0x00000003 Breakpoint                                                                  
  err  0x00000000                                                                             
  eip  0x0080120c                                                                             
  cs   0x----001b                                                                             
  flag 0x00000286                                                                             
  esp  0xeebfdf48                                                                             
  ss   0x----0023                                                                             
K>                                                                                                                          

````

The reason why it is blocked is that the address of `thisenv` is `0x802008`, which is located in `Program Data & Heap`(see `inc/memlayout.h`),this variable is shared between parent and child. 

## Part C: Preemptive Multitasking and Inter-Process communication (IPC)

> **Exercise 13.** Modify `kern/trapentry.S` and `kern/trap.c` to initialize the appropriate entries in the IDT and provide handlers for IRQs 0 through 15. Then modify the code in `env_alloc()` in `kern/env.c` to ensure that user environments are always run with interrupts enabled.
>
> Also uncomment the `sti` instruction in `sched_halt()` so that idle CPUs unmask interrupts.
>
> The processor never pushes an error code when invoking a hardware interrupt handler. You might want to re-read section 9.2 of the [80386 Reference Manual](https://pdos.csail.mit.edu/6.828/2018/readings/i386/toc.htm), or section 5.8 of the[IA-32 Intel Architecture Software Developer's Manual, Volume 3](https://pdos.csail.mit.edu/6.828/2018/readings/ia32/IA32-3A.pdf), at this time.
>
> After doing this exercise, if you run your kernel with any test program that runs for a non-trivial length of time (e.g., `spin`), you should see the kernel print trap frames for hardware interrupts. While interrupts are now enabled in the processor, JOS isn't yet handling them, so you should see it misattribute each interrupt to the currently running user environment and destroy it. Eventually it should run out of environments to destroy and drop into the monitor.

In `kern/trapentry.S` and `kern/trap.c`, register these hardware interrupts as what we have done before.

```c
......
    
#define SET_IRQ_GATE(IRQ_NUM) \
	SETGATE(idt[IRQ_OFFSET+IRQ_NUM], 0, GD_KT, int_funs[IRQ_OFFSET+IRQ_NUM], 0)

	SET_IRQ_GATE(IRQ_TIMER);
	SET_IRQ_GATE(IRQ_KBD);
	SET_IRQ_GATE(IRQ_SERIAL);
	SET_IRQ_GATE(IRQ_SPURIOUS);
	SET_IRQ_GATE(IRQ_IDE);
	SET_IRQ_GATE(IRQ_ERROR);

#undef SET_IRQ_GATE

	
	// Per-CPU setup 
	trap_init_percpu();
}
```

> **Exercise 14.** Modify the kernel's `trap_dispatch()` function so that it calls `sched_yield()` to find and run a different environment whenever a clock interrupt takes place.
>
> You should now be able to get the `user/spin` test to work: the parent environment should fork off the child, `sys_yield()` to it a couple times but in each case regain control of the CPU after one time slice, and finally kill the child environment and terminate gracefully.

Handle clock interrupts in `kern/trap.c:trap_dispatch`. Remember to acknowledge the interrupt using `lapic_eoi()` before calling the scheduler.

```c
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER){
		lapic_eoi();
		sched_yield(); // never return
	}
```

> **Exercise 15.** Implement `sys_ipc_recv` and `sys_ipc_try_send` in `kern/syscall.c`. Read the comments on both before implementing them, since they have to work together. When you call `envid2env` in these routines, you should set the `checkperm` flag to 0, meaning that any environment is allowed to send IPC messages to any other environment, and the kernel does no special permission checking other than verifying that the target envid is valid.	
>
> Then implement the `ipc_recv` and `ipc_send` functions in `lib/ipc.c`.
>
> Use the `user/pingpong` and `user/primes` functions to test your IPC mechanism. `user/primes` will generate for each prime number a new environment until JOS runs out of environments. You might find it interesting to read `user/primes.c` to see all the forking and IPC going on behind the scenes.

Generally Speaking, `sys_ipc_try_send` could send either a page or a value, depending on whether `srcva` is less than than `UTOP`, and `syc_ipc_recv` vice versa. 

In `kern/syscall.c:sys_ipc_recv`, it will be blocked while waiting for the massage, so we marks the receiver status as not runnable, and give up the CPU. On the other hand, the receiver will be waken up by the sender by remarking the status as runnable and return 0. We could modify its environment context and make it look like it's going to return 0. 

```c
static int sys_ipc_recv(void *dstva){
	int rc;

	if((uintptr_t)dstva < UTOP && (uintptr_t)dstva%PGSIZE!=0)
		return -E_INVAL;
	
	curenv->env_ipc_recving = true;
	curenv->env_status = ENV_NOT_RUNNABLE;
	
	// If 'dstva' is < UTOP, then you are willing to receive a page of data.
	if((uintptr_t)dstva < UTOP)
		curenv->env_ipc_dstva = dstva;
	
	curenv->env_tf.tf_regs.reg_eax = 0; // return 0 on success when the sender remarks the receiver runnable.
	sys_yield(); // nenver return
	return 0;
}
```

As for `kern/syscall.c:sys_ipc_try_send`, most of the errors could be detected by `sys_page_map`, except for one thing that it doesn't need to check the permissions. This we use s modified version of  `sys_page_map` without checking permissions.

Notice that only one program could run in kernel mode, so there is no race condition in system call call function.

```c
static int sys_page_map_withoutcheckperm(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm){
	int rc;
	struct Env *src_env, *dst_env;
	uintptr_t src_addr = (uintptr_t)srcva, dst_addr = (uintptr_t)dstva;
	struct PageInfo *pp;
	pte_t *pte;
	static const int ness_perm = PTE_U | PTE_P;
	
	if((rc = envid2env(srcenvid, &src_env, 0)) < 0) return rc;
	if((rc = envid2env(dstenvid, &dst_env, 0)) < 0) return rc;
	if(src_addr>=UTOP || src_addr%PGSIZE!=0) return -E_INVAL;
	if(dst_addr>=UTOP || dst_addr%PGSIZE!=0) return -E_INVAL;
	if((perm & ness_perm)!=ness_perm || (perm & PTE_SYSCALL)!=perm) return -E_INVAL;
	if((pp = page_lookup(src_env->env_pgdir, srcva, &pte)) == NULL) return -E_INVAL;
	if((perm & PTE_W) && !(*pte & PTE_W)) return -E_INVAL;
	if((rc=page_insert(dst_env->env_pgdir, pp, dstva, perm))<0) return rc;
	return 0;
}


static int sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm){
	int rc;
	struct Env *env;
	static const int ness_perm = PTE_U | PTE_P;
	
	// return -E_BAD_ENV if environment envid doesn't currently exist.
	if((rc = envid2env(envid, &env, 0))<0)
		goto done;
	
	/*
	* return -E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
	* or another environment managed to send first.
	*/
	if(!env->env_ipc_recving)
		return -E_IPC_NOT_RECV;
	
	// If 'srcva' is < UTOP, then you are willing to send a page of data.
	if((uintptr_t)srcva < UTOP){
		/* 
		* As for the error return of `sys_page_map`:
		* return -E_INVAL if srcva < UTOP but srcva is not mapped in the caller's address space.
		* return -E_INVAL if (perm & PTE_W), but srcva is read-only in the current environment's address space.
		* return -E_INVAL if srcva < UTOP but srcva is not page-aligned.
		* return -E_INVAL if srcva < UTOP and perm is inappropriate (see sys_page_alloc).
		* return -E_NO_MEM if there's not enough memory to map srcva in envid's address space.
		*/
		if((rc = sys_page_map_withoutcheckperm(0, srcva, envid, env->env_ipc_dstva, perm))<0)
			goto done;
		env->env_ipc_perm = perm;
	}else{
		env->env_ipc_perm = 0;
	}

	// change env->env_ipc_recving if on success.
	env->env_ipc_recving = false;
	env->env_ipc_from = curenv->env_id;
	env->env_ipc_value = value;
	env->env_status = ENV_RUNNABLE;
	rc = 0;
done:
	return rc;
}
```

In `lib/ipc.c`, `ipc_recv` and `ipc_send` provide a API for IPC. One thing needs to be mentioned is that `ipc_send` could not send message to itself.

```c
int32_t ipc_recv(envid_t *from_env_store, void *pg, int *perm_store){
	int rc;
	envid_t env_store = 0;
	int perm = 0;

	rc = sys_ipc_recv(pg==NULL?(void*)UTOP:pg);
	
	if(rc>=0){
		env_store = thisenv->env_ipc_from;
		perm = thisenv->env_ipc_perm;
	}

	if (from_env_store) *from_env_store =  env_store;
	if (perm_store)  *perm_store = perm;
	return rc>=0 ? thisenv->env_ipc_value : rc;
}

void ipc_send(envid_t to_env, uint32_t val, void *pg, int perm){
	if(to_env == sys_getenvid())
		panic("ipc_send: cannot send message to itself.");
	int rc = -1;
	while(rc < 0){
		rc = sys_ipc_try_send(to_env, val, pg==NULL?(void*)UTOP:pg, perm);
		if(rc == 0) return;
		if(rc == -E_IPC_NOT_RECV)
			sys_yield();
		else{
			panic("In ipc_send: sys_ipc_try_send error. %e", rc);
		}	
	}
}

```



Finally we pass all the tests and finish three challenges.

```bash
dumbfork: OK (1.7s)
Part A score: 5/5

faultread: OK (0.9s)
faultwrite: OK (1.1s)
faultdie: OK (0.9s)
faultregs: OK (1.0s)
faultalloc: OK (1.0s)
faultallocbad: OK (1.7s)
faultnostack: OK (2.4s)
faultbadhandler: OK (1.9s)
faultevilhandler: OK (0.9s)
forktree: OK (1.8s)
Part B score: 50/50

spin: OK (1.3s)
stresssched: OK (1.9s)
sendpage: OK (1.0s)
pingpong: OK (1.6s)
primes: OK (34.8s)
Part C score: 25/25

Score: 80/80
```

