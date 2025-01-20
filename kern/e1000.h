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

// Transmit Descriptor Fields
// Each transmit descriptor is 16 bytes long and contains the following fields:

// Buffer Address (64 bits):
// A pointer to the location in memory where the packet data resides.
// Length (16 bits):
// Specifies the length of the data in the buffer (in bytes).
// Checksum Offset (CSO, 8 bits):
// Indicates where the card should begin checksum calculations within the buffer.
// Command (8 bits):
// Defines actions the card should take for this descriptor, such as initiating transmission or enabling interrupts.
// Status (8 bits):
// Provides feedback from the controller on the processing state of the descriptor.
// Checksum Start Field (CSS, 8 bits):
// Specifies the starting byte for checksum calculations.
// Special (16 bits):
// Reserved for special purposes, often application-specific.
struct e1000_tx_desc
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
} __attribute__((packed));;


// Function definitions
int e1000_attach(struct pci_func *pcif);

#endif  // SOL >= 6
