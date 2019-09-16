# Report for Lab 1

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751

------

## Physical Address Space

<u>The first PCs,</u> which were based on the 16-bit Intel 8088 processor,

- were only capable of addressing 1MB of physical memory(start at 0x00000000 but end at 0x000FFFFF)

- The 384KB (very top) area from 0x000A0000 through 0x000FFFFF was reserved by the hardware for special uses

- The most important part of this reserved area is the Basic Input/Output System (BIOS), which occupies the 64KB region from 0x000F0000 through 0x000FFFFF

- The 640KB area marked "Low Memory" was the *only* random-access memory (RAM) that an early PC could use

  

<u>Modern PCs,</u> 

- have a "hole" in physical memory from 0x000A0000 to 0x00100000, dividing RAM into "low" or "conventional memory" (the first 640KB) and "extended memory" (everything else). 
-  some space at the very top of the PC's 32-bit physical address space, above all physical RAM, is now commonly reserved by the BIOS for use by 32-bit PCI devices.

IBM modern PC's Physical Address Space:

![1568218362809](C:\Users\maiji\AppData\Roaming\Typora\typora-user-images\1568218362809.png)

<u>Recent x86 processors</u> can support more than 4GB of physical RAM, so RAM can extend further above 0xFFFFFFFF. In this case the BIOS must arrange to leave a second hole in the system's RAM at the top of the 32-bit addressable region, to leave room for these 32-bit devices to be mapped.



## BIOS

```bash
The target architecture is assumed to be i8086
[f000:fff0]    0xffff0: ljmp   $0xf000,$0xe05b
0x0000fff0 in ?? ()
```

With GDB, we know `ljmp` the first instruction to be executed after power-up, i.e.

- The IBM PC starts executing at physical address 0x000ffff0, which is at the very top of the 64KB area reserved for the ROM BIOS.

- The PC starts executing with `CS = 0xf000` and `IP = 0xfff0`.

- The first instruction to be executed is a `jmp` instruction, which jumps to the segmented address `CS = 0xf000` and `IP = 0xe05b`.

What *BIOS* does is:

- sets up an interrupt descriptor table
- initializes various devices such as the VGA display
- searches for a bootable device such as a floppy, hard drive, or CD-ROM
- when it finds a bootable disk, the BIOS reads the <u>boot loader</u> from the disk and transfers control to it


## Boot Loader


- **What is the different between *BIOS*  and *boot loader*?**

What *boot loader* does is:

- switches the processor from `real mode` to `32-bit protected mode` (for x86)

- reads the kernel from the hard disk

- transfers control to kernel

The order is <u>BIOS->boot loader->kernel</u>.



In the conventional hard drive boot mechanism(which we use here), the boot loader(`obj/boot/boot`) resides in the first sector of our boot device, which we also call `boot sector`. After finishing its work, BIOS loads the 512-byte boot sector into memory at physical addresses <u>0x7c00 through 0x7dff</u>, then uses a `jmp` instruction to set the CS:IP to 0000:7c00, passing control to the boot loader.


- **Why 0x7c00**
  The magic number 0x7c00 is the result of interesting history reasons referring  to  [here](https://www.glamenv-septzen.net/en/view/6) .
  

Actually the boot loader consists of 2 source: `boot/boot.S` and `boot/main.c`




- **What is the relation between *real mode* and *32-bit protected mode*?**
  
  |               | real mode                                                    | 16-bit protected mode                                        | 32-bit protected mode                                        |
  | ------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
  | since...      | 8086                                                         | 80286                                                        | 80386                                                        |
  | address range | 0x00000 - 0xFFFFF(20-bit)                                    |                                                              | 0x00000000-0xFFFFFFFF(32-bit)                                |
  | address mode  | segment : offset(segment * 16 + offset)                      | selector : offset(16-bit)                                    | selector : offset(32-bit)                                    |
  | remark        | In real mode, a <u>segment</u> value is a paragraph number of physical memory.<br />And these segments are at fixed positions in physical memory. | In protected mode, a <u>selector</u> value is an `index` into a `descriptor table`.<br />The selector value denotes the paragraph number of the beginning of the segment.<br />In protected mode, the segments are not at fixed positions in physical memory. In fact, they do not have to be in memory at all! |                                                              |
  |               |                                                              | Protected mode starts using techniques like  `virtual memory` and `Privilege levels`<br /> 16-bit mode, either the entire segment is in memory or none of it is. This is not practical with the larger segments that 32-bit mode allows. | The virtual memory system works with pages now instead of segments. <br />This means that only parts of segment may be in memory at any one time. |
  | segment sizes | Each byte in memory does not have an unique segmented address.<br />A single segment can only refer to **64K** of memory(16 bit of offset) | segment sizes are still limited to at most 64K               | Offsets are expanded to be 32-bits. Thus, segments can have sizes up to 4GB.<br />Segments can be divided into smaller 4K-sized units called `pages` |
  
  


## Exercise 3

> Q: At what point does the processor start executing 32-bit code? What exactly causes the switch from 16- to 32-bit mode?

After the changing of register` $cr0` , the processor start executing 32-bit code:(in `boot/boot.S`)

```assembly
  # Switch from real to protected mode, using a bootstrap GDT
  # and segment translation that makes virtual addresses 
  # identical to their physical addresses, so that the 
  # effective memory map does not change during the switch.
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE_ON, %eax
  movl    %eax, %cr0
```

`lgdt    gdtdesc` just assigns the address of `gdtdesc` to the register `GDTR`, by which we can know the enter of the global descriptor table.

The instruction `ljmp    $PROT_MODE_CSEG, $protcseg`  exactly causes the switch from 16- to 32-bit mode:(in `boot/boot.S`)

```assembly
  # Jump to next instruction, but in 32-bit code segment.
  # Switches processor into 32-bit mode.
  ljmp    $PROT_MODE_CSEG, $protcseg
```
Actually, PROT_MODE_CSEG(0x8) ensure that we still work in the same segment. The offset `$protcseg` is exactly the next instruction. Till now,we have switch to 32-bit mode, but we still work in the same program logic segment.

0x08 points at the code selector(`boot/boot.S`):

```assembly
# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  SEG_NULL								# null seg
  SEG(STA_X|STA_R, 0x0, 0xffffffff)		# code seg
  SEG(STA_W, 0x0, 0xffffffff)	        # data seg

gdtdesc:
  .word   0x17                            # sizeof(gdt) - 1
  .long   gdt                             # address gdt
```

in `inc/mmu.h`

```
#ifdef __ASSEMBLER__

/*
 * Macros to build GDT entries in assembly.
 */
#define SEG_NULL						\
	.word 0, 0;						\
	.byte 0, 0, 0, 0
#define SEG(type,base,lim)					\
	.word (((lim) >> 12) & 0xffff), ((base) & 0xffff);	\
	.byte (((base) >> 16) & 0xff), (0x90 | (type)),		\
		(0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#else	// not __ASSEMBLER__
```

Actually, null segment, code segment and data segment all start at 0x0, their limit are 0xffffffff(4G).

​	

> What is the *last* instruction of the boot loader executed, and what is the *first* instruction of the kernel it just loaded?

the *last* instruction of the boot loader executed:(in `obj/boot/boot.asm`)

```assembly
	((void (*)(void)) (ELFHDR->e_entry))();
    7d6b:	ff 15 18 00 01 00    	call   *0x10018
```
the *first* instruction of the kernel it just loaded: (in `obj/kern/kernel.asm`)

```assembly
f010000c:	66 c7 05 72 04 00 00 	movw   $0x1234,0x472
```

> *Where* is the first instruction of the kernel?

We may get the answer from the solution to previous question. Another way is to  examine `*0x10018` using `gdb`

```bash
(gdb) x/x 0x10018
0x10018:        0x0010000c
```

> How does the boot loader decide how many sectors it must read in order to fetch the entire kernel from disk? Where does it find this information?

The boot loader first reads a single page (512 * 8 = 4096 = 4K Bytes) off of disk and interprets it as `ELF ` structure. The boot loader then reads the  `program header` in the `ELF header` and loads all of them (in `boot/main.c`):

```c
	// read 1st page off disk
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// is this a valid ELF?	
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;	

	// load each program segment (ignores ph flags)
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		// p_pa is the load address of this segment (as well
		// as the physical address)
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
```

In terms of the number of sectors loaded in each segment, the boot loader rounds the number of bytes to sector boundaries and loads them (in `boot/main.c`):

```c
void
readseg(uint32_t pa, uint32_t count, uint32_t offset)
{
	uint32_t end_pa;

	end_pa = pa + count;

	// round down to sector boundary
	pa &= ~(SECTSIZE - 1);

	// translate from bytes to sectors, and kernel starts at sector 1
	offset = (offset / SECTSIZE) + 1;

	// If this is too slow, we could read lots of sectors at a time.
	// We'd write more to memory than asked, but it doesn't matter --
	// we load in increasing order.
	while (pa < end_pa) {
		// Since we haven't enabled paging yet and we're using
		// an identity segment mapping (see boot.S), we can
		// use physical addresses directly.  This won't be the
		// case once JOS enables the MMU.
		readsect((uint8_t*) pa, offset);
		pa += SECTSIZE;
		offset++;
	}
}
```

## Exercise 5

*link address(VMA)* vs. *load address(LMA)*

- The *load address* of a section is the memory address at which that section should be loaded into memory.

- The *link address* of a section is the memory address from which the section expects to execute.



> Trace through the first few instructions of the boot loader again and identify the first instruction that would "break" or otherwise do the wrong thing if you were to get the boot loader's link address wrong.

Obviously the `ljmp $PROT_MODE_CSEG, $protcseg` is the first instruction that would "break" or otherwise do the wrong thing if you were to get the boot loader's link address wrong.

> Then change the link address in `boot/Makefrag` to something wrong, run make clean, recompile the lab with make, and trace into the boot loader again to see what happens. 

In this experiment, I modify `-Ttext 0x7C00` to `-Ttext 0x7E00`.

However, when executing, BIOS load the instructions of boot loader at `0x7c00` by default, regardless of what the value of `-Ttext` we set.

However, in  the subsequent jump instruction `lgdtw`, the address is false:

(in `-Ttext 0x7e00` )
```bash
(gdb) x/20i 0x7c00
=> 0x7c00:      cli
   0x7c01:      cld
   0x7c02:      xor    %ax,%ax
   0x7c04:      mov    %ax,%ds
   0x7c06:      mov    %ax,%es
   0x7c08:      mov    %ax,%ss
   0x7c0a:      in     $0x64,%al
   0x7c0c:      test   $0x2,%al
   0x7c0e:      jne    0x7c0a
   0x7c10:      mov    $0xd1,%al
   0x7c12:      out    %al,$0x64
   0x7c14:      in     $0x64,%al
   0x7c16:      test   $0x2,%al
   0x7c18:      jne    0x7c14
   0x7c1a:      mov    $0xdf,%al
   0x7c1c:      out    %al,$0x60
   0x7c1e:      lgdtw  0x7e64    # false!
   0x7c23:      mov    %cr0,%eax
   0x7c26:      or     $0x1,%eax
   0x7c2a:      mov    %eax,%cr0
```

(in `-Ttext 0x7C00` )
```bash
=> 0x7c00:      cli
   0x7c01:      cld
   0x7c02:      xor    %ax,%ax
   0x7c04:      mov    %ax,%ds
   0x7c06:      mov    %ax,%es
   0x7c08:      mov    %ax,%ss
   0x7c0a:      in     $0x64,%al
   0x7c0c:      test   $0x2,%al
   0x7c0e:      jne    0x7c0a
   0x7c10:      mov    $0xd1,%al
   0x7c12:      out    %al,$0x64
   0x7c14:      in     $0x64,%al
   0x7c16:      test   $0x2,%al
   0x7c18:      jne    0x7c14
   0x7c1a:      mov    $0xdf,%al
   0x7c1c:      out    %al,$0x60
   0x7c1e:      lgdtw  0x7c64		# right!
   0x7c23:      mov    %cr0,%eax
   0x7c26:      or     $0x1,%eax
   0x7c2a:      mov    %eax,%cr0
```

Actually, linking address is not necessarily identical to load address. Linking address is used while linking, i.e. the address generated by `objdump` (actually don't exist in ELF file) may not be the true address while executing.

## Exercise 6

> Examine the 8 words of memory at 0x00100000 at the point the BIOS enters the boot loader, and then again at the point the boot loader enters the kernel. Why are they different? What is there at the second breakpoint? (You do not really need to use QEMU to answer this question. Just think.)

Before the BIOS enters the boot loader

```
(gdb) x/8x 0x100000
0x100000:	0x00000000	0x00000000	0x00000000	0x00000000
0x100010:	0x00000000	0x00000000	0x00000000	0x00000000
```

Before the boot loader enters the kernel

```
(gdb) x/8x 0x100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0x100010:	0x34000004	0x0000b812	0x220f0011	0xc0200fd8
```

Because the boot loader loads the kernel to 0x100000.

## Exercise 7

> Use QEMU and GDB to trace into the JOS kernel and stop at the `movl %eax, %cr0`. Examine memory at 0x00100000 and at 0xf0100000. Now, single step over that instruction using the `stepi` GDB command. Again, examine memory at 0x00100000 and at 0xf0100000. Make sure you understand what just happened.

```asm
(gdb) b *0x100025
Breakpoint 1 at 0x100025
(gdb) c
Continuing.
The target architecture is assumed to be i386
=> 0x100025:    mov    %eax,%cr0

Breakpoint 1, 0x00100025 in ?? ()
(gdb) x/10x 0x00100000
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x2000b812      0x220f0011      0xc0200fd8
0x100020:       0x0100010d      0xc0220f80
(gdb) x/10x 0xf0100000
0xf0100000 <_start+4026531828>: 0x00000000      0x00000000      0x00000000      0x00000000
0xf0100010 <entry+4>:   0x00000000      0x00000000      0x00000000      0x00000000
0xf0100020 <entry+20>:  0x00000000      0x00000000
(gdb) si
=> 0x100028:    mov    $0xf010002f,%eax
0x00100028 in ?? ()
(gdb) x/10x 0x00100000
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x2000b812      0x220f0011      0xc0200fd8
0x100020:       0x0100010d      0xc0220f80
(gdb) x/10x 0xf0100000
0xf0100000 <_start+4026531828>: 0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0xf0100010 <entry+4>:   0x34000004      0x2000b812      0x220f0011      0xc0200fd8
0xf0100020 <entry+20>:  0x0100010d      0xc0220f80
```

As we can see, Before `movl %eax, %cr0`, memory at 0x00100000 and at 0xf0100000 is different. After we `stepi`, memory at 0xf0100000 have been mapped to memory at 0x00100000.

> What is the first instruction *after* the new mapping is established that would fail to work properly if the mapping weren't in place? Comment out the `movl %eax, %cr0` in`kern/entry.S`, trace into it, and see if you were right.

`jmp *%eax` would fail because `0xf010002c` is outside of RAM

```bash
qemu: fatal: Trying to execute code outside RAM or ROM at 0xf010002c
```

## Exercise 8

> We have omitted a small fragment of code - the code necessary to print octal numbers using patterns of the form "%o". Find and fill in this code fragment.

Replace the original code in  `lib/printfmt.c`:
```c
// (unsigned) octal
case 'o':
  // Replace this with your code.
  putch('X', putdat);
  putch('X', putdat);
  putch('X', putdat);
  break;
```
with

```c
// (unsigned) octal
		case 'o':
			// Replace this with your code.
			num = getuint(&ap, lflag);
			base = 8;
			goto number;
```

Be able to answer the following questions:

> 1. Explain the interface between `printf.c` and `console.c`. Specifically, what function does `console.c` export? How is this function used by `printf.c`?

​	`console.c` export functions`cputchar` to `printf.c`.  This function is used as a parameter when `printf.c` calls `vprintfmt` in `printfmt.c`.



> 2. Explain the following from `console.c`:
>
> ```c
> if (crt_pos >= CRT_SIZE) {
>      int i;
>     memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
> 	for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
> 		crt_buf[i] = 0x0700 | ' ';
>     crt_pos -= CRT_COLS;
> }
> ```

When the screen is full, scroll down one row to show newer information, and fill the new row with black space as foreground.



> 3. For the following questions you might wish to consult the notes for Lecture 2. These notes cover GCC's calling convention on the x86.
>
> Trace the execution of the following code step-by-step:
>
> ```c
> int x = 1, y = 3, z = 4;
> cprintf("x %d, y %x, z %d\n", x, y, z);
> ```
>
> - In the call to `cprintf()`, to what does `fmt` point? To what does `ap` point?
> - List (in order of execution) each call to `cons_putc`, `va_arg`, and `vcprintf`. For `cons_putc`, list its argument as well. For `va_arg`, list what `ap` points to before and after the call. For `vcprintf` list the values of its two arguments.

I add this code to `monitor.c`:

```c
void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	/*****
	 * insert 
	 */
	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);
	/*****
	 * insert 
	 */
	
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
```

`fmt` point to the format string of its arguments, `ap` points to the variable arguments after `fmt`.

Calls to `cons_putc`, `va_arg`, and `vcprintf`  are listed as following:

```bash
vcprintf (fmt=0xf0101f26 "x %d, y %x, z %d\n", ap=0xf010ff54 "\001") at kern/printf.c:19

cons_putc (c=120) at kern/console.c:70

cons_putc (c=32) at kern/console.c:70

Hardware watchpoint 6: ap
Old value = (va_list) 0xf010ff54 "\001"
New value = (va_list) 0xf010ff58 "\003"

cons_putc (c=49) at kern/console.c:70

cons_putc (c=44) at kern/console.c:70

cons_putc (c=32) at kern/console.c:70

cons_putc (c=121) at kern/console.c:70

cons_putc (c=32) at kern/console.c:70

Hardware watchpoint 6: ap
Old value = (va_list) 0xf010ff58 "\003"
New value = (va_list) 0xf010ff5c "\004"

cons_putc (c=51) at kern/console.c:70

cons_putc (c=44) at kern/console.c:70

cons_putc (c=32) at kern/console.c:70

cons_putc (c=122) at kern/console.c:70

cons_putc (c=32) at kern/console.c:70

Hardware watchpoint 6: ap
Old value = (va_list) 0xf010ff5c "\004"
New value = (va_list) 0xf010ff60 "\320 ", <incomplete sequence \360>

cons_putc (c=52) at kern/console.c:70

cons_putc (c=10) at kern/console.c:70
```

> 4. Run the following code.
>
> ```c
>     unsigned int i = 0x00646c72;
>     cprintf("H%x Wo%s", 57616, &i);
> ```
>
> What is the output? Explain how this output is arrived at in the step-by-step manner of the previous exercise. 

the output is:

```
He110 World
```

as for `e110`, the decimal number `57616` is written as `e110` in the hexadecimal format.

as for `rld`, `0x00646c72`  is stored in little endian format like `72`, `6c`, `64`, `00`,  which corresponding ASCII character sequence is `rld`(`\0` marks the end of a string.).

> 5. In the following code, what is going to be printed after `'y='`
>
> ```c
>     cprintf("x=%d y=%d", 3); 
> ```

the output is:

```
x=3 y=-267321448
```

the number after `y=` will be the decimal value of the 4 bytes right above where 3 is placed in the stack.



> 6. Let's say that GCC changed its calling convention so that it pushed arguments on the stack in declaration order, so that the last argument is pushed last. How would you have to change `cprintf` or its interface so that it would still be possible to pass it a variable number of arguments?

One possible solution is the modify the definition of `va_start`, `va_arg` and `va_end`.



> *Challenge* Enhance the console to allow text to be printed in different colors. The traditional way to do this is to make it interpret [ANSI escape sequences](http://rrbrandt.dee.ufcg.edu.br/en/docs/ansi/) embedded in the text strings printed to the console, but you may use any mechanism you like. There is plenty of information on [the 6.828 reference page](https://pdos.csail.mit.edu/6.828/2018/reference.html) and elsewhere on the web on programming the VGA display hardware. If you're feeling really adventurous, you could try switching the VGA hardware into a graphics mode and making the console draw text onto the graphical frame buffer.

More details about *ANSI escape sequences* could be viewed [here](http://ascii-table.com/ansi-escape-sequences.php).

 Syntax of ANSI escape sequences: http://ascii-table.com/ansi-escape-sequences.php

 Information about CGA/VGA color display could been seen at [here](https://pdos.csail.mit.edu/6.828/2008/readings/hardware/vgadoc/VGABIOS.TXT).

```
Return: AH = attribute
             bit    7: blink
             bits 6-4: background color
                       000 black
                       001 blue
                       010 green
                       011 cyan
                       100 red
                       101 magenta
                       110 brown
                       111 white
             bits 3-0: foreground color
                       0000 black       1000 dark grey
                       0001 blue        1001 light blue
                       0010 green       1010 light green
                       0011 cyan        1011 light cyan
                       0100 red         1100 light red
                       0101 magenta     1101 light magenta
                       0110 brown       1110 yellow
                       0111 light grey  1111 white
        AL = character
```

As background color doesn't support `yellow`, we use `brown` instead.

in `kern/console.c`:

```c
static uint8_t foreground_vgamap[]={
	0b0000,	// Black
	0b0100,	// Red
	0b0010,	// Green
	0b1110,	// Yellow
	0b0001,	// Blue
	0b0101,	// Magenta
	0b0011,	// Cyan
	0b1111,	// White
};

static uint8_t background_vgamap[]={
	0b000,	// Black
	0b100,	// Red
	0b010,	// Green
	0b110,	// TODO: doesn't support 'yellow', use 'brown' instead.
	0b001,	// Blue
	0b101,	// Magenta
	0b011,	// Cyan
	0b111,	// White
};

static uint16_t cga_color_attr = 0x0700;


// return zero if ansi sequence meet the end.
static int cga_ansi_color(int ch){
	static uint16_t attribute = 0;
	static uint16_t ansi_code = 0;

	if(ch == 'm' || ch==';'){
		if(ansi_code == 0){								// all attributes off 
			attribute = 0x0700;
		}else if (30 <= ansi_code && ansi_code < 38){	// foreground colors
			attribute |= (uint16_t)foreground_vgamap[ansi_code - 30] << 8;
		}else if (40 <= ansi_code && ansi_code < 48){	// background colors
			attribute |= (uint16_t)background_vgamap[ansi_code - 40] << 12;
		}else{
			assert(0);
		}
		ansi_code = 0;
		if(ch == 'm'){
			cga_color_attr = attribute;
			attribute = 0;
			return 0;
		}
	}else{
		ansi_code = ansi_code * 10 + ch - '0';
	}
	return 1;
}

enum ansi_status{
	ANSI_NONE,
	ANSI_REDAY,
	ANSI_PROCESS,
};

static int cga_ansi_check(int ch){
	static enum ansi_status status = ANSI_NONE;
	switch (status){
		case ANSI_NONE:
			if(ch == 0x1b){
				status = ANSI_REDAY;
				return 1;
			}
			return 0;
		break;
		case ANSI_REDAY:
			if(ch == '['){
				status = ANSI_PROCESS;
				return 1;
			}
			status = ANSI_NONE;
			return 0;
		break;
		case ANSI_PROCESS:
			if(!cga_ansi_color(ch)){
				status = ANSI_NONE;
			}
			return 1;
		break;
		default:
			return 0;
	}
}

static void
cga_putc(int c)
{

	if(cga_ansi_check(c))
		return;
	
	// if no attribute given
	if (!(c & ~0xFF))
		c |= cga_color_attr;
	

	switch (c & 0xff) {
           
```

in `kern/init.c`:

```c
void
i386_init(void)
{
	extern char edata[], end[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	cprintf("6828 decimal is %o octal!\n", 6828);

	// Test the stack backtrace function (lab 1 only)
	test_backtrace(5);

	// Test text color
	cprintf("\x001b[30m A \x001b[31m B \x001b[32m C \x001b[33m D \x001b[34m E \x001b[35m F \x001b[36m G \x001b[37m H");
	cprintf("\x001b[0m\n");
	cprintf("\x001b[42;30m A \x001b[42;31m B \x001b[42;32m C \x001b[42;33m D \x001b[42;34m E \x001b[42;35m F \x001b[42;36m G \x001b[42;37m H");
	cprintf("\x001b[0m\n");

	// Drop into the kernel monitor.
	while (1)
		monitor(NULL);
}
```

The final result is:

![1568578419028](C:\Users\maiji\AppData\Roaming\Typora\typora-user-images\1568578419028.png)

## Exercise 9

> Determine where the kernel initializes its stack, and exactly where in memory its stack is located. How does the kernel reserve space for its stack? And at which "end" of this reserved area is the stack pointer initialized to point to?

The virtual memory map in `inc/memlayout.h`:

```c
/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xef400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xef000000
 *                     |           RO ENVS            | R-/R-  PTSIZE
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 *
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.  JOS user programs map pages temporarily at UTEMP.
 */
```

From above, we could know that the kernel stack starts at `0xf0000000`.

in  `kern/entry.S`, just before calling into C function `i386_init`, the kernel stack pointer is initialized:

```assembly
relocated:

	# Clear the frame pointer register (EBP)
	# so that once we get into debugging C code,
	# stack backtraces will be terminated properly.
	movl	$0x0,%ebp			# nuke frame pointer

	# Set the stack pointer
	movl	$(bootstacktop),%esp

	# now to C code
	call	i386_init
```

From above, `$esp` is initialized to point to `bootstacktop` and grows backwards towards page boundary `bootstack`.


Also, `bootstack`and `bootstacktop` is defined in `kern/entry.S`:

```assembly
.data
###################################################################
# boot stack
###################################################################
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:
```

As seen in `inc/mmu.h`:

```C
#define PGSIZE		4096		// bytes mapped by a page
#define PGSHIFT		12		// log2(PGSIZE)
```

As seen in `inc/memlayout.h`:

```c
// Kernel stack.
#define KSTACKTOP	KERNBASE
#define KSTKSIZE	(8*PGSIZE)   		// size of a kernel stack
#define KSTKGAP		(8*PGSIZE)   		// size of a kernel stack guard
```

Thus `.space` directive allocates `KTSTKSIZE` bytes (`8*4096`) for the kernel stack, then it's exported as `bootstacktop` symbol.

## Exercise 10

> To become familiar with the C calling conventions on the x86, find the address of the `test_backtrace` function in `obj/kern/kernel.asm`, set a breakpoint there, and examine what happens each time it gets called after the kernel starts. How many 32-bit words does each recursive nesting level of `test_backtrace` push on the stack, and what are those words?

Each recursive nesting level of `test_backtrace` push seven 32-bit words  on the stack.

In `obj/kern/kernel.asm`, we can get the address of the `test_backtrace` function:

```assembly
f0100040 <test_backtrace>:
#include <kern/console.h>

// Test the stack backtrace function (lab 1 only)
void
test_backtrace(int x)
{
f0100040:	55                   	push   %ebp
f0100041:	89 e5                	mov    %esp,%ebp
f0100043:	56                   	push   %esi
f0100044:	53                   	push   %ebx
f0100045:	e8 72 01 00 00       	call   f01001bc <__x86.get_pc_thunk.bx>
f010004a:	81 c3 be 12 01 00    	add    $0x112be,%ebx
f0100050:	8b 75 08             	mov    0x8(%ebp),%esi
	cprintf("entering test_backtrace %d\n", x);
f0100053:	83 ec 08             	sub    $0x8,%esp
```

Some debug info about the stack is listed following:

breakpoint at `f0100043` in `test_backtrace(5)`:

```bash
(gdb) info reg
eax            0x0      0
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ffd8       0xf010ffd8
ebp            0xf010ffd8       0xf010ffd8
esi            0x10094  65684
edi            0x0      0
eip            0xf0100043       0xf0100043 <test_backtrace+3>
eflags         0x46     [ PF ZF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x10     16
gs             0x10     16                           
(gdb) x/12x $esp
0xf010ffd8:     0xf010fff8      < esp, ebp
				0xf01000f4      < return address
				0x00000005      < parameter x = 5
				0x00001aac
0xf010ffe8:     0x00000640      
				0x00000000      
				0x00000000      
				0x00010094
0xf010fff8:     0x00000000      
				0xf010003e      
				0x00000003      
				0x00001003
```

breakpoint at `f0100040` in `test_backtrace(4)`:

```bash
(gdb) info reg
eax            0x4      4
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ffbc       0xf010ffbc
ebp            0xf010ffd8       0xf010ffd8
esi            0x5      5
edi            0x0      0
eip            0xf0100040       0xf0100040 <test_backtrace>
eflags         0x92     [ AF SF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x10     16
gs             0x10     16
(gdb) x/12x $esp
0xf010ffbc:     0xf01000a1      < esp, return address
				0x00000004      < para x = 4(used to be return address for 'cprintf')
				0x00000005      < (used to be para x = 5 for 'cprintf')
				0x00000000		
0xf010ffcc:     0xf010004a      
				0xf0111308      
				0x00010094      
				0xf010fff8		< ebp, and "value" is the former "ebp"
0xf010ffdc:     0xf01000f4      < return address
				0x00000005      < para x = 5
				0x00001aac      
				0x00000640
```

breakpoint at `f0100040` in `test_backtrace(3)`:

```bash
(gdb) info reg
eax            0x3      3
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ff9c       0xf010ff9c
ebp            0xf010ffb8       0xf010ffb8
esi            0x4      4
edi            0x0      0
eip            0xf0100040       0xf0100040 <test_backtrace>
eflags         0x92     [ AF SF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x10     16
gs             0x10     16
(gdb) x/12x $esp
0xf010ff9c:     0xf01000a1      < esp, return address
				0x00000003      < para x = 3(used to be return address for 'cprintf')
				0x00000004      < (used to be para x = 4 for 'cprintf')
				0x00000000
0xf010ffac:     0xf010004a      
				0xf0111308      
				0x00000005      
				0xf010ffd8		< ebp, and "value" is the former "ebp"
0xf010ffbc:     0xf01000a1      < return address
				0x00000004 		< para x = 4
				0x00000005      
				0x00000000
```

breakpoint at `f0100040` in `test_backtrace(2)`:

```bash
(gdb) info reg
eax            0x2      2
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ff7c       0xf010ff7c
ebp            0xf010ff98       0xf010ff98
esi            0x3      3
edi            0x0      0
eip            0xf0100040       0xf0100040 <test_backtrace>
eflags         0x96     [ PF AF SF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x10     16
gs             0x10     16
(gdb) x/12x $esp
0xf010ff7c:     0xf01000a1      < esp, return address
				0x00000002      < para x = 2(used to be return address for 'cprintf')
				0x00000003      < (used to be para x = 3 for 'cprintf')
				0xf010ffb8
0xf010ff8c:     0xf010004a      
				0xf0111308      
				0x00000004      
				0xf010ffb8		< ebp, and "value" is the former "ebp"
0xf010ff9c:     0xf01000a1      < return address
				0x00000003      < para x = 3
				0x00000004      
				0x00000000
```

breakpoint at `f0100040` in `test_backtrace(0)`:

```bash
(gdb) info reg
eax            0x0      0
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ff3c       0xf010ff3c
ebp            0xf010ff58       0xf010ff58
esi            0x1      1
edi            0x0      0
eip            0xf0100040       0xf0100040 <test_backtrace>
eflags         0x96     [ PF AF SF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x10     16
gs             0x10     16
(gdb) x/12x $esp
0xf010ff3c:     0xf01000a1      < esp, return address
				0x00000000      < para x = 0(used to be return address for 'cprintf')
				0x00000001      < (used to be para x = 1 for 'cprintf')
				0xf010ff78
0xf010ff4c:     0xf010004a      
				0xf0111308      
				0x00000002      
				0xf010ff78		< ebp, and "value" is the former "ebp"
0xf010ff5c:     0xf01000a1      < return address
				0x00000001      < para x = 1
				0x00000002      
				0xf010ff98
```

## Exercise 11

> **Exercise 11.**  Implement the backtrace function as specified above. Use the same format as in the example, since otherwise the grading script will be confused. When you think you have it working right, run make grade to see if its output conforms to what our grading script expects, and fix it if it doesn't. *After* you have handed in your Lab 1 code, you are welcome to change the output format of the backtrace function any way you like.
>
> If you use `read_ebp()`, note that GCC may generate "optimized" code that calls `read_ebp()` *before* `mon_backtrace()`'s function prologue, which results in an incomplete stack trace (the stack frame of the most recent function call is missing). While we have tried to disable optimizations that cause this reordering, you may want to examine the assembly of`mon_backtrace()` and make sure the call to `read_ebp()` is happening after the function prologue.

in `kern/monitor.c`:

```c
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp = read_ebp();
	uintptr_t *p = (uintptr_t *)ebp;

	int i;
	uintptr_t eip;
	
	cprintf("Stack backtrace:\n");
	
	while(p){
		eip = *(p + 1);
		cprintf("  ebp %08x  eip %08x  args", p, eip);
		for(i = 0; i < 5; i++)
			cprintf(" %08x", *(p + 2 + i));
		cprintf("\n");
		p = (uintptr_t *)*p;

	}

	return 0;
}
```

## Exercise 12

> **Exercise 12.** Modify your stack backtrace function to display, for each `eip`, the function name, source file name, and line number corresponding to that `eip`.

To get all this info, we need get the debug info `Eipdebuginfo`, this struct stores all the information we need. in `kern/kdebug.h`:

```c
struct Eipdebuginfo {
	const char *eip_file;		// Source code filename for EIP
	int eip_line;			// Source code linenumber for EIP

	const char *eip_fn_name;	// Name of function containing EIP
					//  - Note: not null terminated!
	int eip_fn_namelen;		// Length of function name
	uintptr_t eip_fn_addr;		// Address of start of function
	int eip_fn_narg;		// Number of function arguments
};
```

To get this struct, we need the `debuginfo_eip(addr, info)` function. This function search the `STAB` table to Fill in the `info` structure with information about the specified instruction address, `addr`. The `debuginfo_eip` function has been given in `kdebug.c`, we just need to add this in `kern/kdebug.h`:

```c
// Your code here.
	stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
	if (lfile == 0)
		return -1;
	info->eip_line = stabs[lline].n_desc;
```

in `kern/monitor.c`:

```c
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
```

Finally, we pass all the tests:

```bash
running JOS: (1.4s)
  printf: OK
  backtrace count: OK
  backtrace arguments: OK
  backtrace symbols: OK
  backtrace lines: OK
Score: 50/50
```

