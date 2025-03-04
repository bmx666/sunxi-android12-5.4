/*
 * linux/drivers/net/ethernet/allwinner/sunxi_gmac_ops.c
 *
 * Copyright © 2016-2018, fuzhaoke
 *		Author: fuzhaoke <fuzhaoke@allwinnertech.com>
 *
 * This file is provided under a dual BSD/GPL license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/printk.h>
#include <linux/io.h>
#include "sunxi-gmac.h"

/******************************************************************************
 *	sun8iw6 operations
 *****************************************************************************/
#define GETH_BASIC_CTL0		0x00
#define GETH_BASIC_CTL1		0x04
#define GETH_INT_STA		0x08
#define GETH_INT_EN		0x0C
#define GETH_TX_CTL0		0x10
#define GETH_TX_CTL1		0x14
#define GETH_TX_FLOW_CTL	0x1C
#define GETH_TX_DESC_LIST	0x20
#define GETH_RX_CTL0		0x24
#define GETH_RX_CTL1		0x28
#define GETH_RX_DESC_LIST	0x34
#define GETH_RX_FRM_FLT		0x38
#define GETH_RX_HASH0		0x40
#define GETH_RX_HASH1		0x44
#define GETH_MDIO_ADDR		0x48
#define GETH_MDIO_DATA		0x4C
#define GETH_ADDR_HI(reg)	(0x50 + ((reg) << 3))
#define GETH_ADDR_LO(reg)	(0x54 + ((reg) << 3))
#define GETH_TX_DMA_STA		0xB0
#define GETH_TX_CUR_DESC	0xB4
#define GETH_TX_CUR_BUF		0xB8
#define GETH_RX_DMA_STA		0xC0
#define GETH_RX_CUR_DESC	0xC4
#define GETH_RX_CUR_BUF		0xC8
#define GETH_RGMII_STA		0xD0

#define RGMII_IRQ		0x00000001

#define	CTL0_LM			0x02
#define CTL0_DM			0x01
#define CTL0_SPEED		0x04

#define BURST_LEN		0x3F000000
#define RX_TX_PRI		0x02
#define SOFT_RST		0x01

#define TX_FLUSH		0x01
#define TX_MD			0x02
#define TX_NEXT_FRM		0x04
#define TX_TH			0x0700

#define RX_FLUSH		0x01
#define RX_MD			0x02
#define RX_RUNT_FRM		0x04
#define RX_ERR_FRM		0x08
#define RX_TH			0x0030

#define TX_INT			0x00001
#define TX_STOP_INT		0x00002
#define TX_UA_INT		0x00004
#define TX_TOUT_INT		0x00008
#define TX_UNF_INT		0x00010
#define TX_EARLY_INT		0x00020
#define RX_INT			0x00100
#define RX_UA_INT		0x00200
#define RX_STOP_INT		0x00400
#define RX_TOUT_INT		0x00800
#define RX_OVF_INT		0x01000
#define RX_EARLY_INT		0x02000
#define LINK_STA_INT		0x10000

#define DISCARD_FRAME	-1
#define GOOD_FRAME	0
#define CSUM_NONE	2
#define LLC_SNAP	4

#define SF_DMA_MODE		1

/* Flow Control defines */
#define FLOW_OFF	0
#define FLOW_RX		1
#define FLOW_TX		2
#define FLOW_AUTO	(FLOW_TX | FLOW_RX)

#define HASH_TABLE_SIZE 64
#define PAUSE_TIME 0x200
#define GMAC_MAX_UNICAST_ADDRESSES	8

/* PHY address */
#define PHY_ADDR		0x01
#define PHY_DM			0x0010
#define PHY_AUTO_NEG		0x0020
#define PHY_POWERDOWN		0x0080
#define PHY_NEG_EN		0x1000

#define MII_BUSY		0x00000001
#define MII_WRITE		0x00000002
#define MII_PHY_MASK		0x0000FFC0
#define MII_CR_MASK		0x0000001C
#define MII_CLK			0x00000008
/* bits 4 3 2 | AHB1 Clock	| MDC Clock
 * -------------------------------------------------------
 *      0 0 0 | 60 ~ 100 MHz	| div-42
 *      0 0 1 | 100 ~ 150 MHz	| div-62
 *      0 1 0 | 20 ~ 35 MHz	| div-16
 *      0 1 1 | 35 ~ 60 MHz	| div-26
 *      1 0 0 | 150 ~ 250 MHz	| div-102
 *      1 0 1 | 250 ~ 300 MHz	| div-124
 *      1 1 x | Reserved	|
 */

enum csum_insertion {
	cic_dis		= 0, /* Checksum Insertion Control */
	cic_ip		= 1, /* Only IP header */
	cic_no_pse	= 2, /* IP header but not pseudoheader */
	cic_full	= 3, /* IP header and pseudoheader */
};

struct gethdev {
	void *iobase;
	unsigned int ver;
	unsigned int mdc_div;
};

static struct gethdev hwdev;

/***************************************************************************
 * External interface
 **************************************************************************/
/* Set a ring desc buffer */
void desc_init_chain(struct dma_desc *desc, unsigned long addr, unsigned int size)
{
	/* In chained mode the desc3 points to the next element in the ring.
	 * The latest element has to point to the head.
	 */
	int i;
	struct dma_desc *p = desc;
	unsigned long dma_phy = addr;

	for (i = 0; i < (size - 1); i++) {
		dma_phy += sizeof(struct dma_desc);
		p->desc3 = (unsigned int)dma_phy;
		/* Chain mode */
		p->desc1.all |= (1 << 24);
		p++;
	}
	p->desc1.all |= (1 << 24);
	p->desc3 = (unsigned int)addr;
}

int sunxi_mdio_read(void *iobase, int phyaddr, int phyreg)
{
	unsigned int value = 0;

	/* Mask the MDC_DIV_RATIO */
	value |= ((hwdev.mdc_div & 0x07) << 20);
	value |= (((phyaddr << 12) & (0x0001F000)) |
			((phyreg << 4) & (0x000007F0)) |
			MII_BUSY);

	while (((readl(iobase + GETH_MDIO_ADDR)) & MII_BUSY) == 1)
		;

	writel(value, iobase + GETH_MDIO_ADDR);
	while (((readl(iobase + GETH_MDIO_ADDR)) & MII_BUSY) == 1)
		;

	return (int)readl(iobase + GETH_MDIO_DATA);
}

int sunxi_mdio_write(void *iobase, int phyaddr, int phyreg, unsigned short data)
{
	unsigned int value;

	value = ((0x07 << 20) & readl(iobase + GETH_MDIO_ADDR)) |
		 (hwdev.mdc_div << 20);
	value |= (((phyaddr << 12) & (0x0001F000)) |
		  ((phyreg << 4) & (0x000007F0))) |
		  MII_WRITE | MII_BUSY;

	/* Wait until any existing MII operation is complete */
	while (((readl(iobase + GETH_MDIO_ADDR)) & MII_BUSY) == 1)
		;

	/* Set the MII address register to write */
	writel(data, iobase + GETH_MDIO_DATA);
	writel(value, iobase + GETH_MDIO_ADDR);

	/* Wait until any existing MII operation is complete */
	while (((readl(iobase + GETH_MDIO_ADDR)) & MII_BUSY) == 1)
		;

	return 0;
}

int sunxi_mdio_reset(void *iobase)
{
	writel((4 << 2), iobase + GETH_MDIO_ADDR);
	return 0;
}

void sunxi_set_link_mode(void *iobase, int duplex, int speed)
{
	unsigned int ctrl = readl(iobase + GETH_BASIC_CTL0);

	if (!duplex)
		ctrl &= ~CTL0_DM;
	else
		ctrl |= CTL0_DM;

	switch (speed) {
	case 1000:
		ctrl &= ~0x0C;
		break;
	case 100:
	case 10:
	default:
		ctrl |= 0x08;
		if (speed == 100)
			ctrl |= 0x04;
		else
			ctrl &= ~0x04;
		break;
	}

	writel(ctrl, iobase + GETH_BASIC_CTL0);
}

void sunxi_mac_loopback(void *iobase, int enable)
{
	int reg;

	reg = readl(iobase + GETH_BASIC_CTL0);
	if (enable)
		reg |= 0x02;
	else
		reg &= ~0x02;
	writel(reg, iobase + GETH_BASIC_CTL0);
}

void sunxi_flow_ctrl(void *iobase, int duplex, int fc, int pause)
{
	unsigned int flow = 0;

	if (fc & FLOW_RX) {
		flow = readl(iobase + GETH_RX_CTL0);
		flow |= 0x10000;
		writel(flow, iobase + GETH_RX_CTL0);
	}

	if (fc & FLOW_TX) {
		flow = readl(iobase + GETH_TX_FLOW_CTL);
		flow |= 0x00001;
		writel(flow, iobase + GETH_TX_FLOW_CTL);
	}

	if (duplex) {
		flow = readl(iobase + GETH_TX_FLOW_CTL);
		flow |= (pause << 4);
		writel(flow, iobase + GETH_TX_FLOW_CTL);
	}
}

int sunxi_int_status(void *iobase, struct geth_extra_stats *x)
{
	int ret = 0;
	/* read the status register (CSR5) */
	unsigned int intr_status;

	intr_status = readl(iobase + GETH_RGMII_STA);
	if (intr_status & RGMII_IRQ)
		readl(iobase + GETH_RGMII_STA);

	intr_status = readl(iobase + GETH_INT_STA);

	/* ABNORMAL interrupts */
	if (intr_status & TX_UNF_INT) {
		ret = tx_hard_error_bump_tc;
		x->tx_undeflow_irq++;
	}
	if (intr_status & TX_TOUT_INT)
		x->tx_jabber_irq++;

	if (intr_status & RX_OVF_INT)
		x->rx_overflow_irq++;

	if (intr_status & RX_UA_INT)
		x->rx_buf_unav_irq++;

	if (intr_status & RX_STOP_INT)
		x->rx_process_stopped_irq++;

	if (intr_status & RX_TOUT_INT)
		x->rx_watchdog_irq++;

	if (intr_status & TX_EARLY_INT)
		x->tx_early_irq++;

	if (intr_status & TX_STOP_INT) {
		x->tx_process_stopped_irq++;
		ret = tx_hard_error;
	}

	/* TX/RX NORMAL interrupts */
	if (intr_status & (TX_INT | RX_INT | RX_EARLY_INT | TX_UA_INT)) {
		x->normal_irq_n++;
		if (intr_status & (TX_INT | RX_INT))
			ret = handle_tx_rx;
	}
	/* Clear the interrupt by writing a logic 1 to the CSR5[15-0] */
	writel(intr_status & 0x3FFF, iobase + GETH_INT_STA);

	return ret;
}

void sunxi_start_rx(void *iobase, unsigned long rxbase)
{
	unsigned int value;

	/* Write the base address of Rx descriptor lists into registers */
	writel(rxbase, iobase + GETH_RX_DESC_LIST);

	value = readl(iobase + GETH_RX_CTL1);
	value |= 0x40000000;
	writel(value, iobase + GETH_RX_CTL1);
}

void sunxi_stop_rx(void *iobase)
{
	unsigned int value;

	value = readl(iobase + GETH_RX_CTL1);
	value &= ~0x40000000;
	writel(value, iobase + GETH_RX_CTL1);
}

void sunxi_start_tx(void *iobase, unsigned long txbase)
{
	unsigned int value;

	/* Write the base address of Tx descriptor lists into registers */
	writel(txbase, iobase + GETH_TX_DESC_LIST);

	value = readl(iobase + GETH_TX_CTL1);
	value |= 0x40000000;
	writel(value, iobase + GETH_TX_CTL1);
}

void sunxi_stop_tx(void *iobase)
{
	unsigned int value = readl(iobase + GETH_TX_CTL1);

	value &= ~0x40000000;
	writel(value, iobase + GETH_TX_CTL1);
}

static int sunxi_dma_init(void *iobase)
{
	unsigned int value;

	/* Burst should be 8 */
	value = (8 << 24);

#ifdef CONFIG_GMAC_DA
	value |= RX_TX_PRI;	/* Rx has priority over tx */
#endif
	writel(value, iobase + GETH_BASIC_CTL1);

	/* Mask interrupts by writing to CSR7 */
	writel(RX_INT | TX_UNF_INT, iobase + GETH_INT_EN);

	return 0;
}

int sunxi_mac_init(void *iobase, int txmode, int rxmode)
{
	unsigned int value;

	sunxi_dma_init(iobase);

	/* Initialize the core component */
	value = readl(iobase + GETH_TX_CTL0);
	value |= (1 << 30);	/* Jabber Disable */
	writel(value, iobase + GETH_TX_CTL0);

	value = readl(iobase + GETH_RX_CTL0);
	value |= (1 << 27);	/* Enable CRC & IPv4 Header Checksum */
	value |= (1 << 28);	/* Automatic Pad/CRC Stripping */
	value |= (1 << 29);	/* Jumbo Frame Enable */
	writel(value, iobase + GETH_RX_CTL0);

	writel((hwdev.mdc_div << 20), iobase + GETH_MDIO_ADDR); /* MDC_DIV_RATIO */

	/* Set the Rx&Tx mode */
	value = readl(iobase + GETH_TX_CTL1);
	if (txmode == SF_DMA_MODE) {
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		value |= TX_MD;
		/* Operating on second frame increase the performance
		 * especially when transmit store-and-forward is used.
		 */
		value |= TX_NEXT_FRM;
	} else {
		value &= ~TX_MD;
		value &= ~TX_TH;
		/* Set the transmit threshold */
		if (txmode <= 64)
			value |= 0x00000000;
		else if (txmode <= 128)
			value |= 0x00000100;
		else if (txmode <= 192)
			value |= 0x00000200;
		else
			value |= 0x00000300;
	}
	writel(value, iobase + GETH_TX_CTL1);

	value = readl(iobase + GETH_RX_CTL1);
	if (rxmode == SF_DMA_MODE) {
		value |= RX_MD;
	} else {
		value &= ~RX_MD;
		value &= ~RX_TH;
		if (rxmode <= 32)
			value |= 0x10;
		else if (rxmode <= 64)
			value |= 0x00;
		else if (rxmode <= 96)
			value |= 0x20;
		else
			value |= 0x30;
	}

	/* Forward frames with error and undersized good frame. */
	value |= (RX_ERR_FRM | RX_RUNT_FRM);

	writel(value, iobase + GETH_RX_CTL1);

	return 0;
}

void sunxi_hash_filter(void *iobase, unsigned long low, unsigned long high)
{
	writel(high, iobase + GETH_RX_HASH0);
	writel(low, iobase + GETH_RX_HASH1);
}

void sunxi_set_filter(void *iobase, unsigned long flags)
{
	int tmp_flags = 0;

	tmp_flags |= ((flags >> 31) |
			((flags >> 9) & 0x00000002) |
			((flags << 1) & 0x00000010) |
			((flags >> 3) & 0x00000060) |
			((flags << 7) & 0x00000300) |
			((flags << 6) & 0x00003000) |
			((flags << 12) & 0x00030000) |
			(flags << 31));

	writel(tmp_flags, iobase + GETH_RX_FRM_FLT);
}

void sunxi_set_umac(void *iobase, unsigned char *addr, int index)
{
	unsigned long data;

	data = (addr[5] << 8) | addr[4];
	writel(data, iobase + GETH_ADDR_HI(index));
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, iobase + GETH_ADDR_LO(index));
}

void sunxi_mac_enable(void *iobase)
{
	unsigned long value;

	value = readl(iobase + GETH_TX_CTL0);
	value |= (1 << 31);
	writel(value, iobase + GETH_TX_CTL0);

	value = readl(iobase + GETH_RX_CTL0);
	value |= (1 << 31);
	writel(value, iobase + GETH_RX_CTL0);
}

void sunxi_mac_disable(void *iobase)
{
	unsigned long value;

	value = readl(iobase + GETH_TX_CTL0);
	value &= ~(1 << 31);
	writel(value, iobase + GETH_TX_CTL0);

	value = readl(iobase + GETH_RX_CTL0);
	value &= ~(1 << 31);
	writel(value, iobase + GETH_RX_CTL0);
}

void sunxi_tx_poll(void *iobase)
{
	unsigned int value;

	value = readl(iobase + GETH_TX_CTL1);
	writel(value | 0x80000000, iobase + GETH_TX_CTL1);
}

void sunxi_rx_poll(void *iobase)
{
	unsigned int value;

	value = readl(iobase + GETH_RX_CTL1);
	writel(value | 0x80000000, iobase + GETH_RX_CTL1);
}

void sunxi_int_enable(void *iobase)
{
	writel(RX_INT | TX_UNF_INT, iobase + GETH_INT_EN);
}

void sunxi_int_disable(void *iobase)
{
	writel(0, iobase + GETH_INT_EN);
}

void desc_buf_set(struct dma_desc *desc, unsigned long paddr, int size)
{
	desc->desc1.all &= (~((1 << 11) - 1));
	desc->desc1.all |= (size & ((1 << 11) - 1));
	desc->desc2 = paddr;
}

void desc_set_own(struct dma_desc *desc)
{
	desc->desc0.all |= 0x80000000;
}

void desc_tx_close(struct dma_desc *first, struct dma_desc *end, int csum_insert)
{
	struct dma_desc *desc = first;

	first->desc1.tx.first_sg = 1;
	end->desc1.tx.last_seg = 1;
	end->desc1.tx.interrupt = 1;

	if (csum_insert)
		do {
			desc->desc1.tx.cic = 3;
			desc++;
		} while (desc <= end);
}

void desc_init(struct dma_desc *desc)
{
	desc->desc1.all = 0;
	desc->desc2  = 0;

	desc->desc1.all |= (1 << 24);
}

int desc_get_tx_status(struct dma_desc *desc, struct geth_extra_stats *x)
{
	int ret = 0;

	if (desc->desc0.tx.under_err) {
		x->tx_underflow++;
		ret = -1;
	}
	if (desc->desc0.tx.no_carr) {
		x->tx_carrier++;
		ret = -1;
	}
	if (desc->desc0.tx.loss_carr) {
		x->tx_losscarrier++;
		ret = -1;
	}

#if 0
	if ((desc->desc0.tx.ex_deferral) ||
			(desc->desc0.tx.ex_coll) ||
			(desc->desc0.tx.late_coll))
		stats->collisions += desc->desc0.tx.coll_cnt;
#endif

	if (desc->desc0.tx.deferred)
		x->tx_deferred++;

	return ret;
}

int desc_buf_get_len(struct dma_desc *desc)
{
	return (desc->desc1.all & ((1 << 11) - 1));
}

int desc_buf_get_addr(struct dma_desc *desc)
{
	return desc->desc2;
}

int desc_rx_frame_len(struct dma_desc *desc)
{
	return desc->desc0.rx.frm_len;
}

int desc_get_rx_status(struct dma_desc *desc, struct geth_extra_stats *x)
{
	int ret = good_frame;

	if (desc->desc0.rx.last_desc == 0) {
		return discard_frame;
	}

	if (desc->desc0.rx.err_sum) {
		if (desc->desc0.rx.desc_err)
			x->rx_desc++;

		if (desc->desc0.rx.sou_filter)
			x->sa_filter_fail++;

		if (desc->desc0.rx.over_err)
			x->overflow_error++;

		if (desc->desc0.rx.ipch_err)
			x->ipc_csum_error++;

		if (desc->desc0.rx.late_coll)
			x->rx_collision++;

		if (desc->desc0.rx.crc_err)
			x->rx_crc++;

		ret = discard_frame;
	}

	if (desc->desc0.rx.len_err) {
		ret = discard_frame;
	}
	if (desc->desc0.rx.mii_err) {
		ret = discard_frame;
	}

	return ret;
}

int desc_get_own(struct dma_desc *desc)
{
	return desc->desc0.all & 0x80000000;
}

int desc_get_tx_ls(struct dma_desc *desc)
{
	return desc->desc1.tx.last_seg;
}

int sunxi_geth_register(void *iobase, int version, unsigned int div)
{
	hwdev.ver = version;
	hwdev.iobase = iobase;
	hwdev.mdc_div = div;

	return 0;
}

int sunxi_mac_reset(void *iobase, void (*delay)(int), int n)
{
	unsigned int value;

	/* DMA SW reset */
	value = readl(iobase + GETH_BASIC_CTL1);
	value |= SOFT_RST;
	writel(value, iobase + GETH_BASIC_CTL1);

	delay(n);

	return !!(readl(iobase + GETH_BASIC_CTL1) & SOFT_RST);
}

/**
 * sunxi_parse_read_str - parse the input string for write attri.
 * @str: string to be parsed, eg: "0x00 0x01".
 * @addr: store the phy addr. eg: 0x00.
 * @reg: store the reg addr. eg: 0x01.
 *
 * return 0 if success, otherwise failed.
 */
int sunxi_parse_read_str(char *str, u16 *addr, u16 *reg)
{
	char *ptr = str;
	char *tstr = NULL;
	int ret = 0;

	/**
	 * Skip the leading whitespace, find the true split symbol.
	 * And it must be 'address value'.
	 */
	tstr = strim(str);
	ptr = strchr(tstr, ' ');
	if (!ptr)
		return -EINVAL;

	/**
	 * Replaced split symbol with a %NUL-terminator temporary.
	 * Will be fixed at end.
	 */
	*ptr = '\0';
	ret = kstrtos16(tstr, 16, addr);
	if (ret)
		goto out;

	ret = kstrtos16(skip_spaces(ptr + 1), 16, reg);

out:
	return ret;
}

/**
 * sunxi_parse_write_str - parse the input string for compare attri.
 * @str: string to be parsed, eg: "0x00 0x11 0x11".
 * @addr: store the phy addr. eg: 0x00.
 * @reg: store the reg addr. eg: 0x11.
 * @val: store the value. eg: 0x11.
 *
 * return 0 if success, otherwise failed.
 */
int sunxi_parse_write_str(char *str, u16 *addr,
			  u16 *reg, u16 *val)
{
	u16 result_addr[3] = { 0 };
	char *ptr = str;
	char *ptr2 = NULL;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(result_addr); i++) {
		ptr = skip_spaces(ptr);
		ptr2 = strchr(ptr, ' ');
		if (ptr2)
			*ptr2 = '\0';

		ret = kstrtou16(ptr, 16, &result_addr[i]);

		if (!ptr2 || ret)
			break;

		ptr = ptr2 + 1;
	}

	*addr = result_addr[0];
	*reg = result_addr[1];
	*val = result_addr[2];

	return ret;
}
