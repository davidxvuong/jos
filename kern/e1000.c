#include <kern/e1000.h>
#include <kern/pmap.h>

int e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);

    return 0;
}
