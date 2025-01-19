#include <kern/e1000.h>
#include <kern/pmap.h>

volatile uint32_t *e1000_base = NULL;

// Macros
#define E1000_REG(offset) (e1000_base[offset / 4])

int e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);

    e1000_base = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(E1000_REG(E1000_STATUS) == 0x80080783);

    return 0;
}
