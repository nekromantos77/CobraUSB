
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <lv2/libc.h>

#include <lv1/lv1.h>
#include <lv1/mm.h>
#include "system.h"
#include "if_ether.h"
#include "gelic.h"

#define GELIC_NET_MAX_MTU						VLAN_ETH_FRAME_LEN

#define GELIC_BUS_ID							1
#define GELIC_DEV_ID							0

#define GELIC_DMA_BASE							0x8000000013380000ULL
#define GELIC_DMA_OFFSET						0x13380000ULL
#define GELIC_DMA_PAGE_SIZE						12
#define GELIC_DMA_SIZE							(1 << GELIC_DMA_PAGE_SIZE)

#define GELIC_PORT_ETHERNET_0					0
#define GELIC_PORT_WIRELESS						1

#define GELIC_LV1_GET_MAC_ADDRESS				1
#define GELIC_LV1_GET_ETH_PORT_STATUS			2
#define GELIC_LV1_SET_NEGOTIATION_MODE			3
#define GELIC_LV1_GET_VLAN_ID					4
#define GELIC_LV1_SET_WOL						5
#define GELIC_LV1_GET_CHANNEL					6
#define GELIC_LV1_POST_WLAN_CMD					9
#define GELIC_LV1_GET_WLAN_CMD_RESULT			10
#define GELIC_LV1_GET_WLAN_EVENT				11

#define GELIC_LV1_VLAN_TX_ETHERNET_0			0x0000000000000002L
#define GELIC_LV1_VLAN_TX_WIRELESS				0x0000000000000003L
#define GELIC_LV1_VLAN_RX_ETHERNET_0			0x0000000000000012L
#define GELIC_LV1_VLAN_RX_WIRELESS				0x0000000000000013L

#define GELIC_DESCR_DMA_COMPLETE				0x00000000
#define GELIC_DESCR_DMA_BUFFER_FULL				0x00000000
#define GELIC_DESCR_DMA_RESPONSE_ERROR			0x10000000
#define GELIC_DESCR_DMA_PROTECTION_ERROR		0x20000000
#define GELIC_DESCR_DMA_FRAME_END				0x40000000
#define GELIC_DESCR_DMA_FORCE_END				0x50000000
#define GELIC_DESCR_DMA_CARDOWNED				0xA0000000
#define GELIC_DESCR_DMA_NOT_IN_USE				0xB0000000

#define GELIC_DESCR_DMA_STAT_MASK				0xF0000000

#define GELIC_DESCR_TX_DMA_IKE					0x00080000
#define GELIC_DESCR_TX_DMA_FRAME_TAIL			0x00040000
#define GELIC_DESCR_TX_DMA_TCP_CHKSUM			0x00020000
#define GELIC_DESCR_TX_DMA_UDP_CHKSUM			0x00030000
#define GELIC_DESCR_TX_DMA_NO_CHKSUM			0x00000000
#define GELIC_DESCR_TX_DMA_CHAIN_END			0x00000002

#define GELIC_DESCR_RXDMADU						0x80000000
#define GELIC_DESCR_RXLSTFBF					0x40000000
#define GELIC_DESCR_RXIPCHK						0x20000000
#define GELIC_DESCR_RXTCPCHK					0x10000000
#define GELIC_DESCR_RXWTPKT						0x00C00000
#define GELIC_DESCR_RXVLNPKT					0x00200000
#define GELIC_DESCR_RXRRECNUM					0x0000ff00

#define GELIC_DESCR_RXALNERR					0x40000000
#define GELIC_DESCR_RXOVERERR					0x20000000
#define GELIC_DESCR_RXRNTERR					0x10000000
#define GELIC_DESCR_RXIPCHKERR					0x08000000
#define GELIC_DESCR_RXTCPCHKERR					0x04000000
#define GELIC_DESCR_RXDRPPKT					0x00100000
#define GELIC_DESCR_RXIPFMTERR					0x00080000
#define GELIC_DESCR_RXDATAERR					0x00020000
#define GELIC_DESCR_RXCALERR					0x00010000
#define GELIC_DESCR_RXCREXERR					0x00008000
#define GELIC_DESCR_RXMLTCST					0x00004000

#define PKT_HDR_SIZE							16
#define PKT_MAGIC								0x0FACE0FF
#define PKT_FLAG_LAST							0x00000001

struct gelic_var
{
	u64 dma_lpar_addr;
	u32 dma_bus_addr;
	u64 pte_index;
	u8 mac_addr[VLAN_ETH_ALEN];
	u64 vlan_id;
};

struct gelic_descr
{
	u32 buf_addr;
	u32 buf_size;
	u32 next_descr_addr;
	u32 dmac_cmd_status;
	u32 result_size;
	u32 valid_size;
	u32 data_status;
	u32 data_error;
};

struct pkt_hdr
{
	u32 magic;
	u32 offset;
	u32 size;
	u32 flags;
};

static struct gelic_var gelic_var;

const u8 gelic_bcast_mac_addr[VLAN_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#define HPTE_V_BOLTED			0x0000000000000010ULL
#define HPTE_V_LARGE			0x0000000000000004ULL
#define HPTE_V_VALID			0x0000000000000001ULL
#define HPTE_R_PROT_MASK		0x0000000000000003ULL
int mm_insert_htab_entry2(u64 va_addr, u64 lpar_addr, u64 prot, u64 *index)
{
	u64 hpte_group, hpte_index, hpte_v, hpte_r, hpte_evicted_v, hpte_evicted_r;
	int result;

	hpte_group = (((va_addr >> 28) ^ ((va_addr & 0xFFFFFFFULL) >> 12)) & 0x7FF) << 3;
	hpte_v = ((va_addr >> 23) << 7) | HPTE_V_VALID;
	hpte_r = lpar_addr | 0x38 | (prot & HPTE_R_PROT_MASK);

	result = lv1_insert_htab_entry(0, hpte_group, hpte_v, hpte_r, HPTE_V_BOLTED, 0,
		&hpte_index, &hpte_evicted_v, &hpte_evicted_r);

	if ((result == 0) && (index != 0))
		*index = hpte_index;

	return result;
}


int gelic_init(void)
{
	u64 dma_lpar_addr, muid, dma_bus_addr, mac_addr, vlan_id, dummy;
	int result;

	result = lv1_allocate_memory(GELIC_DMA_SIZE, GELIC_DMA_PAGE_SIZE, 0, 0,
		&dma_lpar_addr, &muid);
	if (result != 0)
		return result;

	result = lv1_allocate_device_dma_region(GELIC_BUS_ID, GELIC_DEV_ID,
		GELIC_DMA_SIZE, GELIC_DMA_PAGE_SIZE, 0, &dma_bus_addr);
	if (result != 0)
		return result;

	result = lv1_map_device_dma_region(GELIC_BUS_ID, GELIC_DEV_ID, dma_lpar_addr, dma_bus_addr,
		GELIC_DMA_SIZE, 0xF800000000000000ULL);
	if (result != 0)
		return result;

	result = mm_insert_htab_entry2(MM_EA2VA(GELIC_DMA_BASE), dma_lpar_addr, 0, &gelic_var.pte_index);
	if (result != 0)
		return result;

	result = lv1_net_control(GELIC_BUS_ID, GELIC_DEV_ID, GELIC_LV1_GET_MAC_ADDRESS,
		0, 0, 0, &mac_addr, &dummy);
	if (result != 0)
		return result;

	result = lv1_net_control(GELIC_BUS_ID, GELIC_DEV_ID, GELIC_LV1_GET_VLAN_ID,
		GELIC_LV1_VLAN_TX_ETHERNET_0, 0, 0, &vlan_id, &dummy);
	if (result != 0)
		vlan_id = 0;

	result = lv1_net_set_interrupt_mask(GELIC_BUS_ID, GELIC_DEV_ID, 0, 0);
	if (result != 0)
		return result;

	gelic_var.dma_lpar_addr = dma_lpar_addr;
	gelic_var.dma_bus_addr = dma_bus_addr;
	mac_addr <<= 16;
	memcpy(gelic_var.mac_addr, &mac_addr, VLAN_ETH_ALEN);
	gelic_var.vlan_id = vlan_id;

	return 0;
}

int gelic_deinit(void)
{
	volatile struct gelic_descr *descr;
	int result;

	MM_LOAD_BASE(descr, GELIC_DMA_OFFSET);

	descr->dmac_cmd_status = GELIC_DESCR_DMA_NOT_IN_USE;
	descr->next_descr_addr = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;
	descr->buf_addr = 0;
	descr->buf_size = 0;

	wmb();

	result = lv1_net_stop_rx_dma(GELIC_BUS_ID, GELIC_DEV_ID, 0);
	if (result != 0)
		return result;

	result = lv1_net_stop_tx_dma(GELIC_BUS_ID, GELIC_DEV_ID, 0);
	if (result != 0)
		return result;

	result = lv1_unmap_device_dma_region(GELIC_BUS_ID, GELIC_DEV_ID,
		gelic_var.dma_bus_addr, GELIC_DMA_SIZE);
	if (result != 0)
		return result;

	result = lv1_free_device_dma_region(GELIC_BUS_ID, GELIC_DEV_ID, gelic_var.dma_bus_addr);
	if (result != 0)
		return result;

	result = lv1_write_htab_entry(0, gelic_var.pte_index, 0, 0);
	if (result != 0)
		return result;

	result = lv1_release_memory(gelic_var.dma_lpar_addr);
	if (result != 0)
		return result;

	return 0;
}

int gelic_xmit(const u8 dest_mac_addr[VLAN_ETH_ALEN], u16 proto, const void *data, u64 size)
{
	volatile struct gelic_descr *descr;
	struct vlan_eth_hdr *vlan_eth_hdr;
	int result;

	if (size > VLAN_ETH_DATA_LEN)
		return -1;

	MM_LOAD_BASE(descr, GELIC_DMA_OFFSET);

	descr->dmac_cmd_status = GELIC_DESCR_DMA_CARDOWNED |
		GELIC_DESCR_TX_DMA_IKE |
		GELIC_DESCR_TX_DMA_NO_CHKSUM |
		GELIC_DESCR_TX_DMA_FRAME_TAIL;
	descr->next_descr_addr = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;
	descr->buf_addr = gelic_var.dma_bus_addr + 0x100;
	descr->buf_size = VLAN_ETH_HLEN + size;

	vlan_eth_hdr = (struct vlan_eth_hdr *) ((u8 *) descr + 0x100);
	memcpy(vlan_eth_hdr->dest, dest_mac_addr, VLAN_ETH_ALEN);
	memcpy(vlan_eth_hdr->src, gelic_var.mac_addr, VLAN_ETH_ALEN);
	vlan_eth_hdr->proto = ETH_P_8021Q;
	vlan_eth_hdr->tci = gelic_var.vlan_id & VLAN_VID_MASK;
	vlan_eth_hdr->encap_proto = proto;
	memcpy((u8 *) vlan_eth_hdr + VLAN_ETH_HLEN, data, size);

	wmb();

	result = lv1_net_start_tx_dma(GELIC_BUS_ID, GELIC_DEV_ID, gelic_var.dma_bus_addr, 0);
	if (result != 0)
		return result;

	for (;;)
	{
		if (!((descr->dmac_cmd_status & GELIC_DESCR_DMA_STAT_MASK) == GELIC_DESCR_DMA_CARDOWNED))
			break;
	}

	return 0;
}

int gelic_recv(void *data, u64 max_size)
{
#define MIN(a, b)	((a) <= (b) ? (a) : (b))

	volatile struct gelic_descr *descr;
	int status, size, n, result;

again:

	MM_LOAD_BASE(descr, GELIC_DMA_OFFSET);

	descr->dmac_cmd_status = GELIC_DESCR_DMA_CARDOWNED;
	descr->next_descr_addr = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;
	descr->buf_addr = gelic_var.dma_bus_addr + 0x100;
	descr->buf_size = GELIC_NET_MAX_MTU;

	wmb();

	result = lv1_net_start_rx_dma(GELIC_BUS_ID, GELIC_DEV_ID, gelic_var.dma_bus_addr, 0);
	if (result != 0)
		return result;

	for (;;)
	{
		if (!((descr->dmac_cmd_status & GELIC_DESCR_DMA_STAT_MASK) == GELIC_DESCR_DMA_CARDOWNED))
			break;
	}

	status = descr->dmac_cmd_status & GELIC_DESCR_DMA_STAT_MASK;
	if ((status == GELIC_DESCR_DMA_NOT_IN_USE) ||
		(status == GELIC_DESCR_DMA_RESPONSE_ERROR) ||
		(status == GELIC_DESCR_DMA_PROTECTION_ERROR) ||
		(status == GELIC_DESCR_DMA_FORCE_END))
		goto again;
	
	descr->next_descr_addr = 0;
	descr->dmac_cmd_status &= ~GELIC_DESCR_DMA_STAT_MASK;
	descr->dmac_cmd_status |= GELIC_DESCR_DMA_NOT_IN_USE;

	wmb();

	if (descr->valid_size)
		size = descr->valid_size;
	else
		size = descr->result_size;

	n = MIN(size, max_size);
	
	memcpy(data, (u8 *) descr + 0x100, n);

	return n;

#undef MIN
}

int gelic_xmit_data(const u8 dest_mac_addr[VLAN_ETH_ALEN], u16 proto, const void *data, u64 size)
{
#define MIN(a, b)	((a) <= (b) ? (a) : (b))

	u64 offset, pkt_size;
	int result;

	offset = 0;

	while (offset < size)
	{
		pkt_size = MIN(size - offset, ETH_DATA_LEN);

		result = gelic_xmit(dest_mac_addr, proto, (u8 *) data + offset, pkt_size);
		if (result != 0)
			return result;

		offset += pkt_size;
	}

	return 0;

#undef MIN
}

int gelic_recv_data(const void *data, u64 max_size)
{
	static u8 buf[ETH_FRAME_LEN + 2];
	struct eth_hdr *eth_hdr;
	struct pkt_hdr *pkt_hdr;
	u64 offset;
	u32 flags;
 	int result;

	offset = 0;

	for (;;)
	{
		result = gelic_recv(buf, sizeof(buf));
		if (result < (ETH_HLEN + 2 + PKT_HDR_SIZE))
			continue;
		
		eth_hdr = (struct eth_hdr *) (buf + 2);

		pkt_hdr = (struct pkt_hdr *) (eth_hdr + 1);
		if (pkt_hdr->magic != PKT_MAGIC)
			continue;

		if (offset != pkt_hdr->offset)
			continue;

		memcpy((u8 *) data + offset, pkt_hdr + 1, pkt_hdr->size);

		offset += pkt_hdr->size;
		flags = pkt_hdr->flags;

		pkt_hdr->magic = PKT_MAGIC;
		pkt_hdr->offset = offset;
		pkt_hdr->size = 0;
		pkt_hdr->flags = 0;
		if (flags & PKT_FLAG_LAST)
			pkt_hdr->flags |= PKT_FLAG_LAST;

		result = gelic_xmit_data(eth_hdr->src, eth_hdr->proto, pkt_hdr, sizeof(struct pkt_hdr));
		if (result != 0)
			return result;

		if (flags & PKT_FLAG_LAST)
			break;
	}

	return offset;
}
