#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

#define E1000_VEND_ID                       0x8086
#define E1000_DESKTOP_DEV_ID                0x100E
#define E1000_MOBILE_DEV_ID                 0x1015

int e1000_attach(struct pci_func *pcif);

#endif  // SOL >= 6
