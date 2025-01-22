#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

volatile uint32_t *e1000_base = NULL;

// Macros
#define E1000_REG(offset) (e1000_base[offset / 4])

// Allocate a region of memory for the transmit descriptor list. 
// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
struct e1000_tx_desc tx_desc[E1000_TX_DESC_COUNT] = {0};
char tx_buf[E1000_TX_DESC_COUNT * E1000_TX_DESC_SIZE_BYTES] = {0};

// Allocate a region of memory for the receive descriptor list.
// Software should insure this memory is aligned on a paragraph (16-byte) boundary.
struct e1000_rx_desc rx_desc[E1000_RX_DESC_COUNT] = {0};
char rx_buf[E1000_RX_DESC_COUNT * E1000_RX_DESC_SIZE_BYTES] = {0};

// Private function definitions

static void e1000_tx_init()
{
    int i = 0;

    // Reserve memory for the transmit descriptor array and the packet buffers pointed to by the transmit descriptors.
    for (i = 0; i < E1000_TX_DESC_COUNT; i++)
    {
        tx_desc[i].addr = PADDR(tx_buf + (i * E1000_TX_DESC_SIZE_BYTES));
        tx_desc[i].length = E1000_TX_DESC_SIZE_BYTES;
        tx_desc[i].status |= E1000_TXD_STAT_DD;
        tx_desc[i].cmd |= (1) | (1 << 3);       // Setting EOP + Report Status (section 3.3.3.1)
    }

    // Program the Transmit Descriptor Base Address (TDBAL/TDBAH) register(s) with the address of the
    // region. TDBAL is used for 32-bit addresses and both TDBAL and TDBAH are used for 64-bit addresses.
    E1000_REG(E1000_TDBAL) = PADDR(tx_desc);
    E1000_REG(E1000_TDBAH) = 0;

    // Set the Transmit Descriptor Length (TDLEN) register to the size (in bytes) of the descriptor ring.
    // This register must be 128-byte aligned.
    E1000_REG(E1000_TDLEN) = sizeof(tx_desc);

    // The Transmit Descriptor Head and Tail (TDH/TDT) registers are initialized (by hardware) to 0b
    // after a power-on or a software initiated Ethernet controller reset. Software should write 0b to both
    // these registers to ensure this.
    E1000_REG(E1000_TDH) = 0;
    E1000_REG(E1000_TDT) = 0;

    // Set the Enable (TCTL.EN) bit to 1b for normal operation.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_EN;

    // Set the Pad Short Packets (TCTL.PSP) bit to 1b.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_PSP;

    // Configure the Collision Distance (TCTL.COLD) to its expected value. For full duplex
    // operation, this value should be set to 40h. For gigabit half duplex, this value should be set to
    // 200h. For 10/100 half duplex, this value should be set to 40h.
    E1000_REG(E1000_TCTL) |= E1000_TCTL_COLD & (0x40 << 12);

    // Set the Inter Packet Gap to the default values as listed in section 13.4.34
    E1000_REG(E1000_TIPG) = 0xA | (0x4 << 10) | (0x6 << 20);
}

static void e1000_rx_init()
{
    int i = 0;

    // Program the Receive Address Register(s) (RAL/RAH) with the desired Ethernet addresses.
    // MAC Address: 52:54:00:12:34:56
    // Be very careful with the byte order; MAC addresses are written from lowest-order byte to highest-order byte
    E1000_REG(E1000_RAL) = 0x12005452;
    E1000_REG(E1000_RAH) = 0x5634;
    E1000_REG(E1000_RAH) |= E1000_RAH_AV;

    // Initialize the MTA (Multicast Table Array) to 0b.
    E1000_REG(E1000_MTA) = 0;

    // Program the Interrupt Mask Set/Read (IMS) register to enable any interrupt the software
    // driver wants to be notified of when the event occurs. Per JOS lab description, don't
    // enable these for now.
    E1000_REG(E1000_IMS) = 0;

    // Reserve memory for the receieve descriptor array and the packet buffers pointed to by the receive descriptors.
    for (i = 0; i < E1000_RX_DESC_COUNT; i++)
    {
        rx_desc[i].addr = PADDR(rx_buf + (i * E1000_RX_DESC_SIZE_BYTES));
        rx_desc[i].length = E1000_RX_DESC_SIZE_BYTES;
        rx_desc[i].status &= ~E1000_RXD_STAT_DD;
    }

    // Program the Receive Descriptor Base Address (RDBAL/RDBAH) register(s) with the address of the
    // region. RDBAL is used for 32-bit addresses and both RDBAL and RDBAH are used for 64-bit addresses.
    E1000_REG(E1000_RDBAL) = PADDR(rx_desc);
    E1000_REG(E1000_RDBAH) = 0;

    // Set the Receive Descriptor Length (RDLEN) register to the size (in bytes) of the descriptor ring.
    // This register must be 128-byte aligned
    E1000_REG(E1000_RDLEN) = sizeof(rx_desc);

    // Software initializes the Receive Descriptor Head (RDH) register and Receive Descriptor Tail (RDT) with the
    // appropriate head and tail addresses. Head should point to the first valid receive descriptor in the
    // descriptor ring and tail should point to one descriptor beyond the last valid descriptor in the
    // descriptor ring.
    // From JOS: when the network is idle, the transmit queue is empty (because all packets have been sent),
    // but the receive queue is full (of empty packet buffers).
    E1000_REG(E1000_RDH) = 0;
    E1000_REG(E1000_RDT) = E1000_RX_DESC_COUNT - 1;

    // Set the receiver Enable (RCTL.EN) bit to 1b for normal operation.
    E1000_REG(E1000_RCTL) |= E1000_RCTL_EN;

    // Set the Broadcast Accept Mode (RCTL.BAM) bit to 1b allowing the hardware to accept broadcast packets.
    E1000_REG(E1000_RCTL) |= E1000_RCTL_BAM;

    // Set the Strip Ethernet CRC (RCTL.SECRC) bit if the desire is for hardware to strip the CRC prior to
    // DMA-ing the receive packet to host memory.
    E1000_REG(E1000_RCTL) |= E1000_RCTL_SECRC;
}

// Function Definitions

int e1000_attach(struct pci_func *pcif)
{
    pci_func_enable(pcif);

    e1000_base = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(E1000_REG(E1000_STATUS) == 0x80080783);

    // Transmit Initialization
    e1000_tx_init();

    // Receive Initialization
    e1000_rx_init();

    return 0;
}

int e1000_tx(char *buf, int size)
{
    int i = E1000_REG(E1000_TDT);

    assert(size <= E1000_PACKET_SIZE_BYTES);

    if (!(tx_desc[i].status & E1000_TXD_STAT_DD))
        return -E_NIC_BUSY;

    tx_desc[i].status &= ~E1000_TXD_STAT_DD;
    memcpy(&tx_buf[i * E1000_PACKET_SIZE_BYTES], buf, size);
    tx_desc[i].length = size;
    i = (i + 1) % E1000_TX_DESC_COUNT;
    E1000_REG(E1000_TDT) = i;

    return 0;
}

int e1000_rx(char *buf)
{
    int i = (E1000_REG(E1000_RDT) + 1) % E1000_RX_DESC_COUNT;

    if (!(rx_desc[i].status & E1000_RXD_STAT_DD))
        return -E_RX_EMPTY;

    rx_desc[i].status &= ~E1000_RXD_STAT_DD;
    memcpy(buf, &rx_buf[i * E1000_PACKET_SIZE_BYTES], rx_desc[i].length);
    E1000_REG(E1000_RDT) = i;

    return rx_desc[i].length;
}
