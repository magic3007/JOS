#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{

#if 0
	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment. Make sure curenv is not null before
	// dereferencing it.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
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
#else
	/* 
	* Implement fixed-priority scheduling.
	* 
	* Search through 'envs' for an ENV_RUNNABLE environment with the highest priority.
	* 
	* If there are more than one environment with the highest priority, select the environment
	* in the circular fashion starting just after the env this CPU was last running.
	*
	* If no envs with highest priority are runnable, but the environment previously running 
	* on this CPU is still ENV_RUNNING, it's okay to choose that environment.
	* 
	* If there are no runnable environments, simply halt the cpu.
	*/

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
	
#endif
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		// halts the central processing unit (CPU) until the next external interrupt is fired.
		"hlt\n" 
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

