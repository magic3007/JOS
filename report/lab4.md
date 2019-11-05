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





>  **Exercise 4.** The code in `trap_init_percpu()` (`kern/trap.c`) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs. (Note: your new code should not use the global `ts` variable any more.) 