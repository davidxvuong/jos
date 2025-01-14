// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r, perm;
	envid_t envid = sys_getenvid();

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	pte_t pte = uvpt[PGNUM(addr)];

	if (((pte & PTE_COW) | PTE_W) != (PTE_COW | PTE_W))
	{
		panic("Error - unable to access page table corresponding to addr 0x%x", (uintptr_t)addr);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	perm = PTE_P | PTE_U | PTE_W;
	addr = ROUNDDOWN(addr, PGSIZE);

	r = sys_page_alloc(envid, PFTEMP, perm);
	if (r != 0)
	{
		panic("Error - failed to allocate page %c", r);
	}

	memmove(PFTEMP, addr, PGSIZE);

	r = sys_page_map(envid, PFTEMP, envid, addr, perm);
	if (r != 0)
	{
		panic("Error - failed to map page %c", r);
	}

	r = sys_page_unmap(envid, PFTEMP);
	if (r != 0)
	{
		panic("Error - failed to unmap page %c", r);
	}
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
duppage(envid_t envid, unsigned pn)
{
	int r, perm;
	pte_t pte = uvpt[pn];
	void *addr = (void *)(pn * PGSIZE);
	envid_t srcid = sys_getenvid();

	if (((pte & PTE_COW) || (pte & PTE_W)) && !(pte & PTE_SHARE))
	{
		perm = ((pte & ~PTE_W) | PTE_COW) & PTE_SYSCALL;
		r = sys_page_map(srcid, addr, envid, addr, perm);
		if (r != 0)
			panic("Error - src_page_map failed %e", r);

		r = sys_page_map(srcid, addr, srcid, addr, perm);
		if (r != 0)
			panic("Error - src_page_map failed %e", r);
	}
	else
	{
		perm = pte & PTE_SYSCALL;
		r = sys_page_map(srcid, addr, envid, addr, perm);
		if (r != 0)
			panic("Error - src_page_map failed %e", r);

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
fork(void)
{
	int rc = 0, i = 0, j = 0;
	envid_t envid;
	uint32_t pn = 0;

	set_pgfault_handler(pgfault);

	envid = sys_exofork();
	if (envid < 0)
		panic("Error -failed to fork the new process %e", envid);
	else if (envid == 0)
	{
		// Child process
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// Parent process
	for (i = 0; i < NPDENTRIES; i++)
	{
		if (!(uvpd[i] & PTE_P))
			continue;

		for (j = 0; j < NPTENTRIES; j++)
		{
			pn = i * NPDENTRIES + j;

			if (pn == PGNUM(UXSTACKTOP - PGSIZE)) continue;		// Page is the user exception stack
			else if (pn >= PGNUM(UTOP - PGSIZE)) continue;		// Page is above UTOP
			else if (!(uvpt[pn] & PTE_P)) continue;				// No page present
			duppage(envid, pn);
		}
	}

	rc = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (rc != 0)
		panic("Error - failed to set page fault upcall %e", rc);

	rc = sys_page_alloc(envid,(void *)(UXSTACKTOP - PGSIZE) , PTE_U | PTE_W);
	if (rc != 0)
		panic("Error - failed to allocate child user exception stack %e", rc);

	rc = sys_env_set_status(envid, ENV_RUNNABLE);
	if (rc != 0)
		panic("Error - failed to set child environment to runnable %e", rc);

	return envid;

}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
