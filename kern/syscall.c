/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	user_mem_assert(curenv, (void *)s, len, PTE_U);

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

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
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

	int rc = 0;
	struct Env *child_env = NULL;

	rc = env_alloc(&child_env, curenv->env_id);

	if (rc != 0)
		return (envid_t)rc;

	child_env->env_status = ENV_NOT_RUNNABLE;
	child_env->env_tf = curenv->env_tf;

	// This is the child process return code
	child_env->env_tf.tf_regs.reg_eax = 0;

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

	struct Env *e = NULL;
	int rc = 0;

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;

	rc = envid2env(envid, &e, 1);
	if (rc != 0)
		return rc;

	e->env_status = status;

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
	struct Env *e = NULL;
	int rc = 0;

	if (!tf)
		return -E_INVAL;

	if ((rc = envid2env(envid, &e, 1)) != 0)
		return rc;

	e->env_tf = *tf;

	// set the IOPL to 0
	e->env_tf.tf_eflags &= ~FL_IOPL_MASK;

	// enable interrupts
	e->env_tf.tf_eflags |= FL_IF;

	// set user privilage level
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_cs = GD_UT | 3;

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
	struct Env *e = NULL;
	int rc = 0;

	if ((rc = envid2env(envid, &e, 1)) != 0)
		return rc;

	e->env_pgfault_upcall = func;
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

	struct PageInfo *p = NULL;
	struct Env *e = NULL;
	int rc = 0;

	if ((perm & ~PTE_SYSCALL) != 0 || (uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE != 0)
		return -E_INVAL;

	rc = envid2env(envid, &e, 1);
	if (rc != 0)
		return rc;

	p = page_alloc(ALLOC_ZERO);
	if (!p)
		return -E_NO_MEM;

	rc = page_insert(e->env_pgdir, p, va, perm);
	if (rc < 0)
	{
		page_free(p);
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

	struct Env *src_e = NULL, *dst_e = NULL;
	struct PageInfo *src_pp = NULL;
	pte_t *src_pte = NULL;
	int rc = 0;

	if (envid2env(srcenvid, &src_e, 1) != 0 || envid2env(dstenvid, &dst_e, 1) != 0)
		return -E_BAD_ENV;

	if ((uintptr_t)srcva >= UTOP || (uintptr_t)srcva % PGSIZE != 0 ||
		(uintptr_t)dstva >= UTOP || (uintptr_t)dstva % PGSIZE != 0 ||
		perm & ~PTE_SYSCALL)
	{
		return -E_INVAL;
	}

	src_pp = page_lookup(src_e->env_pgdir, srcva, &src_pte);
	if (!src_pp || (perm & PTE_W && !(*src_pte & PTE_W)))
		return -E_INVAL;

	rc = page_insert(dst_e->env_pgdir, src_pp, dstva, perm);

	return rc;
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

	struct Env *e = NULL;
	int rc = 0;

	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE != 0)
		return -E_INVAL;

	if (envid2env(envid, &e, 1) != 0)
		return -E_BAD_ENV;

	page_remove(e->env_pgdir, va);

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
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	struct Env *target_env = NULL;
	struct PageInfo *pp = NULL;
	pte_t *env_pte = NULL;
	int rc = 0;

	if (envid2env(envid, &target_env, 0) != 0)
	{
		return -E_BAD_ENV;
	}

	if (!target_env->env_ipc_recving)
	{
		return -E_IPC_NOT_RECV;
	}

	if ((uintptr_t)srcva < UTOP && target_env->env_ipc_dstva != NULL)
	{
		if ((uintptr_t)srcva % PGSIZE != 0 || (perm & ~PTE_SYSCALL) != 0)
		{
			return -E_INVAL;
		}

		pp = page_lookup(curenv->env_pgdir, srcva, &env_pte);

		if (!pp)
		{
			return -E_INVAL;
		}

		if (perm & PTE_W && !(*env_pte & PTE_W))
		{
			return -E_INVAL;
		}

		if (page_insert(target_env->env_pgdir, pp, target_env->env_ipc_dstva, perm) < 0)
		{
			return -E_NO_MEM;
		}

		target_env->env_ipc_perm = perm;
	}
	else
	{
		target_env->env_ipc_perm = 0;
	}

	target_env->env_ipc_recving = 0;
	target_env->env_ipc_from = curenv->env_id;
	target_env->env_ipc_value = value;
	target_env->env_status = ENV_RUNNABLE;

	return 0;
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
sys_ipc_recv(void *dstva)
{
	if ((uintptr_t)dstva < UTOP)
	{
		if ((uintptr_t)dstva % PGSIZE != 0)
			return -E_INVAL;

		curenv->env_ipc_dstva = dstva;
	}
	else
	{
		curenv->env_ipc_dstva = NULL;
	}

	curenv->env_ipc_recving = 1;
	curenv->env_status = ENV_NOT_RUNNABLE;
	curenv->env_tf.tf_regs.reg_eax = 0;

	sched_yield();
}

// Return the current time.
static int
sys_time_msec(void)
{
	return time_msec();
}

static int
sys_tx_packet(char *buf, int size)
{
	if (!buf || size <= 0)
		return -E_INVAL;

	return e1000_tx(buf, size);
}

static int
sys_rx_packet(char *buf)
{
	if (!buf)
		return -E_INVAL;

	return e1000_rx(buf);
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	switch (syscallno) {
		case SYS_cputs:
			sys_cputs((const char *)a1, a2);
			return 0;
		case SYS_cgetc:
			return sys_cgetc();
		case SYS_getenvid:
			return (int32_t)sys_getenvid();
		case SYS_env_destroy:
			return sys_env_destroy((envid_t)a1);
		case SYS_yield:
			sys_yield();
			return 0;
		case SYS_exofork:
			return (int32_t)sys_exofork();
		case SYS_env_set_status:
			return (int32_t)sys_env_set_status((envid_t)a1, (int)a2);
		case SYS_page_alloc:
			return (int32_t)sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
		case SYS_page_map:
			return (int32_t)sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
		case SYS_page_unmap:
			return (int32_t)sys_page_unmap((envid_t)a1, (void *)a2);
		case SYS_env_set_pgfault_upcall:
			return (int32_t)sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
		case SYS_ipc_try_send:
			return (int32_t)sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
		case SYS_ipc_recv:
			return (int32_t)sys_ipc_recv((void *)a1);
		case SYS_env_set_trapframe:
			return (int32_t)sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
		case SYS_time_msec:
			return (int32_t)sys_time_msec();
		case SYS_tx_packet:
			return (int32_t)sys_tx_packet((char *)a1, (int)a2);
		case SYS_rx_packet:
			return (int32_t)sys_rx_packet((char *)a1);
		default:
			return -E_INVAL;
	}
}

