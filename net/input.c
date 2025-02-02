#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int rc = 0;

	sys_page_unmap(0, &nsipcbuf);

	while (true)
	{
		sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_W | PTE_P);
		do
		{
			rc = sys_rx_packet(nsipcbuf.pkt.jp_data);
			if (rc == -E_RX_EMPTY)
				sys_yield();
		} while(rc == -E_RX_EMPTY);

		if (rc < 0)
			panic("Error - unexpected error when rx packet %e", rc);

		nsipcbuf.pkt.jp_len = rc;

		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U);
		sys_page_unmap(0, &nsipcbuf);
	}
}
