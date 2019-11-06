// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern volatile pte_t uvpt[];
extern volatile pde_t uvpd[];

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
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

	// LAB 4: Your code here.
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
	if((rc = sys_page_map(0, PFTEMP, 0, 
		ROUNDDOWN(addr, PGSIZE), PTE_P | PTE_U | PTE_W))<0)
			panic("In pgfault: sys_page_map error. %e", rc);

}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn){
	int r;
	pte_t pte;
	void *addr  = (void*)(pn << PGSHIFT);

	// LAB 4: Your code here.
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

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void){
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
	* copy our page fault upcall to child.
	* Notice that both the page fault upcall and the page fault handler 
	* which is called by page fault upcall are both in the form of function
	* handler, thus we can simply copy the function pointer of page fault 
	* upcall in envs[] arrays.
	*/
	if((rc = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall)) < 0)
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

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
