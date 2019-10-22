# Report for Lab 3

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751

## Part A: User Environments and Exception Handling

### Allocating the Environments Array

> **Exercise 1.** Modify `mem_init()` in `kern/pmap.c` to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in `inc/memlayout.h`) so user processes can read from this array.
>
> You should run your code and make sure `check_kern_pgdir()` succeeds.

In `kern/pmap.c`, malloc physical memory for `env` and and map this physical memory region to `[UENVS, UENVS+PTSIZE)`. 

```c
	// Make 'envs' point to an array of size 'NENV' of 'struct Env'.
	// LAB 3: Your code here.
	envs = (struct Env *)boot_alloc(sizeof(struct Env) * NENV);
```

```c
	// Use the physical memory that 'bootstack' refers to as the kernel	
	// Map the 'envs' array read-only by the user at linear address UENVS
	// (ie. perm = PTE_U | PTE_P).
	// Permissions:
	//    - the new image at UENVS  -- kernel R, user R
	//    - envs itself -- kernel RW, user NONE
	// LAB 3: Your code here.
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
Note that the page directory of each environment has different permissions.
```c
static int env_setup_vm(struct Env *e){
	int i;
	struct PageInfo *p = NULL;

	// Allocate a page for the page directory
	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	// increment env_pgdir's pp_ref for env_free to work correctly.	
	p->pp_ref++;

	e->env_pgdir = (pte_t *)page2kva(p);

	// user premissions: R-
	for(size_t i = PDX(UTOP); i < PDX(ULIM); i++)
		e->env_pgdir[i] = (kern_pgdir[i] & ~0x3FF) | PTE_U | PTE_P;
	
	// user permissions: --
	for(size_t i = PDX(ULIM); i < NPDENTRIES; i++)
		e->env_pgdir[i] = (kern_pgdir[i] & ~0x3FF) | PTE_P;
	
	// UVPT maps the env's own page table read-only.
	// Permissions: kernel R, user R
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

	return 0;
}

​```c
static void region_alloc(struct Env *e, void *va, size_t len)
{
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

Notice that we must switch the page directory before using `memset` and `memmove`.

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


	for(;ph < eph; ph++){
		if(ph->p_type == ELF_PROG_LOAD){
			va = ph->p_va;
			memsz = ph->p_memsz;
			filesz = ph->p_filesz;
			offset = ph->p_offset;
		}
	}

	// switch to the environment page directory
	lcr3(PADDR(e->env_pgdir));

	for(;ph < eph; ph++){
		if(ph->p_type == ELF_PROG_LOAD){
			va = ph->p_va;
			memsz = ph->p_memsz;
			filesz = ph->p_filesz;
			offset = ph->p_offset;

			cprintf("va = %p, memsz = %u, filesz = %u, offset = %u\n", va, memsz, filesz, offset);

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
void env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.
	int rc;
	struct Env *env;

	if((rc = env_alloc(&env, 0)) < 0)
		panic("env_malloc: %e", rc);
	
	load_icode(env, binary);
	env->env_type = type;
	
}
```

```c
void env_run(struct Env *e)
{
	if(e->env_status == ENV_RUNNING){
		if(curenv && curenv->env_status == ENV_RUNNING)
			curenv->env_status = ENV_RUNNABLE;
		curenv = e;
		curenv->env_status = ENV_RUNNING;
		e->env_runs++;
		lcr3(PADDR(e->env_pgdir));
	}
	
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





> **Questions**
>
> Answer the following questions in your `answers-lab3.txt`:
>
> 1. What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)
> 2. Did you have to do anything to make the `user/softint` program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says `int $14`. *Why* should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?





>  *Challenge!* You probably have a lot of very similar code right now, between the lists of `TRAPHANDLER` in `trapentry.S` and their installations in `trap.c`. Clean this up. Change the macros in `trapentry.S` to automatically generate a table for `trap.c` to use. Note that you can switch between laying down code and data in the assembler by using the directives `.text` and `.data`. 


http://www.cs.uwm.edu/classes/cs315/Bacon/Lecture/HTML/ch12s04.html


## Part B: Page Faults, Breakpoints Exceptions, and System Calls