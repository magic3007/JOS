/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

#define UTEMP2USTACK(addr)	((void*) (addr) + (USTACKTOP - PGSIZE) - UTEMP)
#define UTEMP2			(UTEMP + PGSIZE)
#define UTEMP3			(UTEMP2 + PGSIZE)

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
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

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int	
sys_cgetc(void)
{
	return cons_getc();
}



// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
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

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	// panic("sys_env_set_status not implemented");
	int rc;
	struct Env *env;
	
	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	if(status != ENV_NOT_RUNNABLE && status != ENV_RUNNABLE) return -E_INVAL;
	env->env_status = status;
	return 0;
}

static int
sys_env_set_priority(envid_t envid, int priority){
	int rc;
	struct Env *env;
	
	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	env->env_priority = priority;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	int r;
	struct Env *env;

	if((r = envid2env(envid, &env, true)) < 0)
		return r;
	
	user_mem_assert(env, tf, sizeof(struct Trapframe), 0);

	env->env_tf = *tf;
	env->env_tf.tf_cs = GD_UT | 3;
	env->env_tf.tf_eflags &= ~((uint32_t)FL_IOPL_MASK);
	env->env_tf.tf_eflags |= FL_IF;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	// panic("sys_env_set_pgfault_upcall not implemented");
	struct Env *env;
	int rc;
	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	env->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	int rc;
	struct Env *env;
	uintptr_t va_addr = (uintptr_t)va;
	static const int ness_perm = PTE_U | PTE_P;
	struct PageInfo *pp;

	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	if(va_addr>=UTOP || va_addr%PGSIZE!=0) return -E_INVAL;
	if((perm & ness_perm)!=ness_perm || (perm & PTE_SYSCALL)!=perm) return -E_INVAL;
	if((pp = page_alloc(ALLOC_ZERO)) == NULL) return -E_NO_MEM;
	if((rc=page_insert(env->env_pgdir, pp, va, perm))<0){
		page_free(pp);
		return rc;
	}
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	// panic("sys_page_map not implemented");
	int rc;
	struct Env *src_env, *dst_env;
	uintptr_t src_addr = (uintptr_t)srcva, dst_addr = (uintptr_t)dstva;
	struct PageInfo *pp;
	pte_t *pte;
	static const int ness_perm = PTE_U | PTE_P;
	
	if((rc = envid2env(srcenvid, &src_env, 1)) < 0)return rc;
	if((rc = envid2env(dstenvid, &dst_env, 1)) < 0) return rc;
	if(src_addr>=UTOP || src_addr%PGSIZE!=0)return -E_INVAL;
	if(dst_addr>=UTOP || dst_addr%PGSIZE!=0)return -E_INVAL;
	if((perm & ness_perm)!=ness_perm || (perm & PTE_SYSCALL)!=perm)return -E_INVAL;
	if((pp = page_lookup(src_env->env_pgdir, srcva, &pte)) == NULL)return -E_INVAL;
	if((perm & PTE_W) && !(*pte & PTE_W))return -E_INVAL;
	if((rc=page_insert(dst_env->env_pgdir, pp, dstva, perm))<0) return rc;
	return 0;
}

static int
sys_page_map_withoutcheckperm(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
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

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	// panic("sys_page_unmap not implemented");
	int rc;
	struct Env *env;
	uintptr_t va_addr = (uintptr_t)va;

	if((rc = envid2env(envid, &env, 1)) < 0) return rc;
	if(va_addr>=UTOP || va_addr%PGSIZE!=0) return -E_INVAL;
	page_remove(env->env_pgdir, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm){
	int rc;
	struct Env *env;
	static const int ness_perm = PTE_U | PTE_P;
	
	// return -E_BAD_ENV if environment envid doesn't currently exist.
	if((rc = envid2env(envid, &env, 0))<0)
		goto done;
	
	/*
	* return -E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
	* or another environment managed to send first.
	* only one program could run in kernel mode, so there is no race condition
	* in syscall call function.
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

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva){
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


// Set up the initial stack page.
// using the arguments array pointed to by 'argv',
// which is a null-terminated array of pointers to null-terminated strings.
//
// On success, returns 0 and sets *init_esp
// to the initial stack pointe.
// Returns < 0 on failure.
static int
init_stack(const char **argv, uintptr_t *init_esp)
{
	size_t string_size;
	int argc, i, r;
	char *string_store;
	uintptr_t *argv_store;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (string_size).
	string_size = 0;
	for (argc = 0; argv[argc] != 0; argc++)
		string_size += strlen(argv[argc]) + 1;

	// Determine where to place the strings and the argv array.
	// Set up pointers into the temporary page 'UTEMP'; we'll map a page
	// there later, then remap that page into the new environment
	// at (USTACKTOP - PGSIZE).
	// strings is the topmost thing on the stack.
	string_store = (char*) UTEMP + PGSIZE - string_size;
	// argv is below that.  There's one argument pointer per argument, plus
	// a null pointer.
	argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));

	// Make sure that argv, strings, and the 2 words that hold 'argc'
	// and 'argv' themselves will all fit in a single stack page.
	if ((void*) (argv_store - 2) < (void*) UTEMP)
		return -E_NO_MEM;

	// Allocate the single stack page at UTEMP.
	if ((r = sys_page_alloc(0, (void*) UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		return r;


	//	* Initialize 'argv_store[i]' to point to argument string i,
	//	  for all 0 <= i < argc.
	//	  Also, copy the argument strings from 'argv' into the
	//	  newly-allocated stack page.
	//
	//	* Set 'argv_store[argc]' to 0 to null-terminate the args array.
	//
	//	* Push two more words onto the  stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the new umain() function.
	//	  argv should be below argc on the stack.
	//	  (Again, argv should use an address valid in the new
	//	  environment.)
	//
	//	* Set *init_esp to the initial stack pointer.
	//	  (Again, use an address valid in the new environment.)
	for (i = 0; i < argc; i++) {
		argv_store[i] = UTEMP2USTACK(string_store);
		strcpy(string_store, argv[i]);
		string_store += strlen(argv[i]) + 1;
	}
	argv_store[argc] = 0;
	assert(string_store == (char*)UTEMP + PGSIZE);

	argv_store[-1] = UTEMP2USTACK(argv_store);
	argv_store[-2] = argc;

	*init_esp = UTEMP2USTACK(&argv_store[-2]);

	// After completing the stack, map it into the user normal stack.
	// and unmap it from ours!
	if ((r = sys_page_map(0, UTEMP, 0, (void*) (USTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		goto error;
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		goto error;

	return 0;

error:
	sys_page_unmap(0, UTEMP);
	return r;
}

static int
sys_exec(uint8_t *binary, const char **argv){
	int r;
	struct Proghdr *ph, *eph;
	struct Elf *elf = (struct Elf *)binary;
	uintptr_t va;
	size_t memsz, filesz, offset;

	if(elf->e_magic != ELF_MAGIC)
		panic("sys_exec: bad elf");
	
	ph = (struct Proghdr *) (binary + elf->e_phoff);
	eph = ph + elf->e_phnum;
	
	/* 
	* Set up program segments as defined in ELF header.
	* consult the implementation of kern/env.c:load_icode.
	*/
	for(;ph < eph; ph++){
		if(ph->p_type == ELF_PROG_LOAD){
			va = ph->p_va;
			memsz = ph->p_memsz;
			filesz = ph->p_filesz;
			offset = ph->p_offset;
			if(filesz > memsz)
				panic("segment file size is larger than segment memory size");
			region_alloc(curenv, (void*)va, memsz);
			memmove((void*)va, binary + offset, filesz);
            memset((void*)(va+filesz), 0, memsz - filesz);
		}
	}


	/* set up the initial $eip. */
	curenv->env_tf.tf_eip = elf->e_entry;

	
	/* initialize the stack*/
	if((r = init_stack(argv, &curenv->env_tf.tf_esp))<0)
		panic("sys_exec: init_stack: %e", r);
	
	sched_yield(); // never return.
}
	

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
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
		case SYS_yield:
			sys_yield();
			break;
		case SYS_exofork:
			ret = sys_exofork();
			break;
		case SYS_env_set_status:
			ret = sys_env_set_status(a1, a2);
			break;
		case SYS_env_set_priority:
			ret = sys_env_set_priority(a1, a2);
			break;
		case SYS_env_set_pgfault_upcall:
			ret = sys_env_set_pgfault_upcall(a1, (void*)a2);
			break;
		case SYS_page_alloc:
			ret = sys_page_alloc(a1, (void*)a2, a3);
			break;
		case SYS_exec:
			ret = sys_exec((uint8_t *)a1, (const char **)a2);
			break;
		case SYS_page_map:
			ret = sys_page_map(a1, (void*)a2, a3, (void*)a4, a5);
			break;
		case SYS_page_unmap:
			ret = sys_page_unmap(a1, (void*)a2);
			break;
		case SYS_ipc_try_send:
			ret = sys_ipc_try_send(a1, a2, (void*)a3, a4);
			break;
		case SYS_ipc_recv:
			ret = sys_ipc_recv((void*)a1);
			break;
		case SYS_env_set_trapframe:
			ret = sys_env_set_trapframe(a1, (struct Trapframe*)a2);
			break;
		default:
			ret = -E_INVAL;
	}

	// panic("syscall not implemented");
	return ret;
	
}

