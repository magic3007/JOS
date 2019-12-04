# JOS Lab 5

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751

## The File System

### Disk Access

> **Exercise 1.** `i386_init` identifies the file system environment by passing the type `ENV_TYPE_FS` to your environment creation function, `env_create`. Modify `env_create` in `env.c`, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.
>
> Make sure you can start the file environment without causing a General Protection fault. You should pass the "fs i/o" test in make grade.

When we use sensitive IO instructions, like `IN`, `OUT` and etc, the processor first checks whether `CPL <= IOPL`.  If this condition is true, the I/O operation may proceed. If not true, the processor checks the I/O permission map in TSS. In `kern/env.c`:

```c
void env_create(uint8_t *binary, enum EnvType type){
	......
	// If this is the file server (type == ENV_TYPE_FS) give it I/O privileges.
	// LAB 5: Your code here.
	if(type == ENV_TYPE_FS)
		env->env_tf.tf_eflags |= FL_IOPL_3;
}
```

> **Question**
>
> 1. Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?

No. As the IO privilege `IOPL` is stored in `eflags` and could be saved and restored along with `eflags`.

### The Block Cache

> **Exercise 2.** Implement the `bc_pgfault` and `flush_block` functions in `fs/bc.c`. `bc_pgfault` is a page fault handler, just like the one your wrote in the previous lab for copy-on-write fork, except that its job is to load pages in from the disk in response to a page fault. When writing this, keep in mind that (1) `addr` may not be aligned to a block boundary and (2) `ide_read` operates in sectors, not blocks.
>
> The `flush_block` function should write a block out to disk *if necessary*. `flush_block` shouldn't do anything if the block isn't even in the block cache (that is, the page isn't mapped) or if it's not dirty. We will use the VM hardware to keep track of whether a disk block has been modified since it was last read from or written to disk. To see whether a block needs writing, we can just look to see if the `PTE_D` "dirty" bit is set in the `uvpt` entry. (The `PTE_D` bit is set by the processor in response to a write to that page; see 5.2.4.3 in [chapter 5](http://pdos.csail.mit.edu/6.828/2011/readings/i386/s05_02.htm) of the 386 reference manual.) After writing the block to disk, `flush_block` should clear the `PTE_D` bit using `sys_page_map`.
>
> Use make grade to test your code. Your code should pass "check_bc", "check_super", and "check_bitmap".

in `fs/bc.c:bg_fault` and `fs/bc.c:flush_block`, follow the instruction of code annotation:

```c
static void
bc_pgfault(struct UTrapframe *utf){
	......
	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary. fs/ide.c has code to read
	// the disk.
	//
	// LAB 5: you code here:
	void* page_addr = ROUNDDOWN(addr, PGSIZE);
	if((r = sys_page_alloc(0, page_addr, PTE_SYSCALL)) < 0)
		panic("in bg_fault, sys_page_alloc: %e", r);
	
	uint32_t secno = blockno * (BLKSIZE / SECTSIZE);
	size_t nsecs = PGSIZE / SECTSIZE;
	if((r = ide_read(secno, page_addr, nsecs)<0))
		panic("in bc_pgfault, ide_read: %e", r);
	......
}

void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.

	void *page_addr = ROUNDDOWN(addr, PGSIZE);
	uint32_t secno = blockno * (BLKSIZE / SECTSIZE);
	size_t nsecs = PGSIZE / SECTSIZE;

	if(!va_is_mapped(page_addr) || !va_is_dirty(page_addr)){
		/* If the block is not in the block cache or is not dirty, does nothing. */
		return;
	}
	if((r = ide_write(secno, page_addr, nsecs)) < 0)
		panic("in flush_bloc, ide_write: %e", r);
	
	if((r = sys_page_map(0, page_addr, 0, page_addr, uvpt[PGNUM(page_addr)] & PTE_SYSCALL)) < 0)
		panic("in flush_block, sys_page_map: %e", r);
}
```

### The Block Bitmap

> **Exercise 3.** Use `free_block` as a model to implement `alloc_block` in `fs/fs.c`, which should find a free disk block in the bitmap, mark it used, and return the number of that block. When you allocate a block, you should immediately flush the changed bitmap block to disk with `flush_block`, to help file system consistency.
>
> Use make grade to test your code. Your code should now pass "alloc_block".

Use a simple linear for loop to find a free block. Note that the root(0) block, reserved block(1) and blocks for bitmaps should not be allocated.

![image-20191204121533872](lab5.assets/image-20191204121533872.png)

````c
int
alloc_block(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE blocks.  There are
	// super->s_nblocks blocks in the disk altogether.

	// LAB 5: Your code here.

	/* the root(0) block, reserved block(1) and blocks for bitmaps should not be allocated. */
	uint32_t blockno = 2 + super->s_nblocks / BLKBITSIZE;

	for(; blockno < super->s_nblocks; blockno++)
		if(block_is_free(blockno)){
			/* find a free block*/

			/* allocate this block */
			bitmap[blockno/32] ^= 1<<(blockno%32);

			/* flush the changed bitmap block to disk with flush_block. */			
			flush_block(&bitmap[blockno/32]);

			return blockno;
		}
	return -E_NO_DISK;
}
````

### File Operations

> **Exercise 4.** Implement `file_block_walk` and `file_get_block`. `file_block_walk` maps from a block offset within a file to the pointer for that block in the `struct File` or the indirect block, very much like what `pgdir_walk` did for page tables. `file_get_block` goes one step further and maps to the actual disk block, allocating a new one if necessary.
>
> Use make grade to test your code. Your code should pass "file_open", "file_get_block", and "file_flush/file_truncated/file rewrite", and "testfile".

the attribute `f_direct[]` and `f_indirect` store the block number instead of 

### The file system interface

> **Exercise 5.** Implement `serve_read` in `fs/serv.c`.
>
> `serve_read`'s heavy lifting will be done by the already-implemented `file_read` in `fs/fs.c` (which, in turn, is just a bunch of calls to `file_get_block`). `serve_read` just has to provide the RPC interface for file reading. Look at the comments and code in `serve_set_size` to get a general idea of how the server functions should be structured.
>
> Use make grade to test your code. Your code should pass "serve_open/file_stat/file_close" and "file_read" for a score of 70/150.

> **Exercise 6.** Implement `serve_write` in `fs/serv.c` and `devfile_write` in `lib/file.c`.
>
> Use make grade to test your code. Your code should pass "file_write", "file_read after file_write", "open", and "large file" for a score of 90/150.