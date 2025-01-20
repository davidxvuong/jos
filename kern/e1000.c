#include <kern/e1000.h>
#include <kern/pmap.h>

volatile uint32_t *e1000_base = NULL;

// Macros
#define E1000_REG(offset) (e1000_base[offset / 4])

// Allocate a region of memory for the transmit descriptor list. 
// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
struct e1000_tx_desc tx_array[E1000_TX_DESC_COUNT] = {0};
char tx_buf[E1000_TX_DESC_COUNT * E1000_PACKET_SIZE_BYTES] = {0};

// Function Definitions

int e1000_attach(struct pci_func *pcif)
{
    int i = 0;

    pci_func_enable(pcif);

    e1000_base = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(E1000_REG(E1000_STATUS) == 0x80080783);

    // Transmit Initialization

    // Reserve memory for the transmit descriptor array and the packet buffers pointed to by the transmit descriptors.
    for (i = 0; i < E1000_TX_DESC_COUNT; i++)
    {
        tx_array[i].addr = PADDR(tx_buf + (E1000_TX_DESC_COUNT * E1000_PACKET_SIZE_BYTES));
        tx_array[i].length = E1000_PACKET_SIZE_BYTES;
        tx_array[i].status |= E1000_TXD_STAT_DD & 1;
        tx_array[i].cmd |= (E1000_TXD_CMD_EOP & 1) | (E1000_TXD_CMD_RS & (1 << 3));
    }

    // Program the Transmit Descriptor Base Address (TDBAL/TDBAH) register(s) with the address of the
    // region. TDBAL is used for 32-bit addresses and both TDBAL and TDBAH are used for 64-bit addresses.
    E1000_REG(E1000_TDBAL) = PADDR(tx_array);
    E1000_REG(E1000_TDBAH) = 0;

    // Set the Transmit Descriptor Length (TDLEN) register to the size (in bytes) of the descriptor ring.
    // This register must be 128-byte aligned.
    E1000_REG(E1000_TDLEN) = E1000_TX_DESC_COUNT * E1000_PACKET_SIZE_BYTES;

    // The Transmit Descriptor Head and Tail (TDH/TDT) registers are initialized (by hardware) to 0b
    // after a power-on or a software initiated Ethernet controller reset. Software should write 0b to both
    // these registers to ensure this.
    E1000_REG(E1000_TDH) = 0;
    E1000_REG(E1000_TDT) = 0;

    // Set the Enable (TCTL.EN) bit to 1b for normal operation.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_EN & 1;

    // Set the Pad Short Packets (TCTL.PSP) bit to 1b.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_PSP & (1 << 3);

    // Configure the Collision Distance (TCTL.COLD) to its expected value. For full duplex
    // operation, this value should be set to 40h. For gigabit half duplex, this value should be set to
    // 200h. For 10/100 half duplex, this value should be set to 40h.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_COLD & (0x40 << 12);

    // Set the Inter Packet Gap to the default values as listed in section 13.4.34
    E1000_REG(E1000_TIPG) = 0xA | (0x4 << 10) | (0x6 << 20);

    return 0;
}
