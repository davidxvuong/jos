#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	int rc = 0;
	envid_t envid;
	int perm = 0;
	struct jif_pkt *pkt = NULL;
	int i = 0;
	char *buf = NULL;
	int tx_size = 0;

	while (true)
	{
		rc = ipc_recv(&envid, &nsipcbuf, &perm);

		if (rc == NSREQ_OUTPUT && envid == ns_envid && perm & PTE_P)
		{
			pkt = (struct jif_pkt *)&nsipcbuf.pkt;
			buf = pkt->jp_data;

			for (i = 0; i < pkt->jp_len; i += E1000_PACKET_SIZE_BYTES)
			{
				tx_size = MIN(pkt->jp_len - (i * E1000_PACKET_SIZE_BYTES), E1000_PACKET_SIZE_BYTES);
				while((rc = sys_tx_packet(&buf[i], tx_size)) != 0)
				{
					if (rc == -E_NIC_BUSY)
						sys_yield();
					else
						panic("Error - unexpected error when tx packet %e", rc);
				}
			}
		}
		else
			ipc_send(envid, NRES_INVALID_REQ, NULL, 0);

		sys_page_unmap(0, &nsipcbuf);
	}
}
