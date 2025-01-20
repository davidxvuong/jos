#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

// E1000 Device ID's
#define E1000_VEND_ID                       0x8086
#define E1000_DESKTOP_DEV_ID                0x100E
#define E1000_MOBILE_DEV_ID                 0x1015

// E1000 Registers
// This is taken from https://pdos.csail.mit.edu/6.828/2018/labs/lab6/e1000_hw.h as needed
#define E1000_STATUS                        0x00008  /* Device Status - RO */
#define E1000_TDBAL                         0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH                         0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN                         0x03808  /* TX Descriptor Length - RW */
#define E1000_TDH                           0x03810  /* TX Descriptor Head - RW */
#define E1000_TDT                           0x03818  /* TX Descripotr Tail - RW */
#define E1000_TCTL                          0x00400  /* TX Control - RW */
#define E1000_TCTL_EXT                      0x00404  /* Extended TX Control - RW */
#define E1000_TIPG                          0x00410  /* TX Inter-packet gap -RW */

// TX Descriptor bit definitions
#define E1000_TXD_CMD_EOP                   0x01000000 /* End of Packet */
#define E1000_TXD_CMD_RS                    0x08000000 /* Report Status */
#define E1000_TXD_CMD_DEXT                  0x20000000 /* Descriptor extension (0 = legacy) */
#define E1000_TXD_STAT_DD                   0x00000001 /* Descriptor Done */

// TX Control
#define E1000_TCTL_EN                       0x00000002    /* enable tx */
#define E1000_TCTL_PSP                      0x00000008    /* pad short packets */
#define E1000_TCTL_CT                       0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD                     0x003ff000    /* collision distance */

// E1000/JOS Definitions
#define E1000_TX_DESC_COUNT                 8
#define E1000_PACKET_SIZE_BYTES             1518

// Transmit Descriptor
struct e1000_tx_desc
{
	uint64_t addr;              /* Address of the descriptor's data buffer */
	uint16_t length;            /* Data buffer length */
	uint8_t cso;                /* Checksum offset */
	uint8_t cmd;                /* Descriptor control */
	uint8_t status;             /* Descriptor status */
	uint8_t css;                /* Checksum start */
	uint16_t special;           /* See section 3.3.3 in manual */
} __attribute__((packed));;


// Function definitions
int e1000_attach(struct pci_func *pcif);
int e1000_tx(char *buf, int size);

#endif  // SOL >= 6
