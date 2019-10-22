/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>


###################################################################
# exceptions/interrupts
###################################################################

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
 # #define TRAPHANDLER(name, num)						\
 # 	.globl name;		/* define global symbol for 'name' */	\
 # 	.type name, @function;	/* symbol type is function */		\
 # 	.align 2;		/* align function definition */		\
 # 	name:			/* function starts here */		\
 # 	pushl $(num);							\
 # 	jmp _alltraps


/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
# #define TRAPHANDLER_NOEC(name, num)					\
#   	.globl name;							\
#  	.type name, @function;						\
#  	.align 2;							\
#  	name:								\
#  	pushl $0;							\
#  	pushl $(num);							\
#  	jmp _alltraps

/*
 * Lab 3: Your code here for _alltraps
 */

_alltraps:
	pushl %ds;
	pushl %es;
	pushal;
	
	movl $GD_KD, %eax;
	movw %ax, %ds;
	movw %ax, %es;

	pushl %esp

	call trap

/*
 * =========================================================================
 */

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

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

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