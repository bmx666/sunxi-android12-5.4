/*
 * Debug code for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*Linux version 3.4.0 compilation*/
/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))*/
#include<linux/module.h>
/*#endif*/
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/rtc.h>
#include <linux/time.h>

#include "xradio.h"
#include "hwio.h"
#include "bh.h"
#include "debug.h"
#ifdef SUPPORT_FW_DBG_INF
#include "fw_dbg_inf.h"
#endif

/*for host debuglevel*/
#define XRADIO_DBG_DEFAULT (XRADIO_DBG_ALWY|XRADIO_DBG_ERROR|XRADIO_DBG_WARN)

u8 dbg_common  = XRADIO_DBG_DEFAULT;
u8 dbg_sbus    = XRADIO_DBG_DEFAULT;
u8 dbg_bh      = XRADIO_DBG_DEFAULT;
u8 dbg_txrx    = XRADIO_DBG_DEFAULT;
u8 dbg_wsm     = XRADIO_DBG_DEFAULT;
u8 dbg_sta     = XRADIO_DBG_DEFAULT;
u8 dbg_scan    = XRADIO_DBG_DEFAULT;
u8 dbg_ap      = XRADIO_DBG_DEFAULT;
u8 dbg_pm      = XRADIO_DBG_DEFAULT;
u8 dbg_itp     = XRADIO_DBG_DEFAULT;
u8 dbg_etf     = XRADIO_DBG_DEFAULT;
u8 dbg_logfile = XRADIO_DBG_ERROR;

#ifdef ERROR_HANG_DRIVER
u8 error_hang_driver;
#endif


#if (DBG_XRADIO_HIF)
u16 hif_test_rw; /*0: nothing to do; 1: write only; 2: write and read*/
u16 hif_test_data_mode; /* hif test data mode, such as 0x55, 0xff etc*/
u16 hif_test_data_len; /* hif test data len, every data len pre round*/
u16 hif_test_data_round;
u16 hif_test_oper_delta; /* hif test operation delta time, give more time to analyze data tranx*/
#endif

#ifdef CONFIG_XRADIO_DEBUGFS
/* join_status */
static const char *const xradio_debug_join_status[] = {
	"passive",
	"monitor",
	"station",
	"access point",
};

/* WSM_JOIN_PREAMBLE_... */
static const char *const xradio_debug_preamble[] = {
	"long",
	"short",
	"long on 1 and 2 Mbps",
};

static const char *const xradio_debug_fw_types[] = {
	"ETF",
	"WFM",
	"WSM",
	"HI test",
	"Platform test",
};

static const char *const xradio_debug_link_id[] = {
	"OFF",
	"REQ",
	"SOFT",
	"HARD",
};

static const char *xradio_debug_mode(int mode)
{
	switch (mode) {
	case NL80211_IFTYPE_UNSPECIFIED:
		return "unspecified";
	case NL80211_IFTYPE_MONITOR:
		return "monitor";
	case NL80211_IFTYPE_STATION:
		return "station";
	case NL80211_IFTYPE_ADHOC:
		return "ad-hok";
	case NL80211_IFTYPE_MESH_POINT:
		return "mesh point";
	case NL80211_IFTYPE_AP:
		return "access point";
	case NL80211_IFTYPE_P2P_CLIENT:
		return "p2p client";
	case NL80211_IFTYPE_P2P_GO:
		return "p2p go";
	default:
		return "unsupported";
	}
}

static void xradio_queue_status_show(struct seq_file *seq,
				     struct xradio_queue *q)
{
	int i, if_id;
	seq_printf(seq, "Queue       %d:\n", q->queue_id);
	seq_printf(seq, "  capacity: %zu\n", q->capacity);
	seq_printf(seq, "  queued:   %zu\n", q->num_queued);
	seq_printf(seq, "  pending:  %zu\n", q->num_pending);
	seq_printf(seq, "  sent:     %zu\n", q->num_sent);
	seq_printf(seq, "  locked:   %s\n", q->tx_locked_cnt ? "yes" : "no");
	seq_printf(seq, "  overfull: %s\n", q->overfull ? "yes" : "no");
	seq_puts(seq, "  link map: 0-> ");
	for (if_id = 0; if_id < XRWL_MAX_VIFS; if_id++) {
		for (i = 0; i < q->stats->map_capacity; ++i)
			seq_printf(seq, "%.2d ", q->link_map_cache[if_id][i]);
		seq_printf(seq, "<-%zu\n", q->stats->map_capacity);
	}
}

static void xradio_debug_print_map(struct seq_file *seq,
				   struct xradio_vif *priv,
				   const char *label, u32 map)
{
	int i;
	seq_printf(seq, "%s0-> ", label);
	for (i = 0; i < priv->hw_priv->tx_queue_stats.map_capacity; ++i)
		seq_printf(seq, "%s ", (map & BIT(i)) ? "**" : "..");
	seq_printf(seq, "<-%zu\n",
		   priv->hw_priv->tx_queue_stats.map_capacity - 1);
}

static int xradio_version_show(struct seq_file *seq, void *v)
{
	struct xradio_common *hw_priv = seq->private;
	seq_printf(seq, "Driver Label:%s  %s\n", DRV_VERSION, DRV_BUILDTIME);
	seq_printf(seq, "Firmware Label:%s\n", &hw_priv->wsm_caps.fw_label[0]);
	return 0;
}

static int xradio_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_version_show, inode->i_private);
}

static const struct file_operations fops_version = {
	.open = xradio_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#if (DGB_XRADIO_QC)
static int xradio_hwinfo_show(struct seq_file *seq, void *v)
{
	struct xradio_common *hw_priv = seq->private;
	u32 hw_arry[8] = { 0 };

	wsm_read_mib(hw_priv, WSM_MIB_ID_HW_INFO, (void *)&hw_arry,
		     sizeof(hw_arry), 4);

	/*
	get_random_bytes((u8 *)&hw_arry[0], 8*sizeof(u32));
	hw_arry[0] = 0x0B140D4;
	hw_arry[1] &= ~0xF803FFFF;
	hw_arry[2] &= ~0xC0001FFF;
	hw_arry[5] &= ~0xFFFFF000;
	hw_arry[7] &= ~0xFFFC07C0;
	*/

	seq_printf(seq, "0x%08x, 0x%08x, 0x%08x, 0x%08x, "
			"0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			hw_arry[0], hw_arry[1], hw_arry[2], hw_arry[3],
			hw_arry[4], hw_arry[5], hw_arry[6], hw_arry[7]);
	return 0;
}

static int xradio_hwinfo_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_hwinfo_show, inode->i_private);
}

static const struct file_operations fops_hwinfo = {
	.open = xradio_hwinfo_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};


static int xradio_temperature_show(struct seq_file *seq, void *v)
{
	struct xradio_common *hw_priv = seq->private;
	s32 s32tempQ4;

	wsm_read_mib(hw_priv, WSM_MIB_ID_GET_TEMPERATURE, (void *)&s32tempQ4,
		     sizeof(s32tempQ4), 4);
	seq_printf(seq, "%d\n", (s32tempQ4 >> 4));
	return 0;
}

static int xradio_temperature_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_temperature_show, inode->i_private);
}

static const struct file_operations fops_temperature = {
	.open = xradio_temperature_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};
#endif

static int xradio_status_show_common(struct seq_file *seq, void *v)
{
	int i;
	struct list_head *item;
	struct xradio_common *hw_priv = seq->private;
	struct xradio_debug_common *d = hw_priv->debug;
	int ba_cnt, ba_acc, ba_cnt_rx, ba_acc_rx, ba_avg = 0, ba_avg_rx = 0;
	bool ba_ena;

	spin_lock_bh(&hw_priv->ba_lock);
	ba_cnt = hw_priv->debug->ba_cnt;
	ba_acc = hw_priv->debug->ba_acc;
	ba_cnt_rx = hw_priv->debug->ba_cnt_rx;
	ba_acc_rx = hw_priv->debug->ba_acc_rx;
	ba_ena = hw_priv->ba_ena;
	if (ba_cnt)
		ba_avg = ba_acc / ba_cnt;
	if (ba_cnt_rx)
		ba_avg_rx = ba_acc_rx / ba_cnt_rx;
	spin_unlock_bh(&hw_priv->ba_lock);

	seq_puts(seq,   "XRADIO Wireless LAN driver status\n");
	seq_printf(seq, "Hardware:   %d.%d\n",
		hw_priv->wsm_caps.hardwareId,
		hw_priv->wsm_caps.hardwareSubId);
	seq_printf(seq, "Firmware:   %s %d.%d\n",
		xradio_debug_fw_types[hw_priv->wsm_caps.firmwareType],
		hw_priv->wsm_caps.firmwareVersion,
		hw_priv->wsm_caps.firmwareBuildNumber);
	seq_printf(seq, "FW API:     %d\n",
		hw_priv->wsm_caps.firmwareApiVer);
	seq_printf(seq, "FW caps:    0x%.4X\n",
		hw_priv->wsm_caps.firmwareCap);
	if (hw_priv->channel) {
		seq_printf(seq, "Channel:    %d%s\n",
			hw_priv->channel->hw_value,
			hw_priv->channel_switch_in_progress ?
			" (switching)" : "");

		seq_printf(seq, "  flags:    0x%08x\n",
			hw_priv->channel->flags);
	}
	seq_printf(seq, "HT:         %s\n",
		xradio_is_ht(&hw_priv->ht_info) ? "on" : "off");
	if (xradio_is_ht(&hw_priv->ht_info)) {

#ifdef SUPPORT_HT40

		seq_printf(seq, "2040BSS:    %s\n",
			xradio_is_2040BSS(&hw_priv->ht_info) ? "yes" : "no");

		if (xradio_is_2040BSS(&hw_priv->ht_info)) {
			switch (hw_priv->ht_info.channel_type) {
			case NL80211_CHAN_HT40MINUS:
				seq_printf(seq, "Second Chan:Below\n");
				break;
			case NL80211_CHAN_HT40PLUS:
				seq_printf(seq, "Second Chan:Above\n");
				break;
			default:
				break;
			}
		}

		seq_printf(seq, "ShortGI:    %s\n",
			xradio_ht_ShortGI(&hw_priv->ht_info) ? "yes" : "no");

#endif

		seq_printf(seq, "Greenfield: %s\n",
			xradio_ht_greenfield(&hw_priv->ht_info) ? "yes" : "no");
		seq_printf(seq, "AMPDU dens: %d\n",
			xradio_ht_ampdu_density(&hw_priv->ht_info));
	}
	spin_lock_bh(&hw_priv->tx_policy_cache.lock);
	i = 0;
	list_for_each(item, &hw_priv->tx_policy_cache.used)
		++i;
	spin_unlock_bh(&hw_priv->tx_policy_cache.lock);
	seq_printf(seq, "RC in use:  %d\n", i);
	seq_printf(seq, "BA stat:    %d, %d (%d)\n",
		ba_cnt, ba_acc, ba_avg);
	seq_printf(seq, "BA RX stat:    %d, %d (%d)\n",
		ba_cnt_rx, ba_acc_rx, ba_avg_rx);
	seq_printf(seq, "Block ACK:  %s\n", ba_ena ? "on" : "off");

	seq_puts(seq, "\n");
	for (i = 0; i < 4; ++i) {
		xradio_queue_status_show(seq, &hw_priv->tx_queue[i]);
		seq_puts(seq, "\n");
	}
	seq_printf(seq, "TX burst:   %d\n",
		d->tx_burst);
	seq_printf(seq, "RX burst:   %d\n",
		d->rx_burst);
	seq_printf(seq, "TX miss:    %d\n",
		d->tx_cache_miss);
	seq_printf(seq, "Long retr:  %d\n",
		hw_priv->long_frame_max_tx_count);
	seq_printf(seq, "Short retr: %d\n",
		hw_priv->short_frame_max_tx_count);

	seq_printf(seq, "BH status: %s, errcode=%d\n",
		   atomic_read(&hw_priv->bh_term) ? "terminated" : "alive",
		   hw_priv->bh_error);
	seq_printf(seq, "Pending RX: %d\n",
		atomic_read(&hw_priv->bh_rx));
	seq_printf(seq, "Pending TX: %d\n",
		atomic_read(&hw_priv->bh_tx));

	seq_printf(seq, "TX bufs:    %d x %d bytes\n",
		hw_priv->wsm_caps.numInpChBufs,
		hw_priv->wsm_caps.sizeInpChBuf);
	seq_printf(seq, "Used bufs:  %d\n",
		hw_priv->hw_bufs_used);
#ifdef BH_PROC_THREAD
	seq_printf(seq, "tx_queued:  %d\n",
		atomic_read(&hw_priv->proc.tx_queued));
	seq_printf(seq, "rx_queued:  %d\n",
		atomic_read(&hw_priv->proc.rx_queued));
#endif
	seq_printf(seq, "Powersavemode:%s\n",
		hw_priv->powersave_enabled ? "enable" : "disable");
	seq_printf(seq, "Device:     %s\n",
		hw_priv->device_can_sleep ? "alseep" : "awake");

	spin_lock(&hw_priv->wsm_cmd.lock);
	seq_printf(seq, "WSM status: %s\n",
		hw_priv->wsm_cmd.done ? "idle" : "active");
	seq_printf(seq, "WSM cmd:    0x%.4X (%zu bytes)\n",
		hw_priv->wsm_cmd.cmd, hw_priv->wsm_cmd.len);
	seq_printf(seq, "WSM retval: %d\n",
		hw_priv->wsm_cmd.ret);
	spin_unlock(&hw_priv->wsm_cmd.lock);

	seq_printf(seq, "Datapath:   %s\n",
		atomic_read(&hw_priv->tx_lock) ? "locked" : "unlocked");
	if (atomic_read(&hw_priv->tx_lock))
		seq_printf(seq, "TXlock cnt: %d\n",
			atomic_read(&hw_priv->tx_lock));

	seq_printf(seq, "Scan:       %s\n",
		atomic_read(&hw_priv->scan.in_progress) ? "active" : "idle");
	seq_printf(seq, "Led state:  0x%.2X\n",
		hw_priv->softled_state);

	return 0;
}

static int xradio_status_open_common(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_status_show_common,
		inode->i_private);
}

static const struct file_operations fops_status_common = {
	.open = xradio_status_open_common,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int xradio_counters_show(struct seq_file *seq, void *v)
{
	int ret;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_counters_table counters;

	ret = wsm_get_counters_table(hw_priv, &counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y
#define PUT_COUNTER(tab, name) \
	seq_printf(seq, "%s:" tab "%d\n", #name, \
		__le32_to_cpu(counters.CAT_STR(count, name)))

	PUT_COUNTER("\t\t\t\t", PlcpErrors);
	PUT_COUNTER("\t\t\t\t", FcsErrors);
	PUT_COUNTER("\t\t\t\t", TxPackets);
	PUT_COUNTER("\t\t\t\t", RxPackets);
	PUT_COUNTER("\t\t\t",   RxPacketErrors);
	PUT_COUNTER("\t\t\t\t", RtsSuccess);
	PUT_COUNTER("\t\t\t", RtsFailures);
	PUT_COUNTER("\t\t",   RxFramesSuccess);
	PUT_COUNTER("\t",     RxDecryptionFailures);
	PUT_COUNTER("\t\t\t", RxMicFailures);
	PUT_COUNTER("\t\t",   RxNoKeyFailures);
	PUT_COUNTER("\t\t",   TxMulticastFrames);
	PUT_COUNTER("\t\t",   TxFramesSuccess);
	PUT_COUNTER("\t\t",   TxFrameFailures);
	PUT_COUNTER("\t\t",   TxFramesRetried);
	PUT_COUNTER("\t",     TxFramesMultiRetried);
	PUT_COUNTER("\t\t",   RxFrameDuplicates);
	PUT_COUNTER("\t\t\t", AckFailures);
	PUT_COUNTER("\t\t",   RxMulticastFrames);
	PUT_COUNTER("\t\t",   RxCMACICVErrors);
	PUT_COUNTER("\t\t\t", RxCMACReplays);
	PUT_COUNTER("\t\t",   RxMgmtCCMPReplays);
	PUT_COUNTER("\t\t\t", RxBIPMICErrors);

	PUT_COUNTER("\t\t\t", AllBeacons);
	PUT_COUNTER("\t\t\t", ScanBeacons);
	PUT_COUNTER("\t\t\t", ScanProbeRsps);
	PUT_COUNTER("\t\t\t", OutChanBeacons);
	PUT_COUNTER("\t\t",   OutChanProbeRsps);
	PUT_COUNTER("\t\t\t", BssBeacons);
	PUT_COUNTER("\t\t\t", HostBeacons);
	PUT_COUNTER("\t\t\t", MissBeacons);
	PUT_COUNTER("\t\t\t", DTIMBeacons);


#undef PUT_COUNTER
#undef CAT_STR

	return 0;
}

static int xradio_counters_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_counters_show,
		inode->i_private);
}

static const struct file_operations fops_counters = {
	.open = xradio_counters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int xradio_backoff_show(struct seq_file *seq, void *v)
{
	int ret;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_backoff_counter counters;

	ret = wsm_get_backoff_dbg(hw_priv, &counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y
#define PUT_COUNTER(tab, name) \
	seq_printf(seq, tab"%s:\t%d\n", #name, \
		(__le32_to_cpu(counters.CAT_STR(count, name))&0xffff))

	PUT_COUNTER("backoff_max ", 0);
	PUT_COUNTER("[0, 7]       ", 1);
	PUT_COUNTER("[~, 15]      ", 2);
	PUT_COUNTER("[~, 31]      ", 3);
	PUT_COUNTER("[~, 63]      ", 4);
	PUT_COUNTER("[~, 127]     ", 5);
	PUT_COUNTER("[~, 255]     ", 6);
	PUT_COUNTER("[~, 511]     ", 7);
	PUT_COUNTER("[~, 1023]    ", 8);


#undef PUT_COUNTER
#undef CAT_STR

	return 0;
}

static int xradio_backoff_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_backoff_show,
		inode->i_private);
}

static const struct file_operations fops_backoff = {
	.open = xradio_backoff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#ifdef SUPPORT_HT40

static int xradio_ratemap_show(struct seq_file *seq, void *v)
{
	/* int ret; */
	/* struct xradio_common *hw_priv = seq->private; */

#define PUT_TXRATE_COUNT(idx, bdsss, aofdm) do { \
	seq_printf(seq, "MCS%d:"  "[%8d][%8d] [%8d][%8d] %4s:" \
		"[%8d] %3s:[%8d]\n", idx, \
		__le32_to_cpu(TxedHtofdmRateMap[0][idx]), \
		__le32_to_cpu(TxedHtofdmRateMap[1][idx]), \
		__le32_to_cpu(TxedHtofdmRateMap[2][idx]), \
		__le32_to_cpu(TxedHtofdmRateMap[3][idx]), \
		#bdsss, __le32_to_cpu(TxedLegacyRateMap[0][idx]), \
		#aofdm, __le32_to_cpu(TxedLegacyRateMap[1][idx]));\
		TxedHtofdmRateMap[0][idx] = 0;\
		TxedHtofdmRateMap[1][idx] = 0;\
		TxedHtofdmRateMap[2][idx] = 0;\
		TxedHtofdmRateMap[3][idx] = 0;\
		TxedLegacyRateMap[0][idx] = 0;\
		TxedLegacyRateMap[1][idx] = 0;\
} while (0)

	seq_printf(seq, "\nTxRateMap:\n");
	seq_printf(seq, "     " "[20SGI TX][20LGI TX] " \
				"[40SGI TX][40LGI TX] " \
				"     [DSSS  TX] " \
				"    [OFDM  TX]\n");

	PUT_TXRATE_COUNT(7, 11MS, 54M);
	PUT_TXRATE_COUNT(6, 11ML, 48M);
	PUT_TXRATE_COUNT(5, 5M5S, 36M);
	PUT_TXRATE_COUNT(4, 5M5L, 24M);
	PUT_TXRATE_COUNT(3,  2MS, 18M);
	PUT_TXRATE_COUNT(2,  2ML, 12M);
	PUT_TXRATE_COUNT(1, ----,  9M);
	PUT_TXRATE_COUNT(0,  1ML,  6M);

#undef PUT_TXRATE_COUNT

#define PUT_RXRATE_COUNT(idx, bdsss, aofdm) do { \
	seq_printf(seq, "MCS%d:"  "[%8d][%8d] [%8d][%8d] %4s:" \
		"[%8d] %3s:[%8d]\n", idx, \
		__le32_to_cpu(RxedHtofdmRateMap[0][idx]), \
		__le32_to_cpu(RxedHtofdmRateMap[1][idx]), \
		__le32_to_cpu(RxedHtofdmRateMap[2][idx]), \
		__le32_to_cpu(RxedHtofdmRateMap[3][idx]), \
		#bdsss, __le32_to_cpu(RxedLegacyRateMap[0][idx]), \
		#aofdm, __le32_to_cpu(RxedLegacyRateMap[1][idx]));\
		RxedHtofdmRateMap[0][idx] = 0;\
		RxedHtofdmRateMap[1][idx] = 0;\
		RxedHtofdmRateMap[2][idx] = 0;\
		RxedHtofdmRateMap[3][idx] = 0;\
		RxedLegacyRateMap[0][idx] = 0;\
		RxedLegacyRateMap[1][idx] = 0;\
} while (0)

	seq_printf(seq, "\nRxRateMap:\n");
	seq_printf(seq, "     " "[20SGI RX][20LGI RX] " \
				"[40SGI RX][40LGI RX] " \
				"     [DSSS  RX] " \
				"    [OFDM  RX]\n");

	PUT_RXRATE_COUNT(7, 11MS, 54M);
	PUT_RXRATE_COUNT(6, 11ML, 48M);
	PUT_RXRATE_COUNT(5, 5M5S, 36M);
	PUT_RXRATE_COUNT(4, 5M5L, 24M);
	PUT_RXRATE_COUNT(3,  2MS, 18M);
	PUT_RXRATE_COUNT(2,  2ML, 12M);
	PUT_RXRATE_COUNT(1, ----,  9M);
	PUT_RXRATE_COUNT(0,  1ML,  6M);

#undef PUT_RXRATE_COUNT

	return 0;
}

#else

static int xradio_ratemap_show(struct seq_file *seq, void *v)
{
	/* int ret; */
	/* struct xradio_common *hw_priv = seq->private; */

	seq_printf(seq, "\nRateMap for Tx & RX:\n");
#define PUT_RATE_COUNT(name, idx) do { \
	seq_printf(seq, "%s\t"  "%d,  %d\n", #name, \
		__le32_to_cpu(TxedRateIdx_Map[idx]), \
		__le32_to_cpu(RxedRateIdx_Map[idx])); \
	TxedRateIdx_Map[idx] = 0; \
	RxedRateIdx_Map[idx] = 0; \
} while (0)

	PUT_RATE_COUNT("65   Mbps:", 21);
	PUT_RATE_COUNT("58.5 Mbps:", 20);
	PUT_RATE_COUNT("52   Mbps:", 19);
	PUT_RATE_COUNT("39   Mbps:", 18);
	PUT_RATE_COUNT("26   Mbps:", 17);
	PUT_RATE_COUNT("19.5 Mbps:", 16);
	PUT_RATE_COUNT("13   Mbps:", 15);
	PUT_RATE_COUNT("6.5  Mbps:", 14);

	PUT_RATE_COUNT("54   Mbps:", 13);
	PUT_RATE_COUNT("48   Mbps:", 12);
	PUT_RATE_COUNT("36   Mbps:", 11);
	PUT_RATE_COUNT("24   Mbps:", 10);
	PUT_RATE_COUNT("18   Mbps:",  9);
	PUT_RATE_COUNT("12   Mbps:",  8);
	PUT_RATE_COUNT("9    Mbps:", 7);
	PUT_RATE_COUNT("6    Mbps:",  6);

	PUT_RATE_COUNT("11   Mbps:", 3);
	PUT_RATE_COUNT("5.5  Mbps:", 2);
	PUT_RATE_COUNT("2    Mbps:", 1);
	PUT_RATE_COUNT("1    Mbps:", 0);

#undef PUT_RATE_COUNT

	return 0;
}

#endif

static int xradio_ratemap_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_ratemap_show,
		inode->i_private);
}

static const struct file_operations fops_ratemap = {
	.open = xradio_ratemap_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int xradio_ampducounters_show(struct seq_file *seq, void *v)
{
	int ret;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_ampducounters_table counters;

	ret = wsm_get_ampducounters_table(hw_priv, &counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y
#define PUT_COUNTER(tab, name) \
	seq_printf(seq, "%s:" tab "%d\n", #name, \
		__le32_to_cpu(counters.CAT_STR(count, name)))

	PUT_COUNTER("\t\t\t",	TxAMPDUs);
	PUT_COUNTER("\t\t",	TxMPDUsInAMPDUs);
	PUT_COUNTER("\t\t",	TxOctetsInAMPDUs_l32);
	PUT_COUNTER("\t\t",	TxOctetsInAMPDUs_h32);
	PUT_COUNTER("\t\t\t",	RxAMPDUs);
	PUT_COUNTER("\t\t",	RxMPDUsInAMPDUs);
	PUT_COUNTER("\t\t",	RxOctetsInAMPDUs_l32);
	PUT_COUNTER("\t\t",	RxOctetsInAMPDUs_h32);
	PUT_COUNTER("\t",	RxDelimeterCRCErrorCount);
	PUT_COUNTER("\t\t",	ImplictBARFailures);
	PUT_COUNTER("\t\t",	ExplictBARFailures);



#undef PUT_COUNTER
#undef CAT_STR

	return 0;
}

static int xradio_ampducounters_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_ampducounters_show,
		inode->i_private);
}

static const struct file_operations fops_ampducounters = {
	.open = xradio_ampducounters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int xradio_txpipe_show(struct seq_file *seq, void *v)
{
	int ret;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_txpipe_counter counters;

	ret = wsm_get_txpipe_table(hw_priv, &counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y
#define PUT_COUNTER(tab, name) \
	seq_printf(seq, tab":\t%d\n", \
		__le32_to_cpu(counters.CAT_STR(count, name)))

	PUT_COUNTER("tx-aggr       ", 1);
	PUT_COUNTER("retx-aggr     ", 2);
	PUT_COUNTER("retry_type1   ", 3);
	PUT_COUNTER("retry_type2   ", 4);
	PUT_COUNTER("retry_type3   ", 5);
	PUT_COUNTER("rx-aggr-event ", 6);
	PUT_COUNTER("rx-aggr-end   ", 7);
	PUT_COUNTER("rx-ba         ", 8);
	PUT_COUNTER("tx_ampdu_len  ", 9);
	PUT_COUNTER("fail_by_rts   ", a);


#undef PUT_COUNTER
#undef CAT_STR

	return 0;
}

static int xradio_txpipe_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_txpipe_show,
		inode->i_private);
}

static const struct file_operations fops_txpipe = {
	.open = xradio_txpipe_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#if (SUPPORT_EPTA)

u32 epta_stat_dbg_ctrl = 0x484;
char *med_state_str[4] = {
	"bt inactive",
	"with bt and wlan not waiting",
	"with wlan",
	"with bt and wlan waiting"
};
char *req_type_str[11] = {
	"rx_recovery", "rx_mcast", "rx_bcn", "tx_bcn", "tx_cts", "tx_high", "tx_uapsd", "fastps",
	"scan", "pspoll", "tx_low"
};

static int xradio_epta_stat_show(struct seq_file *seq, void *v)
{
	int ret, i;
	struct xradio_epta_stat epta_stat;
	struct xradio_common *hw_priv = seq->private;

//	memset(&epta_stat, 0, sizeof(epta_stat));
	ret = wsm_get_epta_statistics(hw_priv, &epta_stat);
#if 0
	seq_printf(seq, "\n================\n");
	for (i = 0; i < sizeof(epta_stat); i++) {
		if (i % 4 == 0)
			seq_printf(seq, "%d:\t ", i);
		seq_printf(seq, "%2x ", *((u8 *)&epta_stat+i));
		if ((i+1) % 4 == 0)
			seq_printf(seq, "\n");
	}
	seq_printf(seq, "\n================\n");
#endif
	if (ret)
		return ret;

	seq_printf(seq, "\n[epta statistics]\n");
	seq_printf(seq, "medium state: %s\n", med_state_str[epta_stat.medium_state]);
	if (epta_stat.medium_state == 0)
		return 0;
	if (epta_stat.wlan_req_ongoing) {
		//req got into epta from lmc_epta but not granted or aborted yet
	}
	if (epta_stat.medium_state == 2) {
		seq_printf(seq, "current req :[type:%s][prio_sw:%u][prio_hw:%u][req dur:%u]\n", req_type_str[epta_stat.wlan_req_type],
				epta_stat.wlan_req_priority & 0xf, (epta_stat.wlan_req_priority >> 4) & 0xf,  __le32_to_cpu(epta_stat.wlan_req_request_dur));
	//	seq_printf(seq, "current req granted time: \t %u\n", __le32_to_cpu(epta_stat.grant_or_wait_time));
	} else if (epta_stat.medium_state == 3) {
		seq_printf(seq, "current req :[type:%s][prio_sw:%u][prio_hw:%u][req dur:%u]\n", req_type_str[epta_stat.wlan_req_type],
				epta_stat.wlan_req_priority & 0xf, (epta_stat.wlan_req_priority >> 4) & 0xf, __le32_to_cpu(epta_stat.wlan_req_request_dur));
	//	seq_printf(seq, "wlan is waiting for bt: \t\t %u\n", __le32_to_cpu(epta_stat.grant_or_wait_time));
	} else {
		seq_printf(seq, "current req :\n");
	}

	seq_printf(seq, "=========================wlan req info========================\n");

	seq_printf(seq, "                   %10s ", "all");
	for (i = 0; i < 11; i++)
		if (epta_stat_dbg_ctrl & (1<<i))
			seq_printf(seq, "%10s ", req_type_str[i]);
	seq_printf(seq, "\n");
	seq_printf(seq, "req win cnt:       %10u %10u %10u %10u\n", __le32_to_cpu(epta_stat.wl_req_info[0].cnt_win), __le32_to_cpu(epta_stat.wl_req_info[1].cnt_win),
			__le32_to_cpu(epta_stat.wl_req_info[2].cnt_win), __le32_to_cpu(epta_stat.wl_req_info[3].cnt_win));
	seq_printf(seq, "req lose cnt:      %10u %10u %10u %10u\n", __le32_to_cpu(epta_stat.wl_req_info[0].cnt_lose), __le32_to_cpu(epta_stat.wl_req_info[1].cnt_lose),
			__le32_to_cpu(epta_stat.wl_req_info[2].cnt_lose), __le32_to_cpu(epta_stat.wl_req_info[3].cnt_lose));

	seq_printf(seq, "grant med ret cnt: %10u %10u %10u %10u\n", __le16_to_cpu(epta_stat.wl_req_info[0].cnt_grant_med_ret), __le16_to_cpu(epta_stat.wl_req_info[1].cnt_grant_med_ret),
			__le16_to_cpu(epta_stat.wl_req_info[2].cnt_grant_med_ret), __le16_to_cpu(epta_stat.wl_req_info[3].cnt_grant_med_ret));
	seq_printf(seq, "grant timeout cnt: %10u %10u %10u %10u\n", __le16_to_cpu(epta_stat.wl_req_info[0].cnt_grant_timeout), __le16_to_cpu(epta_stat.wl_req_info[1].cnt_grant_timeout),
			__le16_to_cpu(epta_stat.wl_req_info[2].cnt_grant_timeout), __le16_to_cpu(epta_stat.wl_req_info[3].cnt_grant_timeout));
	seq_printf(seq, "abort misss rx cnt:%10u %10u %10u %10u\n", __le16_to_cpu(epta_stat.wl_req_info[0].cnt_abort_miss_rx), __le16_to_cpu(epta_stat.wl_req_info[1].cnt_abort_miss_rx),
			__le16_to_cpu(epta_stat.wl_req_info[2].cnt_abort_miss_rx), __le16_to_cpu(epta_stat.wl_req_info[3].cnt_abort_miss_rx));
	seq_printf(seq, "abort expired cnt: %10u %10u %10u %10u\n", __le16_to_cpu(epta_stat.wl_req_info[0].cnt_abort_expired), __le16_to_cpu(epta_stat.wl_req_info[1].cnt_abort_expired),
			__le16_to_cpu(epta_stat.wl_req_info[2].cnt_abort_expired), __le16_to_cpu(epta_stat.wl_req_info[3].cnt_abort_expired));

	seq_printf(seq, "granted time:      %10u %10u %10u %10u\n", __le32_to_cpu(epta_stat.wl_req_info[0].granted_time), __le32_to_cpu(epta_stat.wl_req_info[1].granted_time),
			__le32_to_cpu(epta_stat.wl_req_info[2].granted_time), __le32_to_cpu(epta_stat.wl_req_info[3].granted_time));
	seq_printf(seq, "granted used time: %10u %10u %10u %10u\n", __le32_to_cpu(epta_stat.wl_req_info[0].gr_used_time), __le32_to_cpu(epta_stat.wl_req_info[1].gr_used_time),
			__le32_to_cpu(epta_stat.wl_req_info[2].gr_used_time), __le32_to_cpu(epta_stat.wl_req_info[3].gr_used_time));

	seq_printf(seq, "\n");
	seq_printf(seq, "%25s %17s %18s\n", "[req ext cnt] grant_uapsd", "abort_rx_bcn", "abort_pspoll");
	seq_printf(seq, "%25u %17u %18u\n", __le16_to_cpu(epta_stat.cnt_ex_grant_uapsd),
			__le16_to_cpu(epta_stat.cnt_ex_abort_rx_bcn), __le16_to_cpu(epta_stat.cnt_ex_abort_pspoll));

	seq_printf(seq, "%25s %17s %18s\n", "[bt abort cnt]      total", "abort_tx", "abort_tx_fix");
	seq_printf(seq, "%25u %17u %18u\n", __le16_to_cpu(epta_stat.cnt_bt_abort),
			__le16_to_cpu(epta_stat.cnt_bt_abort_tx), __le16_to_cpu(epta_stat.cnt_bt_abort_tx_fix));

	seq_printf(seq, "========================bt tx req type========================\n");
	seq_printf(seq, "%4s %13s %13s %14s %14s\n", "poll", "dm1", "br", "edr", "sco");
	seq_printf(seq, "%4u %13u %13u %14u %14u\n", __le16_to_cpu(epta_stat.cnt_bt_tx_poll_pkt_req),
			__le16_to_cpu(epta_stat.cnt_bt_tx_dm1_pkt_req), __le16_to_cpu(epta_stat.cnt_bt_tx_br_pkt_req),
			__le16_to_cpu(epta_stat.cnt_bt_tx_edr_pkt_req), __le16_to_cpu(epta_stat.cnt_bt_tx_sco_pkt_req));

	seq_printf(seq, "=======================wlan/bt nrt info=======================\n");
	seq_printf(seq, "%13s %14s %7s %10s %14s\n", "wl_quota_used", " bt_quota_used", "bt_req", "gr_bt_req", "gr_bt_max_gap");
	seq_printf(seq, "%13u %14u %7u %10u %14u\n", __le32_to_cpu(epta_stat.nrt_wlan_quota_used), __le32_to_cpu(epta_stat.nrt_bt_quota_used),
			__le32_to_cpu(epta_stat.nrt_bt_requests), __le32_to_cpu(epta_stat.nrt_bt_granted_requestd), __le32_to_cpu(epta_stat.nrt_bt_granted_req_max_gap));
	seq_printf(seq, "\n");

	for (i = 0; i < epta_stat.num_active_rt_bt_links; i++) {
		struct xradio_epta_bt_link_info *bt_link_info = &epta_stat.bt_link_info[i];
		seq_printf(seq, "=======================rt bt link idx %u=======================\n", i);
		seq_printf(seq, "%6s: %-3u %12s: %-2u %13s: %-3u %9s: %-1u\n", "linkid", bt_link_info->link_id, "type  ", bt_link_info->traffic_type,
				"master", bt_link_info->master_or_slave, "prio_sw    ", bt_link_info->traffic_priority);
		seq_printf(seq, "%6s: %7u %8s: %7u %8s: %-3u %9s: %-1u\n", "rt_si ", __le32_to_cpu(bt_link_info->rt_si), "rt_sw ", __le32_to_cpu(bt_link_info->rt_sw),
				"req   ", __le32_to_cpu(bt_link_info->requests), "prio_hw_rt ", ((__le32_to_cpu(bt_link_info->time_diff_req_start)) >> 4) & 0xf);
		seq_printf(seq, "%6s: %7u %8s: %7u %8s: %-3u %9s: %-1u\n", "msg_si", __le32_to_cpu(bt_link_info->msg_si), "msg_sw", __le32_to_cpu(bt_link_info->msg_sw),
				"gr_req", __le32_to_cpu(bt_link_info->granted_requests), "prio_hw_nrt",  (__le32_to_cpu(bt_link_info->time_diff_req_start)) & 0xf);

		seq_printf(seq, "%6s: %-3u %14s: %-3u\n", "caton ",  __le32_to_cpu(bt_link_info->bt_caton_num), "tx_retry", __le32_to_cpu(bt_link_info->bt_tx_retry_num));

		/*if (bt_link_info->curr_req_ongoing)
			seq_printf(seq, "time_from_cur_req_start : %7d\n", __le32_to_cpu(bt_link_info->time_diff_req_start));
		else
			seq_printf(seq, "time_to_next_req_start  : %7d\n", __le32_to_cpu(bt_link_info->time_diff_req_start)); //unuse */
	}

	return 0;
}

static int xradio_epta_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_epta_stat_show,
		inode->i_private);
}

static const struct file_operations fops_epta_stat = {
	.open = xradio_epta_stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t xradio_epta_stat_dbg_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = {0};
	char *endptr = NULL;
	u32 dbg_val = 0;

	count = (count > 11 ? 11 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	dbg_val = simple_strtoul(buf, &endptr, 16);
	if (count_ones(dbg_val) > 3 || dbg_val > 0x7ff) {
		SYS_WARN(1);
		return count;
	}
	epta_stat_dbg_ctrl = dbg_val;
	xradio_dbg(XRADIO_DBG_ALWY, "epta_stat_dbg_ctrl = 0x%x\n", epta_stat_dbg_ctrl);
	SYS_WARN(wsm_set_epta_stat_dbg_ctrl(hw_priv, epta_stat_dbg_ctrl));

	return count;
}

static ssize_t xradio_epta_stat_dbg_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[50];
	size_t size = 0;

	sprintf(buf, "epta_stat_dbg_ctrl = 0x%x\n", epta_stat_dbg_ctrl);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}
#endif

static int count_idx;

#ifdef SUPPORT_HT40

static int xradio_dbgstats_show(struct seq_file *seq, void *v)
{
	int ret;
	int avg_ampdu_len = 0;
	int FrameFail_ratio = 0;
	int FailByRts_ratio = 0;
	int FrameRetry_ratio = 0;
	int AMPDURetry_ratio = 0;
	int Retry1_ratio = 0;
	int Retry2_ratio = 0;
	int Retry3_ratio = 0;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_counters_table counters;
	struct wsm_ampducounters_table ampdu_counters;
	struct wsm_txpipe_counter txpipe;

	ret = wsm_get_counters_table(hw_priv, &counters);
	if (ret)
		return ret;

	ret = wsm_get_txpipe_table(hw_priv, &txpipe);
	if (ret)
		return ret;

	ret = wsm_get_ampducounters_table(hw_priv, &ampdu_counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y

#define PUT_COUNTER(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(counters.CAT_STR(count, name)))

#define PUT_AMPDU_COUNTER(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(ampdu_counters.CAT_STR(count, name)))

#define PUT_TXPIPE(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(txpipe.CAT_STR(count, name)))

	if (ampdu_counters.countTxAMPDUs) {
		avg_ampdu_len = (int)((ampdu_counters.countTxMPDUsInAMPDUs + \
				      (ampdu_counters.countTxAMPDUs>>1)) \
				      /ampdu_counters.countTxAMPDUs);
		AMPDURetry_ratio = (int)(ampdu_counters.countImplictBARFailures * \
					 100/ampdu_counters.countTxAMPDUs);
	}
	if (counters.countAckFailures) {
		Retry1_ratio = (int)(txpipe.count3*100/counters.countAckFailures);
		Retry2_ratio = (int)(txpipe.count4*100/counters.countAckFailures);
		Retry3_ratio = (int)(txpipe.count5*100/counters.countAckFailures);
	}
	if (ampdu_counters.countTxMPDUsInAMPDUs) {
		FrameFail_ratio = (int)(counters.countTxFrameFailures * \
				       1000/ampdu_counters.countTxMPDUsInAMPDUs);
		FrameRetry_ratio = (int)(counters.countAckFailures * \
				       100/ampdu_counters.countTxMPDUsInAMPDUs);
	}

	if (counters.countTxFrameFailures)
		FailByRts_ratio = (int)(txpipe.counta * \
					100/counters.countTxFrameFailures);

	seq_printf(seq, "===========================================\n");
	seq_printf(seq, "                   %02d\n", count_idx);
	seq_printf(seq, "===========================================\n");
	count_idx++;
	count_idx = count_idx % 100;
	PUT_COUNTER("RtsSuccess:        ", RtsSuccess);
	PUT_COUNTER("RtsFailures:       ", RtsFailures);
	seq_printf(seq, "Avg_AMPDU_Len:     %d\n", __le32_to_cpu(avg_ampdu_len));
	PUT_AMPDU_COUNTER("TxAMPDUs:          ", TxAMPDUs);
	PUT_AMPDU_COUNTER("TxMPDUsInAMPDUs:   ", TxMPDUsInAMPDUs);
	/* PUT_COUNTER      ("TxFrameRetries:    ", AckFailures); */
	/* PUT_COUNTER      ("TxFrameFailures:   ", TxFrameFailures); */
	PUT_TXPIPE("Failure_By_Rts:    ", a);
	/* PUT_AMPDU_COUNTER("BA-RX-Fails    ", ImplictBARFailures);
	PUT_AMPDU_COUNTER("TxAMPDUs       ", TxAMPDUs);
	PUT_TXPIPE       ("ReTx-AMPDUs    ", 2);
	PUT_TXPIPE       ("Retry_type1    ", 3);
	PUT_TXPIPE       ("Retry_type2    ", 4);
	PUT_TXPIPE       ("Retry_type3    ", 5); */
	seq_printf(seq, "==============\n");
	seq_printf(seq, "FrameFail_ratio:   %d%%%%\n",
		   __le32_to_cpu(FrameFail_ratio));
	seq_printf(seq, "FailByRts_ratio:   %d%%\n",
		   __le32_to_cpu(FailByRts_ratio));
	seq_printf(seq, "FrameRetry_ratio:  %d%%\n",
		   __le32_to_cpu(FrameRetry_ratio));
	seq_printf(seq, "AMPDURetry_ratio:  %d%%\n",
		   __le32_to_cpu(AMPDURetry_ratio));
	seq_printf(seq, "Retry1_ratio:      %d%%\n",
		   __le32_to_cpu(Retry1_ratio));
	seq_printf(seq, "Retry2_ratio:      %d%%\n",
		   __le32_to_cpu(Retry2_ratio));
	seq_printf(seq, "Retry3_ratio:      %d%%\n",
		   __le32_to_cpu(Retry3_ratio));

	seq_printf(seq, "==============\n");



#define PUT_TXRATE_COUNT(idx, bdsss, aofdm) do { \
		seq_printf(seq, "MCS%d:"  "[%8d][%8d] [%8d][%8d] %4s:" \
			"[%8d] %3s:[%8d]\n", idx, \
			__le32_to_cpu(TxedHtofdmRateMap[0][idx]), \
			__le32_to_cpu(TxedHtofdmRateMap[1][idx]), \
			__le32_to_cpu(TxedHtofdmRateMap[2][idx]), \
			__le32_to_cpu(TxedHtofdmRateMap[3][idx]), \
		#bdsss, __le32_to_cpu(TxedLegacyRateMap[0][idx]), \
		#aofdm, __le32_to_cpu(TxedLegacyRateMap[1][idx]));\
			TxedHtofdmRateMap[0][idx] = 0;\
			TxedHtofdmRateMap[1][idx] = 0;\
			TxedHtofdmRateMap[2][idx] = 0;\
			TxedHtofdmRateMap[3][idx] = 0;\
			TxedLegacyRateMap[0][idx] = 0;\
			TxedLegacyRateMap[1][idx] = 0;\
	} while (0)

		seq_printf(seq, "\nTxRateMap:\n");
		seq_printf(seq, "     " "[20SGI TX][20LGI TX] " \
					"[40SGI TX][40LGI TX] " \
					"     [DSSS  TX] " \
					"    [OFDM  TX]\n");

		PUT_TXRATE_COUNT(7, 11MS, 54M);
		PUT_TXRATE_COUNT(6, 11ML, 48M);
		PUT_TXRATE_COUNT(5, 5M5S, 36M);
		PUT_TXRATE_COUNT(4, 5M5L, 24M);
		PUT_TXRATE_COUNT(3,  2MS, 18M);
		PUT_TXRATE_COUNT(2,  2ML, 12M);
		PUT_TXRATE_COUNT(1, ----,  9M);
		PUT_TXRATE_COUNT(0,  1ML,  6M);

#undef PUT_TXRATE_COUNT

#define PUT_RXRATE_COUNT(idx, bdsss, aofdm) do { \
		seq_printf(seq, "MCS%d:"  "[%8d][%8d] [%8d][%8d] %4s:" \
			"[%8d] %3s:[%8d]\n", idx, \
			__le32_to_cpu(RxedHtofdmRateMap[0][idx]), \
			__le32_to_cpu(RxedHtofdmRateMap[1][idx]), \
			__le32_to_cpu(RxedHtofdmRateMap[2][idx]), \
			__le32_to_cpu(RxedHtofdmRateMap[3][idx]), \
		#bdsss, __le32_to_cpu(RxedLegacyRateMap[0][idx]), \
		#aofdm, __le32_to_cpu(RxedLegacyRateMap[1][idx]));\
			RxedHtofdmRateMap[0][idx] = 0;\
			RxedHtofdmRateMap[1][idx] = 0;\
			RxedHtofdmRateMap[2][idx] = 0;\
			RxedHtofdmRateMap[3][idx] = 0;\
			RxedLegacyRateMap[0][idx] = 0;\
			RxedLegacyRateMap[1][idx] = 0;\
	} while (0)

		seq_printf(seq, "\nRxRateMap:\n");
		seq_printf(seq, "     " "[20SGI RX][20LGI RX] " \
					"[40SGI RX][40LGI RX] " \
					"     [DSSS  RX] " \
					"    [OFDM  RX]\n");

		PUT_RXRATE_COUNT(7, 11MS, 54M);
		PUT_RXRATE_COUNT(6, 11ML, 48M);
		PUT_RXRATE_COUNT(5, 5M5S, 36M);
		PUT_RXRATE_COUNT(4, 5M5L, 24M);
		PUT_RXRATE_COUNT(3,  2MS, 18M);
		PUT_RXRATE_COUNT(2,  2ML, 12M);
		PUT_RXRATE_COUNT(1, ----,  9M);
		PUT_RXRATE_COUNT(0,  1ML,  6M);

#undef PUT_RXRATE_COUNT

#undef PUT_COUNTER
#undef PUT_AMPDU_COUNTER
#undef PUT_TXPIPE
#undef CAT_STR

	return 0;
}

#else

static int xradio_dbgstats_show(struct seq_file *seq, void *v)
{
	int ret;
	int avg_ampdu_len = 0;
	int FrameFail_ratio = 0;
	int FailByRts_ratio = 0;
	int FrameRetry_ratio = 0;
	int AMPDURetry_ratio = 0;
	int Retry1_ratio = 0;
	int Retry2_ratio = 0;
	int Retry3_ratio = 0;
	struct xradio_common *hw_priv = seq->private;
	struct wsm_counters_table counters;
	struct wsm_ampducounters_table ampdu_counters;
	struct wsm_txpipe_counter txpipe;

	ret = wsm_get_counters_table(hw_priv, &counters);
	if (ret)
		return ret;

	ret = wsm_get_txpipe_table(hw_priv, &txpipe);
	if (ret)
		return ret;

	ret = wsm_get_ampducounters_table(hw_priv, &ampdu_counters);
	if (ret)
		return ret;

#define CAT_STR(x, y) x ## y

#define PUT_COUNTER(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(counters.CAT_STR(count, name)))

#define PUT_AMPDU_COUNTER(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(ampdu_counters.CAT_STR(count, name)))

#define PUT_TXPIPE(tab, name) \
	seq_printf(seq, tab "%d\n", \
		__le32_to_cpu(txpipe.CAT_STR(count, name)))

#define PUT_RATE_COUNT(name, idx) do { \
	seq_printf(seq, "%s\t%d\n", #name, \
		__le32_to_cpu(TxedRateIdx_Map[idx])); \
	TxedRateIdx_Map[idx] = 0; \
	} while (0)

	if (ampdu_counters.countTxAMPDUs) {
		avg_ampdu_len = (int)((ampdu_counters.countTxMPDUsInAMPDUs + \
				      (ampdu_counters.countTxAMPDUs>>1)) \
				      /ampdu_counters.countTxAMPDUs);
		AMPDURetry_ratio = (int)(ampdu_counters.countImplictBARFailures * \
					 100/ampdu_counters.countTxAMPDUs);
	}
	if (counters.countAckFailures) {
		Retry1_ratio = (int)(txpipe.count3*100/counters.countAckFailures);
		Retry2_ratio = (int)(txpipe.count4*100/counters.countAckFailures);
		Retry3_ratio = (int)(txpipe.count5*100/counters.countAckFailures);
	}
	if (ampdu_counters.countTxMPDUsInAMPDUs) {
		FrameFail_ratio = (int)(counters.countTxFrameFailures * \
				       1000/ampdu_counters.countTxMPDUsInAMPDUs);
		FrameRetry_ratio = (int)(counters.countAckFailures * \
				       100/ampdu_counters.countTxMPDUsInAMPDUs);
	}

	if (counters.countTxFrameFailures)
		FailByRts_ratio = (int)(txpipe.counta * \
					100/counters.countTxFrameFailures);

	seq_printf(seq, "===========================================\n");
	seq_printf(seq, "                   %02d\n", count_idx);
	seq_printf(seq, "===========================================\n");
	count_idx++;
	count_idx = count_idx%100;
	PUT_COUNTER      ("RtsSuccess:        ", RtsSuccess);
	PUT_COUNTER      ("RtsFailures:       ", RtsFailures);
	seq_printf(seq, "Avg_AMPDU_Len:     %d\n", __le32_to_cpu(avg_ampdu_len));
	PUT_AMPDU_COUNTER("TxAMPDUs:          ", TxAMPDUs);
	PUT_AMPDU_COUNTER("TxMPDUsInAMPDUs:   ", TxMPDUsInAMPDUs);
	/* PUT_COUNTER      ("TxFrameRetries:    ", AckFailures); */
	/* PUT_COUNTER      ("TxFrameFailures:   ", TxFrameFailures); */
	PUT_TXPIPE       ("Failure_By_Rts:    ", a);
	/* PUT_AMPDU_COUNTER("BA-RX-Fails    ", ImplictBARFailures);
	PUT_AMPDU_COUNTER("TxAMPDUs       ", TxAMPDUs);
	PUT_TXPIPE       ("ReTx-AMPDUs    ", 2);
	PUT_TXPIPE       ("Retry_type1    ", 3);
	PUT_TXPIPE       ("Retry_type2    ", 4);
	PUT_TXPIPE       ("Retry_type3    ", 5); */
	seq_printf(seq, "==============\n");
	seq_printf(seq, "FrameFail_ratio:   %d%%%%\n",
		   __le32_to_cpu(FrameFail_ratio));
	seq_printf(seq, "FailByRts_ratio:   %d%%\n",
		   __le32_to_cpu(FailByRts_ratio));
	seq_printf(seq, "FrameRetry_ratio:  %d%%\n",
		   __le32_to_cpu(FrameRetry_ratio));
	seq_printf(seq, "AMPDURetry_ratio:  %d%%\n",
		   __le32_to_cpu(AMPDURetry_ratio));
	seq_printf(seq, "Retry1_ratio:      %d%%\n",
		   __le32_to_cpu(Retry1_ratio));
	seq_printf(seq, "Retry2_ratio:      %d%%\n",
		   __le32_to_cpu(Retry2_ratio));
	seq_printf(seq, "Retry3_ratio:      %d%%\n",
		   __le32_to_cpu(Retry3_ratio));

	seq_printf(seq, "==============\n");
	PUT_RATE_COUNT("65   Mbps:", 21);
	PUT_RATE_COUNT("58.5 Mbps:", 20);
	PUT_RATE_COUNT("52   Mbps:", 19);
	PUT_RATE_COUNT("39   Mbps:", 18);
	PUT_RATE_COUNT("26   Mbps:", 17);
	PUT_RATE_COUNT("19.5 Mbps:", 16);
	PUT_RATE_COUNT("13   Mbps:", 15);
	PUT_RATE_COUNT("6.5  Mbps:", 14);

#undef PUT_COUNTER
#undef PUT_AMPDU_COUNTER
#undef PUT_TXPIPE
#undef PUT_RATE_COUNT
#undef CAT_STR

	return 0;
}

#endif

static int xradio_dbgstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_dbgstats_show, inode->i_private);
}

static const struct file_operations fops_dbgstats = {
	.open = xradio_dbgstats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int xradio_generic_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t xradio_11n_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	struct ieee80211_supported_band *band =
		hw_priv->hw->wiphy->bands[NL80211_BAND_2GHZ];
	return simple_read_from_buffer(user_buf, count, ppos,
		    band->ht_cap.ht_supported ? "1\n" : "0\n", 2);
}

static ssize_t xradio_11n_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	struct ieee80211_supported_band *band[2] = {
		hw_priv->hw->wiphy->bands[NL80211_BAND_2GHZ],
		hw_priv->hw->wiphy->bands[NL80211_BAND_5GHZ],
	};
	char buf[1];
	int ena = 0;

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, 1))
		return -EFAULT;
	if (buf[0] == 1)
		ena = 1;

	band[0]->ht_cap.ht_supported = ena;
#ifdef CONFIG_XRADIO_5GHZ_SUPPORT
	band[1]->ht_cap.ht_supported = ena;
#endif /* CONFIG_XRADIO_5GHZ_SUPPORT */

	return count;
}

static const struct file_operations fops_11n = {
	.open = xradio_generic_open,
	.read = xradio_11n_read,
	.write = xradio_11n_write,
	.llseek = default_llseek,
};


static u32 fwdbg_ctrl;

static ssize_t xradio_fwdbg_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = {0};
	char *endptr = NULL;

	count = (count > 11 ? 11 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	fwdbg_ctrl = simple_strtoul(buf, &endptr, 16);
	xradio_dbg(XRADIO_DBG_ALWY, "fwdbg_ctrl = %d\n", fwdbg_ctrl);
	SYS_WARN(wsm_set_fw_debug_control(hw_priv, fwdbg_ctrl, 0));

	return count;
}

static ssize_t xradio_fwdbg_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[50];
	size_t size = 0;

	sprintf(buf, "fwdbg_ctrl = %u\n", fwdbg_ctrl);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}
static const struct file_operations fops_fwdbg = {
	.open = xradio_generic_open,
	.write = xradio_fwdbg_write,
	.read = xradio_fwdbg_read,
	.llseek = default_llseek,
};

/* read/write fw registers */
static ssize_t xradio_fwreg_rw(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char  buf[256] = {0};
	u16   buf_size = (count > 255 ? 255 : count);
	char *startptr = &buf[0];
	char *endptr   = NULL;
	u16   flag     = 0;
	int   i, end   = 16;

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	flag  = simple_strtoul(startptr, &endptr, 16);
	startptr = endptr+1;
	if (flag & WSM_REG_RW_F) {  /* write */
		WSM_REG_W reg_w;
		reg_w.flag = flag;
		reg_w.data_size = 0;
		if (flag & WSM_REG_BK_F)
			end = 2;

		for (i = 0; (i < end) && ((buf + buf_size - 12) > endptr); i++) {
			reg_w.arg[i].reg_addr = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
			reg_w.arg[i].reg_val  = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
		}
		if (i)
			reg_w.data_size = 4 + i * 8;
		xradio_dbg(XRADIO_DBG_ALWY, "W:flag=0x%x, size=%d\n",
			   reg_w.flag, reg_w.data_size);
		wsm_write_mib(hw_priv, WSM_MIB_ID_RW_FW_REG,
			      (void *)&reg_w, reg_w.data_size, 0);

	} else {  /* read */
		WSM_REG_R reg_r;
		reg_r.flag = flag;
		reg_r.data_size = 0;
		if (flag & WSM_REG_BK_F)
			end = 2;

		for (i = 0; (i < end) && ((buf + buf_size - 6) > endptr); i++) {
			reg_r.arg[i] = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
		}
		if (i)
			reg_r.data_size = 4 + i * 4;

		wsm_read_mib(hw_priv, WSM_MIB_ID_RW_FW_REG, (void *)&reg_r,
			     sizeof(WSM_REG_R), reg_r.data_size);

		xradio_dbg(XRADIO_DBG_ALWY, "R:flag=0x%x, size=%d\n",
			   reg_r.flag, reg_r.data_size);

		end = (reg_r.data_size >> 2) - 1;
		if (!end || !(reg_r.flag & WSM_REG_RET_F))
			return count;

		for (i = 0; i < end; i++) {
			xradio_dbg(XRADIO_DBG_ALWY, "0x%08x ", reg_r.arg[i]);
			if ((i & 3) == 3)
				xradio_dbg(XRADIO_DBG_ALWY, "\n");
		}
		xradio_dbg(XRADIO_DBG_ALWY, "\n");
	}
	return count;
}

static const struct file_operations fops_rw_fwreg = {
	.open = xradio_generic_open,
	.write = xradio_fwreg_rw,
	.llseek = default_llseek,
};

/* This ops only used in bh error occured already.
 * It can be dangerous to use it in normal status. */
static ssize_t xradio_fwreg_rw_direct(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	/* for H64 HIF test */
	struct xradio_common *hw_priv = file->private_data;
	char buf[256] = { 0 };
	u16 buf_size = (count > 255 ? 255 : count);
	char *startptr = &buf[0];
	char *endptr = NULL;
	u16 flag = 0;
	int i, end = 16;

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	flag = simple_strtoul(startptr, &endptr, 16);
	startptr = endptr + 1;
	if (flag & WSM_REG_RW_F) {  /* write */
		int ret = 0;
		u32 val32 = 0;
		WSM_REG_W reg_w;
		reg_w.flag = flag;
		reg_w.data_size = 0;
		if (flag & WSM_REG_BK_F)
			end = 2;

		for (i = 0; (i < end) && ((buf + buf_size - 12) > endptr); i++) {
			reg_w.arg[i].reg_addr = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
			reg_w.arg[i].reg_val  = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
		}

		if (!(reg_w.arg[0].reg_addr & 0xffff0000)) { /* means write register */
			ret = xradio_reg_write_32(hw_priv,
				reg_w.arg[0].reg_addr, reg_w.arg[0].reg_val);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W [register]-- " \
					   "write CONFIG err, ret is %d! \n",
					   __func__, ret);
			} else {
				xradio_dbg(XRADIO_DBG_ALWY, "%s:test HIF R/W [register]]-- " \
					   "write  register @0x%x, val is 0x%x\n",
					   __func__, reg_w.arg[0].reg_addr,
					   reg_w.arg[0].reg_val);
			}
		} else { /* means write memory */
			/* change to direct mode */
			ret = xradio_reg_read_32(hw_priv, HIF_CONFIG_REG_ID, &val32);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "reading CONFIG err, ret is %d! \n", __func__, ret);
			}
			ret = xradio_reg_write_32(hw_priv, HIF_CONFIG_REG_ID,
						  val32 | HIF_CONFIG_ACCESS_MODE_BIT);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "setting direct mode err, ret is %d! \n",
					   __func__, ret);
			}

			ret = xradio_ahb_write_32(hw_priv, reg_w.arg[0].reg_addr,
						  reg_w.arg[0].reg_val);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR,
					   "%s:AHB write test, val of addr %x is %x! \n",
					   __func__, reg_w.arg[0].reg_addr, reg_w.arg[0].reg_val);
			}

			/* return to queue mode */
			ret = xradio_reg_write_32(hw_priv, HIF_CONFIG_REG_ID,
						  val32 & ~HIF_CONFIG_ACCESS_MODE_BIT);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "setting queue mode err, ret is %d! \n", __func__, ret);
			}
		}
	} else {  /* read */
		WSM_REG_R reg_r;
		u32 val32 = 0;
		u32 mem_val = 0;
		int ret = 0;
		reg_r.flag = flag;
		reg_r.data_size = 0;
		if (flag & WSM_REG_BK_F)
			end = 2;

		for (i = 0; (i < end) && ((buf + buf_size - 6) > endptr); i++) {
			reg_r.arg[i] = simple_strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
		}
		/* if (i) reg_r.data_size = 4+i*4; */
		if (!(reg_r.arg[0] & 0xffff0000)) { /* means read register */
			ret = xradio_reg_read_32(hw_priv, (u16)reg_r.arg[0], &val32);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W [register]-- " \
					   "reading CONFIG err, ret is %d! \n",
					   __func__, ret);
			}
			xradio_dbg(XRADIO_DBG_ALWY, "%s:test HIF R/W [register]]-- " \
				   "reading  register @0x%x, val is 0x%x\n",
				   __func__, reg_r.arg[0], val32);
		} else { /* means read memory */

			/* change to direct mode */
			ret = xradio_reg_read_32(hw_priv, HIF_CONFIG_REG_ID, &val32);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "reading CONFIG err, ret is %d! \n",
					   __func__, ret);
			}
			ret = xradio_reg_write_32(hw_priv, HIF_CONFIG_REG_ID,
						   val32 | HIF_CONFIG_ACCESS_MODE_BIT);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "setting direct mode err, ret is %d! \n",
					    __func__, ret);
			}

			if (reg_r.arg[0] & 0x08000000) {
				ret = xradio_ahb_read_32(hw_priv, reg_r.arg[0], &mem_val);
				if (ret < 0) {
					xradio_dbg(XRADIO_DBG_ERROR, "%s:AHB read test err, " \
						   "val of addr %08x is %08x \n",
						   __func__, reg_r.arg[0], mem_val);
				}
				xradio_dbg(XRADIO_DBG_ALWY, "[%08x] = 0x%08x\n",
					   reg_r.arg[0], mem_val);
			} else if (reg_r.arg[0] & 0x09000000) {
				ret = xradio_apb_read_32(hw_priv, reg_r.arg[0], &mem_val);
				if (ret < 0) {
					xradio_dbg(XRADIO_DBG_ERROR, "%s:APB read test err, " \
						   "val of addr %08x is %08x \n",
						   __func__, reg_r.arg[0], mem_val);
				}
				xradio_dbg(XRADIO_DBG_ALWY, "[%08x] = 0x%08x\n",
					   reg_r.arg[0], mem_val);
			}

			/* return to queue mode */
			ret = xradio_reg_write_32(hw_priv, HIF_CONFIG_REG_ID,
						  val32 & ~HIF_CONFIG_ACCESS_MODE_BIT);
			if (ret < 0) {
				xradio_dbg(XRADIO_DBG_ERROR, "%s:test HIF R/W -- " \
					   "setting queue mode err, ret is %d! \n",
					   __func__, ret);
			}
		}
	}
	return count;
}
static const struct file_operations fops_rw_fwreg_direct = {
	.open = xradio_generic_open,
	.write = xradio_fwreg_rw_direct,
	.llseek = default_llseek,
};
/* setting ampdu_len */
u16 ampdu_len[2] = {16, 16};
static ssize_t xradio_ampdu_len_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = { 0 };
	char *endptr = NULL;
	u8 if_id = 0;

	count = (count > 11 ? 11 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	if_id = simple_strtoul(buf, &endptr, 10);
	ampdu_len[if_id] = simple_strtoul(endptr + 1, NULL, 10);

	xradio_dbg(XRADIO_DBG_ALWY, "vif=%d, ampdu_len = %d\n",
		   if_id, ampdu_len[if_id]);
	wsm_write_mib(hw_priv, WSM_MIB_ID_SET_AMPDU_NUM,
		      &ampdu_len[if_id], sizeof(u16), if_id);

	return count;
}

static ssize_t xradio_ampdu_len_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;

	sprintf(buf, "ampdu_len(0)=%d, (1)=%d\n", ampdu_len[0], ampdu_len[1]);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static const struct file_operations fops_ampdu_len = {
	.open = xradio_generic_open,
	.write = xradio_ampdu_len_write,
	.read = xradio_ampdu_len_read,
	.llseek = default_llseek,
};


/* setting rts threshold. */
u32 rts_threshold[2] = {3000, 3000};
static ssize_t xradio_rts_threshold_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;

	sprintf(buf, "rts_threshold(0)=%d, (1)=%d\n",
		rts_threshold[0], rts_threshold[1]);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_rts_threshold_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = { 0 };
	char *endptr = NULL;
	u8 if_id = 0;

	count = (count > 11 ? 11 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	if_id = simple_strtoul(buf, &endptr, 10);
	rts_threshold[if_id] = simple_strtoul(endptr + 1, NULL, 10);

	xradio_dbg(XRADIO_DBG_ALWY, "vif=%d, rts_threshold = %d\n",
		   if_id, rts_threshold[if_id]);
	wsm_write_mib(hw_priv, WSM_MIB_ID_DOT11_RTS_THRESHOLD,
		      &rts_threshold[if_id], sizeof(u32), if_id);

	return count;
}

static const struct file_operations fops_rts_threshold = {
	.open = xradio_generic_open,
	.write = xradio_rts_threshold_set,
	.read = xradio_rts_threshold_get,
	.llseek = default_llseek,
};

/* disable low power mode. */
u8 low_pwr_disable;
static ssize_t xradio_low_pwr_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;

	sprintf(buf, "low_pwr_disable=%d\n", low_pwr_disable);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_low_pwr_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = { 0 };
	char *endptr = NULL;
	int if_id = 0;
	u32 val = wsm_power_mode_quiescent;

	count = (count > 11 ? 11 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	low_pwr_disable = simple_strtoul(buf, &endptr, 16);
	xradio_dbg(XRADIO_DBG_ALWY, "low_pwr_disable=%d\n", low_pwr_disable);

	if (low_pwr_disable)
		val = wsm_power_mode_active;

	val |= BIT(4);   /* disableMoreFlagUsage */

	for (if_id = 0; if_id < xrwl_get_nr_hw_ifaces(hw_priv); if_id++)
		wsm_write_mib(hw_priv, WSM_MIB_ID_OPERATIONAL_POWER_MODE, &val,
				sizeof(val), if_id);

	return count;
}

static const struct file_operations fops_low_pwr = {
	.open = xradio_generic_open,
	.write = xradio_low_pwr_set,
	.read = xradio_low_pwr_get,
	.llseek = default_llseek,
};

/* disable ps mode(80211 protol). */
static ssize_t xradio_ps_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;

	sprintf(buf, "ps_disable=%d, idleperiod=%d, changeperiod=%d\n",
		ps_disable, ps_idleperiod, ps_changeperiod);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_ps_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[20] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;
	struct wsm_set_pm ps = {
		.pmMode = WSM_PSM_FAST_PS,
		.fastPsmIdlePeriod = 0xC8  /* defaut 100ms */
	};

	count = (count > 19 ? 19 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	ps_disable = simple_strtoul(start, &endptr, 10);
	start = endptr + 1;
	if (start < buf + count)
		ps_idleperiod = simple_strtoul(start, &endptr, 10) & 0xff;
	start = endptr + 1;
	if (start < buf + count)
		ps_changeperiod = simple_strtoul(start, &endptr, 10) & 0xff;

	xradio_dbg(XRADIO_DBG_ALWY,
		   "ps_disable=%d, idleperiod=%d, changeperiod=%d\n",
		   ps_disable, ps_idleperiod, ps_changeperiod);

	/* set pm for debug */
	if (ps_disable)
		ps.pmMode = WSM_PSM_ACTIVE;
	if (ps_idleperiod)
		ps.fastPsmIdlePeriod = ps_idleperiod << 1;
	if (ps_changeperiod)
		ps.apPsmChangePeriod = ps_changeperiod << 1;

	wsm_set_pm(hw_priv, &ps, 0);
	if (hw_priv->vif_list[1])
		wsm_set_pm(hw_priv, &ps, 1);

	return count;
}

static const struct file_operations fops_ps_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_ps_set,
	.read = xradio_ps_get,
	.llseek = default_llseek,
};

/* for retry debug. */
u8 retry_dbg;
u8 tx_short;   /* save orgin value. */
u8 tx_long;    /* save orgin value. */

static ssize_t xradio_retry_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100];
	size_t size = 0;

	sprintf(buf, "retry_dbg=%d, short=%d, long=%d\n", retry_dbg,
		hw_priv->short_frame_max_tx_count,
		hw_priv->long_frame_max_tx_count);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_retry_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[20] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	count = (count > 19 ? 19 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	retry_dbg = (simple_strtoul(start, &endptr, 10) & 0x1);
	if (retry_dbg) { /* change retry.*/
		if (!tx_short)
			tx_short = hw_priv->short_frame_max_tx_count;
		if (!tx_long)
			tx_long = hw_priv->long_frame_max_tx_count;
		start = endptr + 1;
		if (start < buf + count) {
			hw_priv->short_frame_max_tx_count =
			    simple_strtoul(start, &endptr, 10) & 0xf;
			start = endptr + 1;
			if (start < buf + count)
				hw_priv->long_frame_max_tx_count =
				    simple_strtoul(start, &endptr, 10) & 0xf;
		}
		xradio_dbg(XRADIO_DBG_ALWY, "retry_dbg on, s=%d, l=%d\n",
			  hw_priv->short_frame_max_tx_count,
			  hw_priv->long_frame_max_tx_count);
	} else {  /* restore retry. */
		if (tx_short) {
			hw_priv->short_frame_max_tx_count = tx_short;
			tx_short = 0;
		}
		if (tx_long) {
			hw_priv->long_frame_max_tx_count = tx_long;
			tx_long = 0;
		}
		xradio_dbg(XRADIO_DBG_ALWY, "retry_dbg off, s=%d, l=%d\n",
			  hw_priv->short_frame_max_tx_count,
			  hw_priv->long_frame_max_tx_count);
	}
	retry_dbg |= 0x2;
	return count;
}

static const struct file_operations fops_retry_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_retry_set,
	.read = xradio_retry_get,
	.llseek = default_llseek,
};

/* for rates debug. */

#ifdef SUPPORT_HT40

u8  rates_dbg_en;
u16 rates_debug[6] = {0x0};
u8  Ratecnt_dbg;

static ssize_t xradio_rates_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[200];
	size_t size = 0;
	sprintf(buf, "rates_dbg_en=%d, [0]=0x%04x, [1]=0x%04x, [2]=0x%04x " \
		"[3]=0x%04x, [4]=0x%04x, [5]=0x%04x\n", (rates_dbg_en & 0x1),
		rates_debug[0], rates_debug[1], rates_debug[2],
		rates_debug[3], rates_debug[4], rates_debug[5]);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_rates_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[50] = {0};
	char *start  = &buf[0];
	char *endptr = NULL;
	int i = 0;
	count = (count > 49 ? 49 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	rates_dbg_en &= ~0x1;
	if (simple_strtoul(start, &endptr, 10)) {
		for (i = 0; i < 6; i++) {
			start = endptr+1;
			if (start < buf+count)
				rates_debug[i] = simple_strtoul(start, &endptr, 16);
			else
				break;
		}

		Ratecnt_dbg  = i;
		rates_dbg_en = 0x1;

		if (Ratecnt_dbg)
			xradio_dbg(XRADIO_DBG_ERROR, "rates_dbg on, entry_cnt=%d!\n", Ratecnt_dbg);
		else
			xradio_dbg(XRADIO_DBG_ERROR, "rates_dbg fail, invaid params!\n");

	} else {
		xradio_dbg(XRADIO_DBG_ERROR, "rates_dbg off\n");
	}
	return count;
}

#else

u8 rates_dbg_en;
u32 rates_debug[3];
u8  maxRate_dbg;

static ssize_t xradio_rates_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;
	sprintf(buf, "rates_dbg_en=%d, [0]=0x%08x, [1]=0x%08x, [2]=0x%08x\n",
		(rates_dbg_en & 0x1), rates_debug[2],
		 rates_debug[1], rates_debug[0]);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_rates_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[50] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;
	int i = 0;
	count = (count > 49 ? 49 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	rates_dbg_en &= ~0x1;
	if (simple_strtoul(start, &endptr, 10)) {
		start = endptr + 1;
		if (start < buf + count)
			rates_debug[2] = simple_strtoul(start, &endptr, 16);

		start = endptr + 1;
		if (start < buf + count)
			rates_debug[1] = simple_strtoul(start, &endptr, 16);

		start = endptr + 1;
		if (start < buf + count)
			rates_debug[0] = simple_strtoul(start, &endptr, 16);

		for (i = 21; i >= 0; i--) {
			if ((rates_debug[i >> 3] >> ((i & 0x7) << 2)) & 0xf) {
				maxRate_dbg = i;
				rates_dbg_en |= 0x1;
				break;
			}
		}
		if (rates_dbg_en & 0x1) {
			xradio_dbg(XRADIO_DBG_ALWY,
				   "rates_dbg on, maxrate=%d!\n", maxRate_dbg);
		} else {
			xradio_dbg(XRADIO_DBG_ALWY, "rates_dbg fail, invaid params!\n");
		}
	} else {
		xradio_dbg(XRADIO_DBG_ALWY, "rates_dbg off\n");
	}
	return count;
}

#endif

static const struct file_operations fops_rates_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_rates_set,
	.read = xradio_rates_get,
	.llseek = default_llseek,
};


/* for backoff setting. */
struct wsm_backoff_ctrl backoff_ctrl;

static ssize_t xradio_backoff_ctrl_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;
	sprintf(buf, "backoff_ctrl_en=%d, min=%d, max=%d\n",
		backoff_ctrl.enable,
		backoff_ctrl.min,
		backoff_ctrl.max);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_backoff_ctrl_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[20] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;
	count = (count > 19 ? 19 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	backoff_ctrl.enable = simple_strtoul(start, &endptr, 10);
	if (backoff_ctrl.enable) {
		start = endptr + 1;
		if (start < buf + count)
			backoff_ctrl.min = simple_strtoul(start, &endptr, 10);
		start = endptr + 1;
		if (start < buf + count)
			backoff_ctrl.max = simple_strtoul(start, &endptr, 10);

		xradio_dbg(XRADIO_DBG_ALWY, "backoff_ctrl on\n");
	} else {
		xradio_dbg(XRADIO_DBG_ALWY, "backoff_ctrl off\n");
	}
	wsm_set_backoff_ctrl(hw_priv, &backoff_ctrl);
	return count;
}

static const struct file_operations fops_backoff_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_backoff_ctrl_set,
	.read = xradio_backoff_ctrl_get,
	.llseek = default_llseek,
};

/* for TALA(Tx-Ampdu-Len-Adaption) setting. */
struct wsm_tala_para tala_para;
static ssize_t xradio_tala_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[100];
	size_t size = 0;
	sprintf(buf, "tala_para=0x%08x, tala_thresh=0x%08x\n",
		tala_para.para, tala_para.thresh);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_tala_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[30] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;
	count = (count > 29 ? 29 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (start < buf + count)
		tala_para.para = simple_strtoul(start, &endptr, 16);
	start = endptr + 1;
	if (start < buf + count)
		tala_para.thresh = simple_strtoul(start, &endptr, 16);

	wsm_set_tala(hw_priv, &tala_para);
	return count;
}

static const struct file_operations fops_tala_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_tala_set,
	.read = xradio_tala_get,
	.llseek = default_llseek,
};

/* Tx power debug */
char buf_show[1024] = { 0 };

typedef struct _PWR_INFO_TBL {
	u8 Index;
	u8 u8Complete;
	s16 s16TargetPwr;
	s16 s16AdjustedPower;
	s16 s16SmthErrTerm;
	u32 u32Count;
	u16 u16PpaVal;
	u16 u16DigVal;
} PWR_CTRL_TBL;
struct _TX_PWR_SHOW {
	u8 InfoID;
	u8 Status;
	u16 reserved;
	PWR_CTRL_TBL table[16];
} pwr_ctrl;
static ssize_t xradio_tx_pwr_show(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;

	int pos = 0, i = 0;
	pwr_ctrl.InfoID = 0x1;
	wsm_read_mib(hw_priv, WSM_MIB_ID_TX_POWER_INFO,
		     (void *)&pwr_ctrl, sizeof(pwr_ctrl), 4);

	if (pwr_ctrl.Status) {
		pos += sprintf(&buf_show[pos],
			       "read TX_POWER_INFO error=%x\n",
				pwr_ctrl.Status);
	} else {
		for (i = 0; i < 16; i++) {
			pos += sprintf(&buf_show[pos], "M%d:%d, ALG=%d, DIG=%d\n", i,
				       pwr_ctrl.table[i].s16AdjustedPower,
				       pwr_ctrl.table[i].u16PpaVal,
				       pwr_ctrl.table[i].u16DigVal);
		}
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf_show, pos);
}

static ssize_t xradio_tx_pwr_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	return count;
}

static const struct file_operations fops_tx_pwr_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_tx_pwr_set,
	.read = xradio_tx_pwr_show,
	.llseek = default_llseek,
};

/* TPA debug */
#define CASE_NUM   9
#define MAX_POINTS 4
#define PWR_LEVEL_NUM 40
#define MODULN_NUM 11
typedef struct tag_pwr_modulation {
	u8      def_pwr_idx;  /* default power index of modulation.*/
	u8      max_pwr_idx;  /* max power index of modulation.*/
	u8      mid_pwr_idx;  /* power index of middle point.*/
	u8      cur_point;    /* current sample point.*/

	u8      max_point;   /* the point has max q value.*/
	u8      max_stable;  /* counter of stable max of the same point.*/
	u8      exception;   /* the counter of exception case.*/
	u8      listen_def;  /* whether to listen to default point.*/

	u16     mod_smp_cnt;   /* total sample of the modulation.*/
	u16     update_cnt;    /* counter of power update.*/
	u32     update_time;   /* last time of power update.*/
	u16     smp_points[MAX_POINTS*2];

	u8      reserved;
	u8      last_rate;
	u16     last_max_Q;
} PWR_MODULN;

typedef struct tag_tpa_debug {
	u32     update_total[MODULN_NUM];
	u32     power_sum[MODULN_NUM];

	u16     smp_case[CASE_NUM]; /* counter of every case.*/
	u16     reserved0;
	u16     smp_move_cnt[MAX_POINTS];    /* counter of movement of update power.*/
	u16     max_point_cnt[MAX_POINTS];   /* counter of max point.*/

	u16     smp_thresh_q_cnt;
	u16     smp_timeout;
	u16     smp_listdef_cnt;
	u16     smp_excep_cnt;
	u16     smp_stable_cnt;

	u8      reserved2;
	u8      smp_last_moduln;
	u16     point_last_smp[MAX_POINTS*2];  /* Q value of point last update.*/
} TPA_DEBUG_INFO;

typedef struct tag_tpa_control {
	u8      tpa_enable;
	u8      tpa_initialized;
	u8      point_interval;
	u8      point_step;

	u16     thresh_q;
	u16     thresh_time;
	u16     thresh_update;
	u8      thresh_def_lstn;
	u8      thresh_stable;
	u8      pwr_level[PWR_LEVEL_NUM];
} TPA_CONTROL;

struct _TPA_INFO {
	u8  InfoID;
	u8  Status;
	u8  node;
	u8  reserved;
	union {
		TPA_DEBUG_INFO debug;
		TPA_CONTROL    ctrl;
		PWR_MODULN moduln[MODULN_NUM];
	} u;
} tpa_info;
static ssize_t xradio_tpa_ctrl_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	int pos = 0, i = 0;
	memset(&tpa_info, 0, sizeof(tpa_info));
	tpa_info.InfoID = 0x03;
	tpa_info.node   = 0;
	wsm_read_mib(hw_priv, WSM_MIB_ID_TPA_DEBUG_INFO,
		     (void *)&tpa_info, sizeof(tpa_info), 4);

	if (tpa_info.Status && tpa_info.InfoID != 0x43) {
		pos += sprintf(&buf_show[pos],
			       "read TPA_DEBUG_INFO error=%x\n",
			       tpa_info.Status);
	} else {
		u8  *pwr = &tpa_info.u.ctrl.pwr_level[0];

		pos += sprintf(&buf_show[pos],
			       "en=%d, init=%d, intvl=%d, step=%d\n",
			       tpa_info.u.ctrl.tpa_enable,
			       tpa_info.u.ctrl.tpa_initialized,
			       tpa_info.u.ctrl.point_interval,
			       tpa_info.u.ctrl.point_step);
		pos += sprintf(&buf_show[pos], "th_q=%d, th_tm=%d, th_updt=%d, " \
			       "th_def_lstn=%d, th_stbl=%d\n",
			       tpa_info.u.ctrl.thresh_q,
			       tpa_info.u.ctrl.thresh_time,
			       tpa_info.u.ctrl.thresh_update,
			       tpa_info.u.ctrl.thresh_def_lstn,
			       tpa_info.u.ctrl.thresh_stable);
	  for (i = 0; i < 4; i++) {
			pos += sprintf(&buf_show[pos],
				       "pwr lvl=%d.%d, %d.%d, %d.%d, %d.%d\n",
				       pwr[0]>>3, ((pwr[0]%8)*100)>>3,
				       pwr[1]>>3, ((pwr[1]%8)*100)>>3,
				       pwr[2]>>3, ((pwr[2]%8)*100)>>3,
				       pwr[3]>>3, ((pwr[3]%8)*100)>>3);
			pwr += 4;
		}
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf_show, pos);
}

struct TPA_CONTROL_SET {
	u8      tpa_enable;
	u8      reserved;
	u8      point_interval;
	u8      point_step;

	u16     thresh_q;
	u16     thresh_time;
	u16     thresh_update;
	u8      thresh_def_lstn;
	u8      thresh_stable;
} tpa_ctrl_set;
static ssize_t xradio_tpa_ctrl_set(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buffer[256] = { 0 };
	char *buf = &buffer[0];
	u16 buf_size = (count > 255 ? 255 : count);
	char *startptr = &buffer[0];
	char *endptr = NULL;

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;


	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.tpa_enable = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.point_interval = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.point_step = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}

	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.thresh_q = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.thresh_time = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.thresh_update = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.thresh_def_lstn = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	if ((buf + buf_size) > endptr) {
		tpa_ctrl_set.thresh_stable = simple_strtoul(startptr, &endptr, 10);
		startptr = endptr + 1;
	}
	wsm_write_mib(hw_priv, WSM_MIB_ID_SET_TPA_PARAM,
		      (void *)&tpa_ctrl_set, sizeof(tpa_ctrl_set), 0);

	return count;
}

static const struct file_operations fops_tpa_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_tpa_ctrl_set,
	.read = xradio_tpa_ctrl_get,
	.llseek = default_llseek,
};

u8 tpa_node_dbg;
static int xradio_tpa_debug(struct seq_file *seq, void *v)
{
	int ret, i;
	struct xradio_common *hw_priv = seq->private;

#define PUT_TPA_MODULN(tab, name) \
	seq_printf(seq, tab":\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", \
		   __le32_to_cpu(tpa_info.u.moduln[0].name), \
		   __le32_to_cpu(tpa_info.u.moduln[1].name), \
		   __le32_to_cpu(tpa_info.u.moduln[2].name), \
		   __le32_to_cpu(tpa_info.u.moduln[3].name), \
		   __le32_to_cpu(tpa_info.u.moduln[4].name), \
		   __le32_to_cpu(tpa_info.u.moduln[5].name), \
		   __le32_to_cpu(tpa_info.u.moduln[6].name), \
		   __le32_to_cpu(tpa_info.u.moduln[7].name), \
		   __le32_to_cpu(tpa_info.u.moduln[8].name), \
		   __le32_to_cpu(tpa_info.u.moduln[9].name), \
		   __le32_to_cpu(tpa_info.u.moduln[10].name))

	tpa_info.InfoID = 0x01;
	tpa_info.node = tpa_node_dbg;
	ret = wsm_read_mib(hw_priv, WSM_MIB_ID_TPA_DEBUG_INFO,
			   (void *)&tpa_info, sizeof(tpa_info), 4);

	if (tpa_info.Status && tpa_info.InfoID != 0x41) {
		seq_printf(seq, "read TPA_DEBUG_INFO error=%x\n", tpa_info.Status);
	} else {
		seq_printf(seq, "\t\tm0\tm1\tm2\tm3\tm4\tm5\tm6\tm7\tm8\tm9\tm10\t\n");

		PUT_TPA_MODULN("max_idx", max_pwr_idx);
		PUT_TPA_MODULN("def_idx", def_pwr_idx);
		PUT_TPA_MODULN("mid_idx", mid_pwr_idx);
		PUT_TPA_MODULN("cur_pt ", cur_point);
		PUT_TPA_MODULN("max_pt ", max_point);
		PUT_TPA_MODULN("stable ", max_stable);
		PUT_TPA_MODULN("exceptn", exception);
		PUT_TPA_MODULN("listen ", listen_def);
		PUT_TPA_MODULN("smp_cnt", mod_smp_cnt);
		PUT_TPA_MODULN("update ", update_cnt);

		PUT_TPA_MODULN("pt[0]  ", smp_points[0]);
		PUT_TPA_MODULN("pt[0]  ", smp_points[1]);
		PUT_TPA_MODULN("pt[1]  ", smp_points[2]);
		PUT_TPA_MODULN("pt[1]  ", smp_points[3]);
		PUT_TPA_MODULN("pt[2]  ", smp_points[4]);
		PUT_TPA_MODULN("pt[2]  ", smp_points[5]);
		PUT_TPA_MODULN("pt[3]  ", smp_points[6]);
		PUT_TPA_MODULN("pt[3]  ", smp_points[7]);

		PUT_TPA_MODULN("rate   ", last_rate);
		PUT_TPA_MODULN("Max Q  ", last_max_Q);
	}
#undef PUT_TPA_MODULN

#define SMP_CASE(i)    __le32_to_cpu(tpa_info.u.debug.smp_case[i])
#define PWR_LVL_S(n)  (tpa_info.u.debug.power_sum[n]>>3)


	tpa_info.InfoID = 0x02;
	tpa_info.node = tpa_node_dbg;
	ret = wsm_read_mib(hw_priv, WSM_MIB_ID_TPA_DEBUG_INFO,
			   (void *)&tpa_info, sizeof(tpa_info), 4);

	if (tpa_info.Status && tpa_info.InfoID != 0x42) {
		seq_printf(seq, "read TPA_DEBUG_INFO error=%x\n", tpa_info.Status);
	} else {
		for (i = 0; i < MODULN_NUM; i++) {
			if (tpa_info.u.debug.update_total[i])
				tpa_info.u.debug.power_sum[i] /=
				    tpa_info.u.debug.update_total[i];
			else
				tpa_info.u.debug.power_sum[i] = 0;
		}
		seq_printf(seq, "\nupdate_total:\t%d\t%d\t%d\t%d\t" \
			   "%d\t%d\t%d\t%d\t%d\t%d\t%d\n", \
			   __le32_to_cpu(tpa_info.u.debug.update_total[0]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[1]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[2]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[3]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[4]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[5]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[6]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[7]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[8]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[9]), \
			   __le32_to_cpu(tpa_info.u.debug.update_total[10]));

		seq_printf(seq, "pwr_avrg:\t%d\t%d\t%d\t%d\t%d\t%d\t"
				"%d\t%d\t%d\t%d\t%d\n", \
			   PWR_LVL_S(0), \
			   PWR_LVL_S(1), \
			   PWR_LVL_S(2), \
			   PWR_LVL_S(3), \
			   PWR_LVL_S(4), \
			   PWR_LVL_S(5), \
			   PWR_LVL_S(6), \
			   PWR_LVL_S(7), \
			   PWR_LVL_S(8), \
			   PWR_LVL_S(9), \
			   PWR_LVL_S(10));

		seq_printf(seq, "SMP_CASE: %d, %d, %d, %d(E), " \
			   "%d, %d, %d(E), %d(E), %d\n",
			   SMP_CASE(0), SMP_CASE(1), SMP_CASE(2), SMP_CASE(3),
			   SMP_CASE(4), SMP_CASE(5), SMP_CASE(6), SMP_CASE(7),
			   SMP_CASE(8));
		seq_printf(seq, "MAX: M=%d, L=%d, R=%d, D=%d\n",
			   tpa_info.u.debug.max_point_cnt[0],
			   tpa_info.u.debug.max_point_cnt[1],
			   tpa_info.u.debug.max_point_cnt[2],
			   tpa_info.u.debug.max_point_cnt[3]);
		seq_printf(seq, "MOVE: M=%d, L=%d, R=%d, D=%d\n",
			   tpa_info.u.debug.smp_move_cnt[0],
			   tpa_info.u.debug.smp_move_cnt[1],
			   tpa_info.u.debug.smp_move_cnt[2],
			   tpa_info.u.debug.smp_move_cnt[3]);
		seq_printf(seq, "listen=%d, timeout=%d, thresh_q=%d, " \
			   "excep=%d, stable=%d\n",
			   tpa_info.u.debug.smp_listdef_cnt,
			   tpa_info.u.debug.smp_timeout,
			   tpa_info.u.debug.smp_thresh_q_cnt,
			   tpa_info.u.debug.smp_excep_cnt,
			   tpa_info.u.debug.smp_stable_cnt);

		seq_printf(seq, "lsat Moduln=%d, M=%d, %d; " \
			   "L=%d, %d; R=%d, %d; D=%d, %d\n",
			   tpa_info.u.debug.smp_last_moduln,
			   tpa_info.u.debug.point_last_smp[0],
			   tpa_info.u.debug.point_last_smp[1],
			   tpa_info.u.debug.point_last_smp[2],
			   tpa_info.u.debug.point_last_smp[3],
			   tpa_info.u.debug.point_last_smp[4],
			   tpa_info.u.debug.point_last_smp[5],
			   tpa_info.u.debug.point_last_smp[6],
			   tpa_info.u.debug.point_last_smp[7]);
	}

#undef PWR_LVL_S
#undef SMP_CASE

	return 0;
}

static int xradio_tpa_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_tpa_debug,
		inode->i_private);
}

static const struct file_operations fops_tpa_debug = {
	.open = xradio_tpa_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* policy_info */
u32 tx_retrylimit;
u32 tx_lower_limit;
u32 tx_over_limit;
int retry_mis;
u32 policy_upload;
u32 policy_num;

static ssize_t xradio_policy_info(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data; */
	char buf[256];
	size_t size = 0;
	sprintf(buf, "tx_retrylimit=%d, tx_lower_limit=%d, " \
		"tx_over_limit=%d, retry_mis=%d\n" \
		"policy_upload=%d, policy_num=%d\n",
		tx_retrylimit, tx_lower_limit, tx_over_limit, retry_mis,
		policy_upload, policy_num);
	size = strlen(buf);

	/* clear counters */
	tx_retrylimit  = 0;
	tx_lower_limit = 0;
	tx_over_limit  = 0;
	retry_mis      = 0;
	policy_upload  = 0;
	policy_num     = 0;

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static const struct file_operations fops_policy_info = {
	.open   = xradio_generic_open,
	.read   = xradio_policy_info,
	.llseek = default_llseek,
};

/* info of interruption */
u32 irq_count;
u32 int_miss_cnt;
u32 fix_miss_cnt;
u32 next_rx_cnt;
u32 rx_total_cnt;
u32 tx_total_cnt;
u32 tx_buf_limit;
u32 tx_limit;
u32 tx_rx_idle;
u32 bh_idle;
u32 queue_lock_cnt;
u32 overfull_lock_cnt;
u32 policy_lock_cnt;

static ssize_t xradio_bh_statistic(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[512];
	size_t size = 0;
	sprintf(buf, "irq_count=%u, rx_total=%u, rx_miss=%u, "
		"rx_fix=%u, rx_next=%u, rx_burst=%d, irq/rx=%u%%\n"
		"tx_total=%u, tx_burst=%d, tx_limit=%u, tx_buf_limit=%u, "
		"limit/tx=%u%%, buf_limit/limit=%u%%, tx_rx_idle=%u, bh_idle=%u\n"
		"queue_lock_cnt=%u, overfull_lock_cnt=%u, policy_lock_cnt=%u\n",
		irq_count, rx_total_cnt, int_miss_cnt, fix_miss_cnt,
		next_rx_cnt, hw_priv->debug->rx_burst,
		(rx_total_cnt ? irq_count*100/rx_total_cnt : 0),
		tx_total_cnt, hw_priv->debug->tx_burst, tx_limit, tx_buf_limit,
		(tx_total_cnt ? tx_limit*100/tx_total_cnt : 0),
		(tx_limit ? tx_buf_limit*100/tx_limit : 0),
		tx_rx_idle, bh_idle, queue_lock_cnt, overfull_lock_cnt,
		policy_lock_cnt);
	size = strlen(buf);

	/*clear counters*/
	irq_count    = 0;
	int_miss_cnt = 0;
	fix_miss_cnt = 0;
	next_rx_cnt  = 0;
	rx_total_cnt = 0;
	tx_total_cnt = 0;
	tx_buf_limit = 0;
	tx_limit     = 0;
	tx_rx_idle   = 0;
	bh_idle      = 0;
	queue_lock_cnt = 0;
	overfull_lock_cnt = 0;
	policy_lock_cnt = 0;
	hw_priv->debug->rx_burst = 0;
	hw_priv->debug->tx_burst = 0;

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static const struct file_operations fops_bh_stat = {
	.open = xradio_generic_open,
	.read = xradio_bh_statistic,
	.llseek = default_llseek,
};

/* time info of bh tx and rx */
#if PERF_INFO_TEST
static inline void perf_info_reset(struct perf_info *info)
{
	memset(info, 0, sizeof(struct perf_info));
	info->min_time = (u32)-1;
}
struct perf_info data_tx;
struct perf_info data_rx;
struct perf_info bh_others;
struct perf_info bh_wait;
struct perf_info sdio_reg;
struct perf_info prepare_tx;
struct perf_info sdio_write;
struct perf_info dev_wake;
struct perf_info prepare_rx;
struct perf_info sdio_read;
struct perf_info handle_rx;

#ifdef BH_PROC_THREAD
struct perf_info get_item;
struct perf_info wake_proc;
struct perf_info proc_rx;
struct perf_info proc_tx;
struct perf_info proc_wait;
#endif
struct perf_info ind_rx;
struct perf_info mac_rx;
struct perf_info mac_tx;

#if TEST_GETTIME
struct perf_info get_time;
#endif

struct timeval last_showtime;
static inline u32 xradio_show_intv(struct timeval *showtime)
{
	u32 time_int;
	struct timeval time_now;
	xr_do_gettimeofday(&time_now);
	time_int = (time_now.tv_sec - showtime->tv_sec) * 100000 + \
			    (time_now.tv_usec - showtime->tv_usec) / 10;
	xr_do_gettimeofday(showtime);
	return time_int; /*10us*/
}
/*mac(26) + IV(8) + LLC(8) + IP(20) + TCP(20) or UDP(8)*/
#define TX_OVERHEAD (sizeof(struct wsm_tx)+26+8+20+20)
#define RX_OVERHEAD (12+26+8+20+20)
static int xradio_perf_info(struct seq_file *seq, void *v)
{
	/* struct xradio_common *hw_priv = seq->private; */
	u32 time_int = xradio_show_intv(&last_showtime);
	u32 txrx_time = data_tx.totaltime + data_rx.totaltime;
	u32 real_tx = (data_tx.totalsize<<3)/107*10;
	u32 real_rx = (data_rx.totalsize<<3)/106*10;
	if (bh_others.totaltime > bh_wait.totaltime)
		txrx_time += (bh_others.totaltime - bh_wait.totaltime);
	else
		txrx_time += sdio_reg.totaltime;


#define PUT_PERF_INFO(tab, name) \
	seq_printf(seq, "%s" tab "%d\t%d\t%d\t%d\t%d\t%d\t%d.%d\t%d.%02d%%\n", #name, \
	name.count, name.totaltime, (name.totalsize>>3), name.min_time, name.max_time, \
	name.count ? (name.totaltime/name.count) : 0, \
	name.totaltime ? ((name.totalsize<<3)/name.totaltime) : 0, \
	name.totaltime ? (((name.totalsize<<3)%name.totaltime)*10/name.totaltime) : 0, \
	time_int ? (name.totaltime*10/time_int) : 0, \
	time_int ? (((name.totaltime*10)%time_int)*100/time_int) : 0)

	seq_printf(seq, "\nTime_int=%d, Tx=%d.%02dMbps, Rx=%d.%02dMbps, "
		"RealTx=%d.%02dMbps, RealRx=%d.%02dMbps\n", time_int*10,
		txrx_time ? ((data_tx.totalsize<<3)/txrx_time) : 0,
		txrx_time ? ((data_tx.totalsize<<3)%txrx_time)*100/txrx_time : 0,
		txrx_time ? ((data_rx.totalsize<<3)/txrx_time) : 0,
		txrx_time ? ((data_rx.totalsize<<3)%txrx_time)*100/txrx_time : 0,
		time_int ? (real_tx/time_int) : 0,
		time_int ? (real_tx%time_int)*100/time_int : 0,
		time_int ? (real_rx/time_int) : 0,
		time_int ? (real_rx%time_int)*100/time_int : 0);
	seq_printf(seq, "Items\t\tcnt\ttime\tsize(8)\tmin\tmax\tavg\tperf(M)\tduty\n");
	PUT_PERF_INFO("\t\t", data_tx);
	PUT_PERF_INFO("\t\t", data_rx);
	PUT_PERF_INFO("\t\t", bh_wait);
	PUT_PERF_INFO("\t",   sdio_reg);
	PUT_PERF_INFO("\t",   prepare_tx);
	PUT_PERF_INFO("\t",   sdio_write);
	PUT_PERF_INFO("\t",   dev_wake);
	PUT_PERF_INFO("\t",   prepare_rx);
	PUT_PERF_INFO("\t",   sdio_read);
	PUT_PERF_INFO("\t",   handle_rx);
#ifdef BH_PROC_THREAD
	PUT_PERF_INFO("\t",   get_item);
	PUT_PERF_INFO("\t",   wake_proc);
	PUT_PERF_INFO("\t\t", proc_rx);
	PUT_PERF_INFO("\t\t", proc_tx);
	PUT_PERF_INFO("\t",   proc_wait);
#endif
	PUT_PERF_INFO("\t\t", ind_rx);
	PUT_PERF_INFO("\t\t", mac_rx);
	PUT_PERF_INFO("\t\t", mac_tx);
	PUT_PERF_INFO("\t",   bh_others);

#if TEST_GETTIME
	PUT_PERF_INFO("\t", get_time);
#endif

	seq_printf(seq, "reg_cnt1=%d, reg_cnt2=%d, reg_cnt3=%d, reg_cnt4=%d, "
		"reg_fail1=%d, reg_fail2=%d\n",
		sdio_reg_cnt1, sdio_reg_cnt2, sdio_reg_cnt3, sdio_reg_cnt4,
		sdio_reg_cnt5, sdio_reg_cnt6);
	seq_printf(seq, "limit_cnt1=%d, limit_cnt2=%d, "
		"limit_cnt3=%d, limit_cnt4=%d, limit_cnt5=%d, limit_cnt6=%d\n",
		tx_limit_cnt1, tx_limit_cnt2, tx_limit_cnt3,
		tx_limit_cnt4, tx_limit_cnt5, tx_limit_cnt6);

#if BH_PROC_DPA
	seq_printf(seq, "proc_dpa_cnt=%d, proc_up_cnt=%d, proc_down_cnt=%d, prio=%d\n",
			proc_dpa_cnt, proc_up_cnt, proc_down_cnt, hw_priv->proc.proc_prio);
	proc_dpa_cnt = 0;
	proc_up_cnt  = 0;
	proc_down_cnt = 0;
#endif

	sdio_reg_cnt1 = 0;
	sdio_reg_cnt2 = 0;
	sdio_reg_cnt3 = 0;
	sdio_reg_cnt4 = 0;
	sdio_reg_cnt5 = 0;
	sdio_reg_cnt6 = 0;

	tx_limit_cnt1 = 0;
	tx_limit_cnt2 = 0;
	tx_limit_cnt3 = 0;
	tx_limit_cnt4 = 0;
	tx_limit_cnt5 = 0;
	tx_limit_cnt6 = 0;


#undef PUT_PERF_INFO

	perf_info_reset(&data_tx);
	perf_info_reset(&data_rx);
	perf_info_reset(&bh_wait);
	perf_info_reset(&sdio_reg);
	perf_info_reset(&prepare_tx);
	perf_info_reset(&sdio_write);
	perf_info_reset(&dev_wake);
	perf_info_reset(&prepare_rx);
	perf_info_reset(&sdio_read);
	perf_info_reset(&handle_rx);
#ifdef BH_PROC_THREAD
	perf_info_reset(&get_item);
	perf_info_reset(&wake_proc);
	perf_info_reset(&proc_rx);
	perf_info_reset(&proc_tx);
	perf_info_reset(&proc_wait);
#endif
	perf_info_reset(&ind_rx);
	perf_info_reset(&mac_rx);
	perf_info_reset(&mac_tx);
	perf_info_reset(&bh_others);

#if TEST_GETTIME
	if (get_time.count > 10000)
		perf_info_reset(&get_time);
#endif

	return 0;
}

static int xradio_perf_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_perf_info, inode->i_private);
}

static const struct file_operations fops_perf_info = {
	.open = xradio_perf_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};
#endif /* #if PERF_INFO_TEST */


u32 dbg_txconfirm[32];
static int xradio_txconfirm_show(struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < 8; i++) {
		seq_printf(seq, "Txcfm%d:%d\t", i+1, dbg_txconfirm[i]);
		seq_printf(seq, "Txcfm%d:%d\t", i+1+8, dbg_txconfirm[i+8]);
		seq_printf(seq, "Txcfm%d:%d\t", i+1+16, dbg_txconfirm[i+16]);
		seq_printf(seq, "Txcfm%d:%d\t\n", i+1+24, dbg_txconfirm[i+24]);
		dbg_txconfirm[i]    = 0;
		dbg_txconfirm[i+8]  = 0;
		dbg_txconfirm[i+16] = 0;
		dbg_txconfirm[i+24] = 0;
	}
	return 0;
}

static int xradio_txconfirm_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_txconfirm_show,
		inode->i_private);
}

static const struct file_operations fops_txconfirm = {
	.open = xradio_txconfirm_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

u32 dbg_tx_delay_min = 0xffffffff;
u32 dbg_tx_delay_max;
u32 dbg_tx_media_delay[16];
void xradio_debug_tx_delay(u32 media_delay, u32 queue_delay)
{
	if (media_delay < dbg_tx_delay_min)
		dbg_tx_delay_min = media_delay;
	if (media_delay > dbg_tx_delay_max)
		dbg_tx_delay_max = media_delay;
	if (media_delay <= 500)
		dbg_tx_media_delay[0]++;
	else if (media_delay <= 1000)
		dbg_tx_media_delay[1]++;
	else if (media_delay <= 10000)
		dbg_tx_media_delay[2]++;
	else if (media_delay <= 20000)
		dbg_tx_media_delay[3]++;
	else if (media_delay <= 30000)
		dbg_tx_media_delay[4]++;
	else if (media_delay <= 40000)
		dbg_tx_media_delay[5]++;
	else if (media_delay <= 50000)
		dbg_tx_media_delay[6]++;
	else if (media_delay <= 100000)
		dbg_tx_media_delay[7]++;
	else if (media_delay <= 150000)
		dbg_tx_media_delay[8]++;
	else if (media_delay <= 200000)
		dbg_tx_media_delay[9]++;
	else if (media_delay <= 300000)
		dbg_tx_media_delay[10]++;
	else if (media_delay <= 500000)
		dbg_tx_media_delay[11]++;
	else if (media_delay <= 1000000)
		dbg_tx_media_delay[12]++;
	else if (media_delay <= 3000000)
		dbg_tx_media_delay[13]++;
	else if (media_delay <= 15000000)
		dbg_tx_media_delay[14]++;
	else
		xradio_dbg(XRADIO_DBG_WARN, "media_delay=%d, queue_delay=%d\n",
		   media_delay, queue_delay);
	if (media_delay >= 30000)
		xradio_dbg(XRADIO_DBG_NIY, "media_delay=%d, queue_delay=%d\n",
		   media_delay, queue_delay);
}
static int xradio_tx_delay_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "MinDelay %dus, MaxDelay=%dus\n", dbg_tx_delay_min, dbg_tx_delay_max);
	seq_printf(seq, "Delay (0~0.5ms]   :%d\n", dbg_tx_media_delay[0]);
	seq_printf(seq, "Delay (0.5~1ms]   :%d\n", dbg_tx_media_delay[1]);
	seq_printf(seq, "Delay (1~10ms]    :%d\n", dbg_tx_media_delay[2]);
	seq_printf(seq, "Delay (10~20ms]   :%d\n", dbg_tx_media_delay[3]);
	seq_printf(seq, "Delay (20~30ms]   :%d\n", dbg_tx_media_delay[4]);
	seq_printf(seq, "Delay (30~40ms]   :%d\n", dbg_tx_media_delay[5]);
	seq_printf(seq, "Delay (40~50ms]   :%d\n", dbg_tx_media_delay[6]);
	seq_printf(seq, "Delay (50~100ms]  :%d\n", dbg_tx_media_delay[7]);
	seq_printf(seq, "Delay (100~150ms] :%d\n", dbg_tx_media_delay[8]);
	seq_printf(seq, "Delay (150~200ms] :%d\n", dbg_tx_media_delay[9]);
	seq_printf(seq, "Delay (200~300ms] :%d\n", dbg_tx_media_delay[10]);
	seq_printf(seq, "Delay (300~500ms] :%d\n", dbg_tx_media_delay[11]);
	seq_printf(seq, "Delay (500~1000ms]:%d\n", dbg_tx_media_delay[12]);
	seq_printf(seq, "Delay (1~3s]      :%d\n", dbg_tx_media_delay[13]);
	seq_printf(seq, "Delay (3~15s]     :%d\n", dbg_tx_media_delay[14]);

	dbg_tx_delay_min = 0xffffffff;
	dbg_tx_delay_max = 0;
	memset(dbg_tx_media_delay, 0, sizeof(dbg_tx_media_delay));
	return 0;
}

static int xradio_tx_delay_open(struct inode *inode, struct file *file)
{
	return single_open(file, &xradio_tx_delay_show,
		inode->i_private);
}

static const struct file_operations fops_txdelay = {
	.open = xradio_tx_delay_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*for disable low power mode.*/
extern u16 txparse_flags;
extern u16 rxparse_flags;
static ssize_t xradio_parse_flags_get(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/*struct xradio_common *hw_priv = file->private_data;*/
	char buf[100];
	size_t size = 0;

	sprintf(buf, "txparse=0x%04x, rxparse=0x%04x\n",
		txparse_flags, rxparse_flags);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_parse_flags_set(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	/*struct xradio_common *hw_priv = file->private_data;*/
	char buf[30] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	count = (count > 29 ? 29 : count);

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	txparse_flags = simple_strtoul(buf, &endptr, 16);
	start = endptr + 1;
	if (start < buf + count)
		rxparse_flags = simple_strtoul(start, &endptr, 16);

	txparse_flags &= 0x7fff;
	rxparse_flags &= 0x7fff;

	xradio_dbg(XRADIO_DBG_ALWY, "txparse=0x%04x, rxparse=0x%04x\n",
		   txparse_flags, rxparse_flags);
	return count;
}

static const struct file_operations fops_parse_flags = {
	.open = xradio_generic_open,
	.write = xradio_parse_flags_set,
	.read = xradio_parse_flags_get,
	.llseek = default_llseek,
};

#if (DGB_XRADIO_HWT)
u8 hwt_testing;
/*HIF TX test*/
u8 hwt_tx_en;
u8 hwt_tx_cfm;	/*confirm interval*/
u16 hwt_tx_len;
u16 hwt_tx_num;
struct timeval hwt_start_time = { 0 };
struct timeval hwt_end_time = { 0 };

int wsm_hwt_cmd(struct xradio_common *hw_priv, void *arg,
		size_t arg_size);

static ssize_t xradio_hwt_hif_tx(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	if (hwt_testing) {
		xradio_dbg(XRADIO_DBG_ALWY, "cmd refuse, hwt is testing!\n");
		return count;
	}

	count = (count > 99 ? 99 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (simple_strtoul(start, &endptr, 10)) {
		start = endptr + 1;
		if (start < buf + count)
			hwt_tx_len = simple_strtoul(start, &endptr, 10);
		start = endptr + 1;
		if (start < buf + count)
			hwt_tx_num = simple_strtoul(start, &endptr, 10);
		start = endptr + 1;
		if (start < buf + count)
			hwt_tx_cfm = simple_strtoul(start, &endptr, 10);
		hwt_tx_en = 1;
		hwt_testing = 1;
	} else {
		hwt_tx_en = 0;
	}
	xradio_dbg(XRADIO_DBG_ALWY,
		   "hwt_tx_en=%d, hwt_tx_len=%d, hwt_tx_num=%d, hwt_tx_cfm=%d\n",
		   hwt_tx_en, hwt_tx_len, hwt_tx_num, hwt_tx_cfm);

	if (!hw_priv->bh_error &&
		  atomic_add_return(1, &hw_priv->bh_tx) == 1)
		wake_up(&hw_priv->bh_wq);
	return count;
}

static const struct file_operations fops_hwt_hif_tx = {
	.open = xradio_generic_open,
	.write = xradio_hwt_hif_tx,
	.llseek = default_llseek,
};

/*HIF RX test*/
u8 hwt_rx_en;
u16 hwt_rx_len;
u16 hwt_rx_num;
static ssize_t xradio_hwt_hif_rx(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	if (hwt_testing) {
		xradio_dbg(XRADIO_DBG_ALWY, "cmd refuse, hwt is testing!\n");
		return count;
	}

	count = (count > 99 ? 99 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (simple_strtoul(start, &endptr, 10)) {
		start = endptr + 1;
		if (start < buf + count)
			hwt_rx_len = simple_strtoul(start, &endptr, 10);
		start = endptr + 1;
		if (start < buf + count)
			hwt_rx_num = simple_strtoul(start, &endptr, 10);

		hwt_rx_en = 1;
	} else {
		hwt_rx_en = 0;
	}
	xradio_dbg(XRADIO_DBG_ALWY,
		   "hwt_rx_en=%d, hwt_rx_len=%d, hwt_rx_num=%d\n", hwt_rx_en,
		   hwt_rx_len, hwt_rx_num);

	/*check the parameters.*/
	if (hwt_rx_len < 100 || hwt_rx_len > 1500)
		hwt_rx_len = 1500;
	if (hwt_rx_en && hwt_rx_num) {
		HWT_PARAMETERS hwt_hdr = {
			.TestID = 0x0002,
			.Params = hwt_rx_num,
			.Data = hwt_rx_len
		};
		hwt_testing = 1;
		wsm_hwt_cmd(hw_priv, (void *)&hwt_hdr.TestID, sizeof(hwt_hdr)-4);
		xr_do_gettimeofday(&hwt_start_time);
	}

	return count;
}

static const struct file_operations fops_hwt_hif_rx = {
	.open = xradio_generic_open,
	.write = xradio_hwt_hif_rx,
	.llseek = default_llseek,
};

/*ENC test*/
u8 hwt_enc_type;
u8 hwt_key_len;
u16 hwt_enc_len;
u16 hwt_enc_cnt;
static ssize_t xradio_hwt_enc(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	if (hwt_testing) {
		xradio_dbg(XRADIO_DBG_ALWY, "cmd refuse, hwt is testing!\n");
		return count;
	}

	count = (count > 99 ? 99 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	hwt_enc_type = simple_strtoul(start, &endptr, 10);
	start = endptr + 1;
	if (start < buf + count)
		hwt_key_len = simple_strtoul(start, &endptr, 10);
	start = endptr + 1;
	if (start < buf + count)
		hwt_enc_len = simple_strtoul(start, &endptr, 10);
	start = endptr + 1;
	if (start < buf + count)
		hwt_enc_cnt = simple_strtoul(start, &endptr, 10);

	xradio_dbg(XRADIO_DBG_ALWY,
		   "enc_type=%d, key_len=%d, enc_len=%d, enc_cnt=%d\n",
		   hwt_enc_type, hwt_key_len, hwt_enc_len, hwt_enc_cnt);

	/*check the parameters.*/
	if (hwt_enc_type < 10 && hwt_key_len <= 16 &&
		hwt_enc_len <= 1500 && hwt_enc_cnt > 0) {
		HWT_PARAMETERS hwt_hdr = {
			.TestID  = 0x0003,
			.Params  = (hwt_key_len<<8) | hwt_enc_type,
			.Datalen = hwt_enc_len,
			.Data    = hwt_enc_cnt
		};
		hwt_testing = 1;
		wsm_hwt_cmd(hw_priv, (void *)&hwt_hdr.TestID, sizeof(hwt_hdr)-4);
	}

	return count;
}

static const struct file_operations fops_hwt_enc = {
	.open = xradio_generic_open,
	.write = xradio_hwt_enc,
	.llseek = default_llseek,
};

/*MIC test*/
u16 hwt_mic_len;
u16 hwt_mic_cnt;
static ssize_t xradio_hwt_mic(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	if (hwt_testing) {
		xradio_dbg(XRADIO_DBG_ALWY, "cmd refuse, hwt is testing!\n");
		return count;
	}

	count = (count > 99 ? 99 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	hwt_mic_len = simple_strtoul(start, &endptr, 10);
	start = endptr + 1;
	if (start < buf + count)
		hwt_mic_cnt = simple_strtoul(start, &endptr, 10);

	xradio_dbg(XRADIO_DBG_ALWY, "mic_len=%d, mic_cnt=%d\n",
		   hwt_mic_len, hwt_mic_cnt);

	/*check the parameters.*/
	if (hwt_mic_len <= 1500 && hwt_mic_cnt > 0) {
		HWT_PARAMETERS hwt_hdr = {
			.TestID = 0x0004,
			.Params = 0,
			.Datalen = hwt_mic_len,
			.Data = hwt_mic_cnt
		};
		hwt_testing = 1;
		wsm_hwt_cmd(hw_priv, (void *)&hwt_hdr.TestID, sizeof(hwt_hdr)-4);
	}

	return count;
}

static const struct file_operations fops_hwt_mic = {
	.open = xradio_generic_open,
	.write = xradio_hwt_mic,
	.llseek = default_llseek,
};

static ssize_t xradio_hwt_hif_rx_burn(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[100] = { 0 };
	char *start = &buf[0];
	char *endptr = NULL;

	u16 hw_inf_cmd = 0;

	HWT_PARAMETERS hwt_hdr = {
		.TestID = 0x0005,
		.Params = hw_inf_cmd,
		.Data = 10000
	};


	count = (count > 99 ? 99 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;


	hw_inf_cmd = simple_strtoul(start, &endptr, 10);

	xradio_dbg(XRADIO_DBG_ALWY, "hw_inf_cmd=%d\n", hw_inf_cmd);

	hwt_hdr.Params = hw_inf_cmd;

	wsm_hwt_cmd(hw_priv, (void *)&hwt_hdr.TestID, sizeof(hwt_hdr)-4);

	return count;
}

static const struct file_operations fops_hwt_hif_rx_burn = {
	.open = xradio_generic_open,
	.write = xradio_hwt_hif_rx_burn,
	.llseek = default_llseek,
};

#endif /*DGB_XRADIO_HWT*/

static u32 measure_type;

static ssize_t xradio_measure_type_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[12] = { 0 };
	char *endptr = NULL;
	count = (count > 11 ? 11 : count);
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	measure_type = simple_strtoul(buf, &endptr, 16);

	xradio_dbg(XRADIO_DBG_ALWY, "measure_type = %08x\n", measure_type);
	SYS_WARN(wsm_11k_measure_requset(hw_priv, (measure_type & 0xff),
					 ((measure_type & 0xff00) >> 8),
					 ((measure_type & 0xffff0000) >> 16)));
	return count;
}

static ssize_t xradio_measure_type_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/*struct xradio_common *hw_priv = file->private_data;*/
	char buf[20];
	size_t size = 0;

	sprintf(buf, "measure_type = %u\n", measure_type);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static const struct file_operations fops_11k = {
	.open = xradio_generic_open,
	.write = xradio_measure_type_write,
	.read = xradio_measure_type_read,
	.llseek = default_llseek,
};

static ssize_t xradio_wsm_dumps(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[1];

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, 1))
		return -EFAULT;

	if (buf[0] == '1')
		hw_priv->wsm_enable_wsm_dumps = 1;
	else
		hw_priv->wsm_enable_wsm_dumps = 0;

	return count;
}

static const struct file_operations fops_wsm_dumps = {
	.open = xradio_generic_open,
	.write = xradio_wsm_dumps,
	.llseek = default_llseek,
};

static ssize_t xradio_short_dump_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *hw_priv = file->private_data;
	char buf[20];
	size_t size = 0;

	sprintf(buf, "Size: %u\n", hw_priv->wsm_dump_max_size);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_short_dump_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_common *priv = file->private_data;
	char buf[20];
	unsigned long dump_size = 0;

	if (!count || count > 20)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (kstrtoul(buf, 10, &dump_size))
		return -EINVAL;
	xradio_dbg(XRADIO_DBG_ALWY, "%s get %lu\n", __func__, dump_size);

	priv->wsm_dump_max_size = dump_size;

	return count;
}

static const struct file_operations fops_short_dump = {
	.open = xradio_generic_open,
	.write = xradio_short_dump_write,
	.read = xradio_short_dump_read,
	.llseek = default_llseek,
};

#ifdef SUPPORT_HT40

static int xradio_status_show_priv(struct seq_file *seq, void *v)
{
	int i;
	struct xradio_vif *priv = seq->private;
	struct xradio_debug_priv *d = priv->debug;

	seq_printf(seq, "Mode:       %s%s\n",
		xradio_debug_mode(priv->mode),
		priv->listening ? " (listening)" : "");
	seq_printf(seq, "Assoc:      %s\n",
		xradio_debug_join_status[priv->join_status]);
	if (priv->rx_filter.promiscuous)
		seq_puts(seq, "Filter:     promisc\n");
	else if (priv->rx_filter.fcs)
		seq_puts(seq, "Filter:     fcs\n");
	if (priv->rx_filter.bssid)
		seq_puts(seq, "Filter:     bssid\n");
	if (priv->bf_control.bcn_count)
		seq_puts(seq, "Filter:     beacons\n");

	if (priv->enable_beacon ||
	    priv->mode == NL80211_IFTYPE_AP ||
	    priv->mode == NL80211_IFTYPE_ADHOC ||
	    priv->mode == NL80211_IFTYPE_MESH_POINT ||
	    priv->mode == NL80211_IFTYPE_P2P_GO)
		seq_printf(seq, "Beaconing:  %s\n",
			   priv->enable_beacon ? "enabled" : "disabled");
	if (priv->ssid_length ||
	    priv->mode == NL80211_IFTYPE_AP ||
	    priv->mode == NL80211_IFTYPE_ADHOC ||
	    priv->mode == NL80211_IFTYPE_MESH_POINT ||
	    priv->mode == NL80211_IFTYPE_P2P_GO)
		seq_printf(seq, "SSID:       %.*s\n",
			   (int)priv->ssid_length, priv->ssid);

	for (i = 0; i < 4; ++i) {
		seq_printf(seq, "EDCA(%d):    %d, %d, %d, %d, %d\n", i,
			   priv->edca.params[i].cwMin,
			   priv->edca.params[i].cwMax,
			   priv->edca.params[i].aifns,
			   priv->edca.params[i].txOpLimit,
			   priv->edca.params[i].maxReceiveLifetime);
	}
	if (priv->join_status == XRADIO_JOIN_STATUS_STA) {
		static const char *pmMode = "unknown";
		switch (priv->powersave_mode.pmMode) {
		case WSM_PSM_ACTIVE:
			pmMode = "off";
			break;
		case WSM_PSM_PS:
			pmMode = "on";
			break;
		case WSM_PSM_FAST_PS:
			pmMode = "dynamic";
			break;
		}
		seq_printf(seq, "Preamble:   %s\n",
			xradio_debug_preamble[
			priv->association_mode.PhyModeCfg.PreambleCfg]);
		seq_printf(seq, "AMPDU spcn: %d\n",
			priv->association_mode.mpduStartSpacing);
		seq_printf(seq, "Basic rate: 0x%.8X\n",
			le32_to_cpu(priv->association_mode.basicRateSet));
		seq_printf(seq, "Bss lost:   %d beacons\n",
			priv->bss_params.beaconLostCount);
		seq_printf(seq, "AID:        %d\n",
			priv->bss_params.aid);
		seq_printf(seq, "Rates:      0x%.8X\n",
			priv->bss_params.operationalRateSet);
		seq_printf(seq, "Powersave:  %s\n", pmMode);
	}
	seq_printf(seq, "RSSI thold: %d\n",
		priv->cqm_rssi_thold);
	seq_printf(seq, "RSSI hyst:  %d\n",
		priv->cqm_rssi_hyst);
	seq_printf(seq, "TXFL thold: %d\n",
		priv->cqm_tx_failure_thold);
	seq_printf(seq, "Linkloss:   %d\n",
		priv->cqm_link_loss_count);
	seq_printf(seq, "Bcnloss:    %d\n",
		priv->cqm_beacon_loss_count);

	xradio_debug_print_map(seq, priv, "Link map:   ",
		priv->link_id_map);
	xradio_debug_print_map(seq, priv, "Asleep map: ",
		priv->sta_asleep_mask);
	xradio_debug_print_map(seq, priv, "PSPOLL map: ",
		priv->pspoll_mask);

	seq_puts(seq, "\n");

	for (i = 0; i < MAX_STA_IN_AP_MODE; ++i) {
		if (priv->link_id_db[i].status) {
			seq_printf(seq, "Link %d:     %s, %pM\n",
				i + 1, xradio_debug_link_id[
				priv->link_id_db[i].status],
				priv->link_id_db[i].mac);
		}
	}

	seq_puts(seq, "\n");

	seq_printf(seq, "Powermgmt:  %s\n",
		priv->powersave_enabled ? "on" : "off");

	seq_printf(seq, "TXed:       %d\n",
		d->tx);
	seq_printf(seq, "AGG TXed:   %d\n",
		d->tx_agg);
	seq_printf(seq, "MULTI TXed: %d (%d)\n",
		d->tx_multi, d->tx_multi_frames);
	seq_printf(seq, "RXed:       %d\n",
		d->rx);
	seq_printf(seq, "AGG RXed:   %d\n",
		d->rx_agg);
	seq_printf(seq, "TX align:   %d\n",
		d->tx_align);
	seq_printf(seq, "TX TTL:     %d\n",
		d->tx_ttl);
	return 0;
}

#else

static int xradio_status_show_priv(struct seq_file *seq, void *v)
{
	int i;
	struct xradio_vif *priv = seq->private;
	struct xradio_debug_priv *d = priv->debug;

	seq_printf(seq, "Mode:       %s%s\n",
		xradio_debug_mode(priv->mode),
		priv->listening ? " (listening)" : "");
	seq_printf(seq, "Assoc:      %s\n",
		xradio_debug_join_status[priv->join_status]);
	if (priv->rx_filter.promiscuous)
		seq_puts(seq, "Filter:     promisc\n");
	else if (priv->rx_filter.fcs)
		seq_puts(seq, "Filter:     fcs\n");
	if (priv->rx_filter.bssid)
		seq_puts(seq, "Filter:     bssid\n");
	if (priv->bf_control.bcn_count)
		seq_puts(seq, "Filter:     beacons\n");

	if (priv->enable_beacon ||
	    priv->mode == NL80211_IFTYPE_AP ||
	    priv->mode == NL80211_IFTYPE_ADHOC ||
	    priv->mode == NL80211_IFTYPE_MESH_POINT ||
	    priv->mode == NL80211_IFTYPE_P2P_GO)
		seq_printf(seq, "Beaconing:  %s\n",
			   priv->enable_beacon ? "enabled" : "disabled");
	if (priv->ssid_length ||
	    priv->mode == NL80211_IFTYPE_AP ||
	    priv->mode == NL80211_IFTYPE_ADHOC ||
	    priv->mode == NL80211_IFTYPE_MESH_POINT ||
	    priv->mode == NL80211_IFTYPE_P2P_GO)
		seq_printf(seq, "SSID:       %.*s\n",
			   (int)priv->ssid_length, priv->ssid);

	for (i = 0; i < 4; ++i) {
		seq_printf(seq, "EDCA(%d):    %d, %d, %d, %d, %d\n", i,
			   priv->edca.params[i].cwMin,
			   priv->edca.params[i].cwMax,
			   priv->edca.params[i].aifns,
			   priv->edca.params[i].txOpLimit,
			   priv->edca.params[i].maxReceiveLifetime);
	}
	if (priv->join_status == XRADIO_JOIN_STATUS_STA) {
		static const char *pmMode = "unknown";
		switch (priv->powersave_mode.pmMode) {
		case WSM_PSM_ACTIVE:
			pmMode = "off";
			break;
		case WSM_PSM_PS:
			pmMode = "on";
			break;
		case WSM_PSM_FAST_PS:
			pmMode = "dynamic";
			break;
		}
		seq_printf(seq, "Preamble:   %s\n",
			xradio_debug_preamble[
			priv->association_mode.preambleType]);
		seq_printf(seq, "AMPDU spcn: %d\n",
			   priv->association_mode.mpduStartSpacing);
		seq_printf(seq, "Basic rate: 0x%.8X\n",
			   le32_to_cpu(priv->association_mode.basicRateSet));
		seq_printf(seq, "Bss lost:   %d beacons\n",
			   priv->bss_params.beaconLostCount);
		seq_printf(seq, "AID:        %d\n", priv->bss_params.aid);
		seq_printf(seq, "Rates:      0x%.8X\n",
			   priv->bss_params.operationalRateSet);
		seq_printf(seq, "Powersave:  %s\n", pmMode);
	}
	seq_printf(seq, "RSSI thold: %d\n", priv->cqm_rssi_thold);
	seq_printf(seq, "RSSI hyst:  %d\n", priv->cqm_rssi_hyst);
	seq_printf(seq, "TXFL thold: %d\n", priv->cqm_tx_failure_thold);
	seq_printf(seq, "Linkloss:   %d\n", priv->cqm_link_loss_count);
	seq_printf(seq, "Bcnloss:    %d\n", priv->cqm_beacon_loss_count);

	xradio_debug_print_map(seq, priv, "Link map:   ", priv->link_id_map);
	xradio_debug_print_map(seq, priv, "Asleep map: ",
			       priv->sta_asleep_mask);
	xradio_debug_print_map(seq, priv, "PSPOLL map: ", priv->pspoll_mask);

	seq_puts(seq, "\n");

	for (i = 0; i < MAX_STA_IN_AP_MODE; ++i) {
		if (priv->link_id_db[i].status) {
			seq_printf(seq, "Link %d:     %s, %pM\n",
				i + 1, xradio_debug_link_id[
				priv->link_id_db[i].status],
				priv->link_id_db[i].mac);
		}
	}

	seq_puts(seq, "\n");

	seq_printf(seq, "Powermgmt:  %s\n",
		priv->powersave_enabled ? "on" : "off");

	seq_printf(seq, "TXed:       %d\n", d->tx);
	seq_printf(seq, "AGG TXed:   %d\n", d->tx_agg);
	seq_printf(seq, "MULTI TXed: %d (%d)\n",
		   d->tx_multi, d->tx_multi_frames);
	seq_printf(seq, "RXed:       %d\n", d->rx);
	seq_printf(seq, "AGG RXed:   %d\n", d->rx_agg);
	seq_printf(seq, "TX align:   %d\n", d->tx_align);
	seq_printf(seq, "TX TTL:     %d\n", d->tx_ttl);
	return 0;
}

#endif

static int xradio_status_open_priv(struct inode *inode,
			struct file *file)
{
	return single_open(file, &xradio_status_show_priv, inode->i_private);
}

static const struct file_operations fops_status_priv = {
	.open = xradio_status_open_priv,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

#if defined(CONFIG_XRADIO_USE_EXTENSIONS)

static ssize_t xradio_hang_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_vif *priv = file->private_data;
	struct xradio_common *hw_priv = xrwl_vifpriv_to_hwpriv(priv);
	char buf[1];

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, 1))
		return -EFAULT;

	if (priv->vif) {
#ifdef CONFIG_PM
		xradio_pm_stay_awake(&hw_priv->pm_state, 3 * HZ);
#endif

		wsm_send_deauth_to_self(hw_priv, priv);

		/* ieee80211_driver_hang_notify(priv->vif, GFP_KERNEL); */
	} else
		return -ENODEV;

	return count;
}

static const struct file_operations fops_hang = {
	.open = xradio_generic_open,
	.write = xradio_hang_write,
	.llseek = default_llseek,
};
#endif

#ifdef AP_HT_COMPAT_FIX
extern u8 ap_compat_bssid[ETH_ALEN];
static ssize_t xradio_ht_compat_show(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_vif *priv = file->private_data;
	char buf[100];
	size_t size = 0;
	sprintf(buf, "ht_compat_det=0x%x, BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n",
		priv->ht_compat_det,
		ap_compat_bssid[0], ap_compat_bssid[1],
		ap_compat_bssid[2], ap_compat_bssid[3],
		ap_compat_bssid[4], ap_compat_bssid[5]);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
}

static ssize_t xradio_ht_compat_disalbe(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct xradio_vif *priv = file->private_data;
	char buf[2];
	char *endptr = NULL;

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, 2))
		return -EFAULT;

	if (simple_strtoul(buf, &endptr, 10))
		priv->ht_compat_det |= 0x10;
	else
		priv->ht_compat_det &= ~0x10;
	return count;
}

static const struct file_operations fops_ht_compat_dis = {
	.open = xradio_generic_open,
	.read = xradio_ht_compat_show,
	.write = xradio_ht_compat_disalbe,
	.llseek = default_llseek,
};
#endif

#define VIF_DEBUGFS_NAME_S 10
int xradio_debug_init_priv(struct xradio_common *hw_priv,
			   struct xradio_vif *priv)
{
	int ret = -ENOMEM;
	struct xradio_debug_priv *d;
	char name[VIF_DEBUGFS_NAME_S];
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);

	if (SYS_WARN(!hw_priv))
		return ret;

	if (SYS_WARN(!hw_priv->debug))
		return ret;

	d = xr_kzalloc(sizeof(struct xradio_debug_priv), false);
	priv->debug = d;
	if (SYS_WARN(!d))
		return ret;

	memset(name, 0, VIF_DEBUGFS_NAME_S);
	ret = snprintf(name, VIF_DEBUGFS_NAME_S, "vif_%d", priv->if_id);
	if (SYS_WARN(ret < 0))
		goto err;

	d->debugfs_phy = debugfs_create_dir(name,
					    hw_priv->debug->debugfs_phy);
	if (SYS_WARN(!d->debugfs_phy))
		goto err;

#if defined(CONFIG_XRADIO_USE_EXTENSIONS)
	if (SYS_WARN(!debugfs_create_file("hang", S_IWUSR, d->debugfs_phy,
			priv, &fops_hang)))
		goto err;
#endif

#if defined(AP_HT_COMPAT_FIX)
	if (SYS_WARN(!debugfs_create_file("htcompat_disable",
			 S_IWUSR, d->debugfs_phy, priv, &fops_ht_compat_dis)))
		goto err;
#endif

	if (!debugfs_create_file("status", S_IRUSR, d->debugfs_phy,
			priv, &fops_status_priv))
		goto err;

	return 0;
err:
	priv->debug = NULL;
	debugfs_remove_recursive(d->debugfs_phy);
	kfree(d);
	return ret;

}

void xradio_debug_release_priv(struct xradio_vif *priv)
{
	struct xradio_debug_priv *d = priv->debug;
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);
	if (d) {
		priv->debug = NULL;
		debugfs_remove_recursive(d->debugfs_phy);
		kfree(d);
	}
}

int xradio_print_fw_version(struct xradio_common *hw_priv,
				u8 *buf, size_t len)
{
	return snprintf(buf, len, "%s %d.%d",
			xradio_debug_fw_types[hw_priv->wsm_caps.firmwareType],
			hw_priv->wsm_caps.firmwareVersion,
			hw_priv->wsm_caps.firmwareBuildNumber);
}
#if (SUPPORT_EPTA)
static const struct file_operations fops_epta_stat_dbg_ctrl = {
	.open = xradio_generic_open,
	.write = xradio_epta_stat_dbg_write,
	.read = xradio_epta_stat_dbg_read,
	.llseek = default_llseek,
};
#endif

#if (DBG_XRADIO_HIF)

static ssize_t xradio_hif_test_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[50] = {0};
	char *start = &buf[0];
	char *endptr = NULL;

	count = (count > 50 ? 50 : count);

	printk(KERN_ERR"set value\n");
	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	hif_test_rw = simple_strtoul(start, &endptr, 16);
	start = endptr + 1;

	if (start < buf + count)
		hif_test_data_mode = simple_strtoul(start, &endptr, 16);

	start = endptr + 1;
	if (start < buf + count)
		hif_test_data_len = simple_strtoul(start, &endptr, 16);

	start = endptr + 1;
	if (start < buf + count)
		hif_test_data_round = simple_strtoul(start, &endptr, 16);

	start = endptr + 1;
	if (start < buf + count)
		hif_test_oper_delta = simple_strtoul(start, &endptr, 16);

	xradio_dbg(XRADIO_DBG_ALWY, "[HIF test] configuration %s, val is 0x%x, "
		"round is %u, data size is %u per round delay %u ms per round\n",
		hif_test_rw ? (hif_test_rw > 1 ? "R & W" : "W") : "Not Test",
		hif_test_data_round,
		hif_test_data_mode,
		hif_test_data_len*4,
		hif_test_oper_delta);
	return count;
}

static ssize_t xradio_hif_test_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	/* struct xradio_common *hw_priv = file->private_data;
	char buf[50];
	size_t size = 0;

	sprintf(buf, "fwdbg_ctrl = %u\n", fwdbg_ctrl);
	size = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, size);
	 */
	return 0;
}
static const struct file_operations fops_hif_test = {
	.open = xradio_generic_open,
	.write = xradio_hif_test_write,
	.read = xradio_hif_test_read,
	.llseek = default_llseek,
};

#endif

#ifdef ERROR_HANG_DRIVER
void xradio_hang_driver_for_debug(struct xradio_common *hw_priv, int error)
{
	int cnt = 0;
	if (!error_hang_driver || !error)
		return ; /*do nothing.*/

	if (!hw_priv->debug) { /*have not setup debugfs yet, just do it.*/
		xradio_debug_init_common(hw_priv);
	}
	while (error_hang_driver) {
		msleep(5000);
		cnt++;
		xradio_dbg(XRADIO_DBG_ALWY, "err=%d, hang driver for %ds.\n",
			error, cnt*5);
	}
	xradio_debug_release_common(hw_priv);
	xradio_dbg(XRADIO_DBG_ALWY, "%s exit.\n", __func__);
}
#endif

/*for host debuglevel*/
struct dentry *debugfs_host;
#if (DGB_XRADIO_QC)
struct dentry *debugfs_hwinfo;
#endif
int xradio_host_dbg_init(void)
{
	int line = 0;
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);

	if (!debugfs_initialized()) {
		xradio_dbg(XRADIO_DBG_ERROR, "debugfs isnot initialized\n");
		return 0;
	}
#define ERR_LINE  do { line = __LINE__; goto err; } while (0)

	debugfs_host = debugfs_create_dir("xradio_host_dbg", NULL);
	if (!debugfs_host)
		ERR_LINE;

	if (!debugfs_create_x8("dbg_common", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_common))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_sbus", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_sbus))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_ap", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_ap))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_sta", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_sta))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_scan", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_scan))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_bh", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_bh))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_txrx", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_txrx))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_wsm", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_wsm))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_pm", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_pm))
		ERR_LINE;

#ifdef CONFIG_XRADIO_ITP
	if (!debugfs_create_x8("dbg_itp", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_itp))
		ERR_LINE;
#endif

#ifdef CONFIG_XRADIO_ETF
	if (!debugfs_create_x8("dbg_etf", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_etf))
		ERR_LINE;
#endif

	if (!debugfs_create_x8("dbg_logfile", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_logfile))
		ERR_LINE;

	if (!debugfs_create_x8("dbg_tpa_node", S_IRUSR | S_IWUSR,
				   debugfs_host, &tpa_node_dbg))
		ERR_LINE;

	if (!debugfs_create_u32("set_sdio_clk", S_IRUSR | S_IWUSR,
				   debugfs_host, &dbg_sdio_clk))
		ERR_LINE;

	if (!debugfs_create_u32("tx_burst_limit", S_IRUSR | S_IWUSR,
				   debugfs_host, &tx_burst_limit))
		ERR_LINE;

#ifdef ERROR_HANG_DRIVER
	if (!debugfs_create_u8("error_hang_driver", S_IRUSR | S_IWUSR,
				   debugfs_host, &error_hang_driver))
		ERR_LINE;
#endif

	return 0;

#undef ERR_LINE
err:
	xradio_dbg(XRADIO_DBG_ERROR, "xradio_host_dbg_init failed=%d\n", line);
	if (debugfs_host)
		debugfs_remove_recursive(debugfs_host);
#if (DGB_XRADIO_QC)
	debugfs_hwinfo = NULL;
#endif
	debugfs_host = NULL;
	return 0;
}

void xradio_host_dbg_deinit(void)
{
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);
	if (debugfs_host)
		debugfs_remove_recursive(debugfs_host);
#if (DGB_XRADIO_QC)
	debugfs_hwinfo = NULL;
#endif
	debugfs_host = NULL;
}

int xradio_debug_init_common(struct xradio_common *hw_priv)
{
	int ret = -ENOMEM;
	int line = 0;
	struct xradio_debug_common *d = NULL;
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);

	/*init some debug variables here.*/
	retry_dbg    = 0;
	tpa_node_dbg = 0;

#define ERR_LINE  do { line = __LINE__; goto err; } while (0)

	d = xr_kzalloc(sizeof(struct xradio_debug_common), false);
	hw_priv->debug = d;
	if (!d) {
		xradio_dbg(XRADIO_DBG_ERROR, "%s, xr_kzalloc failed!\n", __func__);
		return ret;
	}

#ifdef ERROR_HANG_DRIVER
	d->debugfs_phy = debugfs_create_dir("xradio", NULL);
#else
	d->debugfs_phy = debugfs_create_dir("xradio",
			    hw_priv->hw->wiphy->debugfsdir);
#endif

	if (!d->debugfs_phy)
		ERR_LINE;

	if (!debugfs_create_file("version", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_version))
		ERR_LINE;

	if (!debugfs_create_file("status", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_status_common))
		ERR_LINE;

	if (!debugfs_create_file("counters", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_counters))
		ERR_LINE;

	if (!debugfs_create_file("backoff", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_backoff))
		ERR_LINE;

	if (!debugfs_create_file("txpipe", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_txpipe))
		ERR_LINE;

	if (!debugfs_create_file("ampdu", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_ampducounters))
		ERR_LINE;

	if (!debugfs_create_file("ratemap", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_ratemap))
		ERR_LINE;

	if (!debugfs_create_file("dbgstats", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_dbgstats))
		ERR_LINE;

	if (!debugfs_create_file("11n", S_IRUSR | S_IWUSR,
			d->debugfs_phy, hw_priv, &fops_11n))
		ERR_LINE;

	if (!debugfs_create_file("wsm_dumps", S_IWUSR, d->debugfs_phy,
			hw_priv, &fops_wsm_dumps))
		ERR_LINE;

	if (!debugfs_create_file("set_fwdbg", S_IRUSR | S_IWUSR, d->debugfs_phy,
			hw_priv, &fops_fwdbg))
		ERR_LINE;

	if (!debugfs_create_file("rw_fwreg", S_IWUSR, d->debugfs_phy, hw_priv,
		  &fops_rw_fwreg))
		ERR_LINE;

	if (!debugfs_create_file("rw_fwreg_direct", S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_rw_fwreg_direct))
		ERR_LINE;

	if (!debugfs_create_file("set_ampdu_len", S_IRUSR | S_IWUSR,
		  d->debugfs_phy, hw_priv, &fops_ampdu_len))
		ERR_LINE;

	if (!debugfs_create_file("set_rts_threshold", S_IRUSR | S_IWUSR,
		  d->debugfs_phy, hw_priv, &fops_rts_threshold))
		ERR_LINE;

	if (!debugfs_create_file("low_pwr_disable", S_IRUSR | S_IWUSR,
		  d->debugfs_phy, hw_priv, &fops_low_pwr))
		ERR_LINE;

	if (!debugfs_create_file("ps_disable", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_ps_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("retry_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_retry_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("rates_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_rates_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("backoff_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_backoff_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("tala_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_tala_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("tx_pwr_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_tx_pwr_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("tpa_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_tpa_ctrl))
		ERR_LINE;

	if (!debugfs_create_file("tpa_debug", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_tpa_debug))
		ERR_LINE;

	if (!debugfs_create_file("policy_info", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_policy_info))
		ERR_LINE;

	if (!debugfs_create_file("bh_stat", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_bh_stat))
		ERR_LINE;

#if PERF_INFO_TEST
	if (!debugfs_create_file("perf_info", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_perf_info))
		ERR_LINE;
#endif

	if (!debugfs_create_file("txconfirm", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_txconfirm))
		ERR_LINE;

	if (!debugfs_create_file("tx_delay", S_IRUSR, d->debugfs_phy,
		  hw_priv, &fops_txdelay))
		ERR_LINE;

	if (!debugfs_create_file("parse_flags", S_IRUSR | S_IWUSR,
		  d->debugfs_phy, hw_priv, &fops_parse_flags))
		ERR_LINE;

	if (!debugfs_create_file("set_measure_type", S_IRUSR | S_IWUSR,
		 d->debugfs_phy, hw_priv, &fops_11k))
		ERR_LINE;

	if (!debugfs_create_file("wsm_dump_size", S_IRUSR | S_IWUSR,
		d->debugfs_phy, hw_priv, &fops_short_dump))
		ERR_LINE;

#if (SUPPORT_EPTA)
	if (!debugfs_create_file("epta_stat", S_IRUSR, d->debugfs_phy,
			hw_priv, &fops_epta_stat))
		ERR_LINE;

	if (!debugfs_create_file("epta_stat_ctrl", S_IRUSR | S_IWUSR, d->debugfs_phy,
			hw_priv, &fops_epta_stat_dbg_ctrl))
		ERR_LINE;
#endif

#if (DGB_XRADIO_HWT)
	/*hardware test*/
	if (!debugfs_create_file("hwt_hif_tx", S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_hwt_hif_tx))
		ERR_LINE;

	if (!debugfs_create_file("hwt_hif_rx", S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_hwt_hif_rx))
		ERR_LINE;

	if (!debugfs_create_file("hwt_enc", S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_hwt_enc))
		ERR_LINE;

	if (!debugfs_create_file("hwt_mic", S_IWUSR, d->debugfs_phy,
		  hw_priv, &fops_hwt_mic))
		ERR_LINE;

	if (!debugfs_create_file("hwt_hif_rx_burn", S_IWUSR, d->debugfs_phy,
		hw_priv, &fops_hwt_hif_rx_burn))
	ERR_LINE;

#endif /*DGB_XRADIO_HWT*/

#if (DGB_XRADIO_QC)
	/*for QC apk read.*/
	if (debugfs_host && !debugfs_hwinfo) {
		debugfs_hwinfo = debugfs_create_file("hwinfo", S_IRUSR | S_IWUSR,
				debugfs_host, hw_priv, &fops_hwinfo);
		if (!debugfs_hwinfo)
			ERR_LINE;
	}

	if (!debugfs_create_file("temperature", S_IRUSR, d->debugfs_phy,
		hw_priv, &fops_temperature))
	ERR_LINE;
#endif
	ret = xradio_itp_init(hw_priv);
	if (ret)
		ERR_LINE;

#ifdef SUPPORT_FW_DBG_INF
	xradio_fw_dbg_init(hw_priv);
#endif

	return 0;

#undef ERR_LINE

err:
	xradio_dbg(XRADIO_DBG_ERROR,
		   "xradio_debug_init_common failed=%d\n", line);
	hw_priv->debug = NULL;
	debugfs_remove_recursive(d->debugfs_phy);
	kfree(d);
	return ret;
}

void xradio_debug_release_common(struct xradio_common *hw_priv)
{
	struct xradio_debug_common *d = hw_priv->debug;
	xradio_dbg(XRADIO_DBG_TRC, "%s\n", __func__);

#if (DGB_XRADIO_QC)
	if (debugfs_hwinfo) {
		debugfs_remove(debugfs_hwinfo);
		debugfs_hwinfo = NULL;
	}
#endif
	if (d) {
#ifdef SUPPORT_FW_DBG_INF
		xradio_fw_dbg_deinit();
#endif
		xradio_itp_release(hw_priv);
		hw_priv->debug = NULL;

#ifdef ERROR_HANG_DRIVER
		debugfs_remove_recursive(d->debugfs_phy);
#else
		/* removed by mac80211, don't remove it again,
		 * fixed wifi on/off.*/
		/*
		 debugfs_remove_recursive(d->debugfs_phy);
		 */
#endif
		kfree(d);
	}
}

#define FRAME_TYPE(xx) ieee80211_is_ ## xx(fctl)
#define FT_MSG_PUT(f, ...) do { \
	if (flags&f)	\
		frame_msg += sprintf(frame_msg, __VA_ARGS__); \
	} while (0)

#define PT_MSG_PUT(f, ...) do { \
	if (flags&f)	\
		proto_msg += sprintf(proto_msg, __VA_ARGS__); \
	} while (0)

#define FRAME_PARSE(f, name) do { \
	if (FRAME_TYPE(name)) { \
		FT_MSG_PUT(f, "%s", #name); goto outprint; } \
	} while (0)

#define IS_FRAME_PRINT (frame_msg != (char *)&framebuf[0])
#define IS_PROTO_PRINT (proto_msg != (char *)&protobuf[0])


char framebuf[512] = { 0 };
char protobuf[512] = { 0 };

char *p2p_frame_type[] = {
	"GO Negotiation Request",
	"GO Negotiation Response",
	"GO Negotiation Confirmation",
	"P2P Invitation Request",
	"P2P Invitation Response",
	"Device Discoverability Request",
	"Device Discoverability Response",
	"Provision Discovery Request",
	"Provision Discovery Response",
	"Reserved"
};

void xradio_parse_frame(u8 *mac_data, u8 iv_len, u16 flags, u8 if_id)
{
	char *frame_msg = &framebuf[0];
	char *proto_msg = &protobuf[0];
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *)mac_data;
	u16 fctl = frame->frame_control;

	memset(frame_msg, 0, sizeof(framebuf));
	memset(proto_msg, 0, sizeof(protobuf));

	if (ieee80211_is_data(fctl)) {
		u8 machdrlen = ieee80211_hdrlen(fctl);
		u8 *llc_data = mac_data + machdrlen + iv_len;

		if (ieee80211_is_qos_nullfunc(fctl) ||
		    ieee80211_is_data_qos(fctl))
			FT_MSG_PUT(PF_DATA, "QoS");
		if (ieee80211_is_nullfunc(fctl)) {
			FT_MSG_PUT(PF_DATA, "NULL(ps=%d)", !!(fctl&IEEE80211_FCTL_PM));
			goto outprint;
		}
		FT_MSG_PUT(PF_DATA, "data(TDFD=%d%d, R=%d, P=%d)",
			   !!(fctl & IEEE80211_FCTL_TODS),
			   !!(fctl & IEEE80211_FCTL_FROMDS),
			   !!(fctl & IEEE80211_FCTL_RETRY),
			   !!(fctl & IEEE80211_FCTL_PROTECTED));

		if (is_SNAP(llc_data)) {
			if (is_ip(llc_data)) {
				u8 *ip_hdr = llc_data + LLC_LEN;
				u8 *ipaddr_s = ip_hdr + IP_S_ADD_OFF;
				u8 *ipaddr_d = ip_hdr + IP_D_ADD_OFF;
				u8 *proto_hdr = ip_hdr + ((ip_hdr[0] & 0xf) << 2);	/*ihl:words*/

				if (is_tcp(llc_data)) {
					PT_MSG_PUT(PF_TCP,
						   "TCP%s%s, src=%d, dest=%d, seq=0x%08x, ack=0x%08x",
					    (proto_hdr[13]&0x01) ? "(S)" : "",
					    (proto_hdr[13]&0x02) ? "(F)" : "",
					    (proto_hdr[0]<<8)  | proto_hdr[1],
					    (proto_hdr[2]<<8)  | proto_hdr[3],
					    (proto_hdr[4]<<24) | (proto_hdr[5]<<16) |
					    (proto_hdr[6]<<8)  | proto_hdr[7],
					    (proto_hdr[8]<<24) | (proto_hdr[9]<<16) |
					    (proto_hdr[10]<<8) | proto_hdr[11]);

				} else if (is_udp(llc_data)) {
					if (is_dhcp(llc_data)) {
						u8 Options_len = BOOTP_OPS_LEN;
						u32 dhcp_magic  = cpu_to_be32(DHCP_MAGIC);
						u8 *dhcphdr = proto_hdr + UDP_LEN+UDP_BOOTP_LEN;
						while (Options_len) {
							if (*(u32 *)dhcphdr == dhcp_magic)
								break;
							dhcphdr++;
							Options_len--;
						}
						PT_MSG_PUT(PF_DHCP, "DHCP, Opt=%d, MsgType=%d",
							   *(dhcphdr+4), *(dhcphdr+6));
					} else {
						PT_MSG_PUT(PF_UDP, "UDP, source=%d, dest=%d",
							  (proto_hdr[0]<<8) | proto_hdr[1],
							  (proto_hdr[2]<<8) | proto_hdr[3]);
					}
				} else if (is_icmp(llc_data)) {
					PT_MSG_PUT(PF_ICMP, "ICMP%s%s, Seq=%d",
						   (8 == proto_hdr[0]) ? "(ping)"  : "",
						   (0 == proto_hdr[0]) ? "(reply)" : "",
							(proto_hdr[6]<<8) | proto_hdr[7]);
				} else if (is_igmp(llc_data)) {
					PT_MSG_PUT(PF_UNKNWN, "IGMP, type=0x%x", proto_hdr[0]);
				} else {
					PT_MSG_PUT(PF_UNKNWN, "unknown IP type=%d",
						   *(ip_hdr + IP_PROTO_OFF));
				}
				if (IS_PROTO_PRINT) {
					PT_MSG_PUT(PF_IPADDR, "-%d.%d.%d.%d(s)", \
						   ipaddr_s[0], ipaddr_s[1],
						   ipaddr_s[2], ipaddr_s[3]);
					PT_MSG_PUT(PF_IPADDR, "-%d.%d.%d.%d(d)", \
						   ipaddr_d[0], ipaddr_d[1],
						   ipaddr_d[2], ipaddr_d[3]);
				}

			} else if (is_8021x(llc_data)) {
				PT_MSG_PUT(PF_8021X, "8021X");
			} else {	/*other protol, no detail.*/
				switch (cpu_to_be16(*(u16 *)(llc_data+LLC_TYPE_OFF))) {
				case ETH_P_IPV6:	/*0x08dd*/
					PT_MSG_PUT(PF_UNKNWN, "IPv6");
					break;
				case ETH_P_ARP:	/*0x0806*/
					PT_MSG_PUT(PF_UNKNWN, "ARP");
					break;
				case ETH_P_RARP:	/*0x8035*/
					PT_MSG_PUT(PF_UNKNWN, "RARP");
					break;
				case ETH_P_DNA_RC:	/*0x6002*/
					PT_MSG_PUT(PF_UNKNWN, "DNA Remote Console");
					break;
				case ETH_P_DNA_RT:	/*0x6003*/
					PT_MSG_PUT(PF_UNKNWN, "DNA Routing");
					break;
				case ETH_P_8021Q:	/*0x8100*/
					PT_MSG_PUT(PF_UNKNWN, "802.1Q VLAN");
					break;
				case ETH_P_LINK_CTL:	/*0x886c*/
					PT_MSG_PUT(PF_UNKNWN, "wlan link local tunnel(HPNA)");
					break;
				case ETH_P_PPP_DISC:	/*0x8863*/
					PT_MSG_PUT(PF_UNKNWN, "PPPoE discovery");
					break;
				case ETH_P_PPP_SES:	/*0x8864*/
					PT_MSG_PUT(PF_UNKNWN, "PPPoE session");
					break;
				case ETH_P_MPLS_UC:	/*0x8847*/
					PT_MSG_PUT(PF_UNKNWN, "MPLS Unicast");
					break;
				case ETH_P_MPLS_MC:	/*0x8848*/
					PT_MSG_PUT(PF_UNKNWN, "MPLS Multicast");
					break;
				default:
					PT_MSG_PUT(PF_UNKNWN, "unknown Ethernet type=0x%04x",
						   cpu_to_be16(*(u16 *)(llc_data+LLC_TYPE_OFF)));
					break;
				}
			}
		} else if (is_STP(llc_data)) {
			/*spanning tree proto.*/
			PT_MSG_PUT(PF_UNKNWN, "spanning tree");
		} else {
			PT_MSG_PUT(PF_UNKNWN, "unknown LLC type=0x%08x, 0x%08x",
				   *(u32 *)(llc_data), *((u32 *)(llc_data)+1));
		}

	} else if (ieee80211_is_mgmt(fctl) && (PF_MGMT & flags)) {

		FRAME_PARSE(PF_MGMT, auth);
		FRAME_PARSE(PF_MGMT, deauth);
		FRAME_PARSE(PF_MGMT, assoc_req);
		FRAME_PARSE(PF_MGMT, assoc_resp);
		FRAME_PARSE(PF_MGMT, reassoc_req);
		FRAME_PARSE(PF_MGMT, reassoc_resp);
		FRAME_PARSE(PF_MGMT, disassoc);
		FRAME_PARSE(PF_MGMT, atim);

		/*for more information about action frames.*/
		if (FRAME_TYPE(action)) {
			u8 *encrypt_frame = NULL;
			struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)frame;
			if (frame->frame_control & __cpu_to_le32(IEEE80211_FCTL_PROTECTED) && (flags & PF_RX)) {
				encrypt_frame = (u8 *)frame + IEEE80211_CCMP_256_HDR_LEN;
				mgmt = (struct ieee80211_mgmt *)encrypt_frame;
			}
			FT_MSG_PUT(PF_MGMT, "%s", "action");

			if (mgmt->u.action.category == WLAN_CATEGORY_PUBLIC) {
				u8 *action = (u8 *) &mgmt->u.action.category;
				u32 oui = *(u32 *) &action[2];
				u8 oui_subtype = action[6] > 8 ? 9 : action[6];
				if (action[1] == 0x09 && oui == 0x099A6F50)
					FT_MSG_PUT(PF_MGMT, "(%s)", p2p_frame_type[oui_subtype]);
			} else if (mgmt->u.action.category == WLAN_CATEGORY_BACK &&
				mgmt->u.action.u.addba_req.action_code ==
				WLAN_ACTION_ADDBA_REQ) {
				FT_MSG_PUT(PF_MGMT, "(ADDBA_REQ-%d)",
					   mgmt->u.action.u.addba_req.start_seq_num);
			} else if (mgmt->u.action.category == WLAN_CATEGORY_BACK &&
				mgmt->u.action.u.addba_req.action_code ==
				WLAN_ACTION_ADDBA_RESP) {
				FT_MSG_PUT(PF_MGMT, "(ADDBA_RESP-%d)",
					   mgmt->u.action.u.addba_resp.status);
			} else if (mgmt->u.action.category == WLAN_CATEGORY_SA_QUERY &&
				mgmt->u.action.u.sa_query.action ==
				WLAN_ACTION_SA_QUERY_REQUEST) {
				FT_MSG_PUT(PF_MGMT, "(SA_Query_Req)");
			} else if (mgmt->u.action.category == WLAN_CATEGORY_SA_QUERY &&
				mgmt->u.action.u.sa_query.action ==
				WLAN_ACTION_SA_QUERY_RESPONSE) {
				FT_MSG_PUT(PF_MGMT, "(SA_Query_Resp)");
			} else {
				FT_MSG_PUT(PF_MGMT, "(%d)", mgmt->u.action.category);
			}
			goto outprint;
		}
		/*too much scan results, don't print if no need.*/
		FRAME_PARSE(PF_SCAN, probe_req);
		FRAME_PARSE(PF_SCAN, probe_resp);
		FRAME_PARSE(PF_SCAN, beacon);
		/*must be last.*/
		FT_MSG_PUT(PF_UNKNWN, "unknown mgmt");

	} else if (ieee80211_is_ctl(fctl) && (PF_CTRL & flags)) {

		flags &= (~PF_MAC_SN);	/*no seq ctrl in ctrl frames.*/
		FRAME_PARSE(PF_CTRL, back);
		FRAME_PARSE(PF_CTRL, back_req);
		FRAME_PARSE(PF_CTRL, ack);
		FRAME_PARSE(PF_CTRL, rts);
		FRAME_PARSE(PF_CTRL, cts);
		FRAME_PARSE(PF_CTRL, pspoll);
		/*must be last.*/
		FT_MSG_PUT(PF_UNKNWN, "unknown ctrl");
	} else {
		FT_MSG_PUT(PF_UNKNWN, "unknown mac frame, fctl=0x%04x\n", fctl);
	}

outprint:

	FT_MSG_PUT(PF_MAC_SN, "-SN=%d(%d)",
		   (frame->seq_ctrl>>4), (frame->seq_ctrl&0xf));

	/*output all msg.*/
	if (IS_FRAME_PRINT || IS_PROTO_PRINT) {
		u8 *related = NULL;
		u8 *own = NULL;
		char *r_type = NULL;
		char *o_type = NULL;
		u8 machdrlen = ieee80211_hdrlen(fctl);
		u8 *sa = ieee80211_get_SA(frame);
		u8 *da = ieee80211_get_DA(frame);

		if (flags & PF_RX) {
			related = frame->addr2;
			own = frame->addr1;
			r_type = "TA";
			o_type = "RA";
		} else {
			related = frame->addr1;
			own = frame->addr2;
			r_type = "RA";
			o_type = "TA";
		}

		if (machdrlen >= 16) {	/*if ACK or BA, don't print.*/
			FT_MSG_PUT(PF_MACADDR, "-%02x:%02x:%02x:%02x:%02x:%02x(%s)",
				   related[0], related[1], related[2],
				   related[3], related[4], related[5],
				   r_type);
			FT_MSG_PUT(PF_OWNMAC, "-%02x:%02x:%02x:%02x:%02x:%02x(%s)",
				   own[0], own[1], own[2], own[3], own[4], own[5],
				   o_type);
			FT_MSG_PUT(PF_SA_DA, "-%02x:%02x:%02x:%02x:%02x:%02x(DA)",
				   da[0], da[1], da[2], da[3], da[4], da[5]);
			FT_MSG_PUT(PF_SA_DA, "-%02x:%02x:%02x:%02x:%02x:%02x(SA)",
				   sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);
		}

		xradio_dbg(XRADIO_DBG_ALWY, "if%d-%s%s--%s\n", if_id,
			   (PF_RX & flags) ? "RX-" : "TX-", framebuf, protobuf);
	}
}

#undef FT_MSG_PUT
#undef PT_MSG_PUT
#undef FRAME_PARSE
#undef FRAME_TYPE

#if DGB_LOG_FILE
u8 log_buffer[DGB_LOG_BUF_LEN];
u16 log_pos;
struct file *fp_log;
atomic_t file_ref = { 0 };

#define T_LABEL_LEN  32
char last_time_label[T_LABEL_LEN] = { 0 };

int xradio_logfile(char *buffer, int buf_len, u8 b_time)
{
	int ret = -1;
	int size = buf_len;
	mm_segment_t old_fs = get_fs();

	if (!buffer)
		return ret;

	if (buf_len < 0)
		size = strlen(buffer);
	if (!size)
		return ret;

	if (atomic_add_return(1, &file_ref) == 1) {
		fp_log = filp_open(DGB_LOG_PATH0, O_CREAT | O_WRONLY, 0666);
		if (IS_ERR(fp_log)) {
			printk(KERN_ERR "[XRADIO] ERR, can't open %s(%d).\n",
			       DGB_LOG_PATH0, (int)fp_log);
			goto exit;
		}
	}
	/*printk(KERN_ERR "[XRADIO] file_ref=%d\n", atomic_read(&file_ref));*/

	if (fp_log->f_op->write == NULL) {
		printk(KERN_ERR "[XRADIO] ERR, %s:File is not allow to write!\n",
		       __func__);
		goto exit;
	} else {
		set_fs(KERNEL_DS);
		if (fp_log->f_op->llseek != NULL) {
			vfs_llseek(fp_log, 0, SEEK_END);
		} else {
			fp_log->f_pos = 0;
		}
		set_fs(old_fs);
		if (b_time) {
			struct timeval time_now = { 0 };
			struct rtc_time tm;
			char time_label[T_LABEL_LEN] = { 0 };
			xr_do_gettimeofday(&time_now);
			time_now.tv_sec -= sys_tz.tz_minuteswest * 60;
			rtc_time_to_tm(time_now.tv_sec, &tm);
			sprintf(time_label, "\n%d-%02d-%02d_%02d-%02d-%02d\n",
				tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			if (memcmp(last_time_label, time_label, T_LABEL_LEN)) {
				memcpy(last_time_label, time_label, T_LABEL_LEN);
				ret = kernel_write(fp_log, time_label, strlen(time_label),
						&fp_log->f_pos);
			}
		}
		ret = kernel_write(fp_log, buffer, size, &fp_log->f_pos);
	}

exit:
	if (atomic_read(&file_ref) == 1) {
		if (!IS_ERR(fp_log)) {
			filp_close(fp_log, NULL);
			fp_log = (struct file *)-ENOENT;
		}
	}
	atomic_sub(1, &file_ref);
	return ret;
}
#endif

#if (DGB_XRADIO_HWT)
/***************************for HWT********************************/
struct sk_buff *hwt_skb;
int sent_num;
int get_hwt_hif_tx(struct xradio_common *hw_priv, u8 **data,
		   size_t *tx_len, int *burst, int *vif_selected)
{

	HWT_PARAMETERS *hwt_tx_hdr = NULL;
	if (!hwt_tx_en || !hwt_tx_len || !hwt_tx_num ||
		  sent_num >= hwt_tx_num) {
		if (hwt_skb) {
			dev_kfree_skb(hwt_skb);
			hwt_skb = NULL;
		}
		return 0;
	}

	if (!hwt_skb) {
		hwt_skb = xr_alloc_skb(1504);
		if (!hwt_skb) {
			xradio_dbg(XRADIO_DBG_ERROR, "%s:skb is NULL!\n", __func__);
			return 0;
		}
		if ((unsigned long)hwt_skb->data & 3) {
			u8 align = 4 - ((unsigned long)hwt_skb->data & 3);
			skb_reserve(hwt_skb, align);
		}
		skb_put(hwt_skb, 1500);
	}
	/*fill the header info*/
	if (hwt_tx_len < sizeof(HWT_PARAMETERS))
		hwt_tx_len = sizeof(HWT_PARAMETERS);
	if (hwt_tx_len > 1500)
		hwt_tx_len = 1500;
	hwt_tx_hdr = (HWT_PARAMETERS *)hwt_skb->data;
	hwt_tx_hdr->MsgID = 0x0004;
	hwt_tx_hdr->Msglen = hwt_tx_len;
	hwt_tx_hdr->TestID = 0x0001;
	hwt_tx_hdr->Data = 0x1234;

	/*send the packet*/
	*data = hwt_skb->data;
	*tx_len = hwt_tx_hdr->Msglen;
	*vif_selected = 0;
	*burst = 2;		/*burst > 1 for continuous tx.*/
	sent_num++;

	/*first packet.*/
	if (sent_num == 1) {
		xr_do_gettimeofday(&hwt_start_time);
	}
	/*set confirm*/
	hwt_tx_hdr->Params = 0;
	if (sent_num >= hwt_tx_num) {
		hwt_tx_hdr->Params = 0x101;	/*last packet*/
		hwt_tx_en = 0;	/*disable hwt_tx_en*/
		xradio_dbg(XRADIO_DBG_ALWY, "%s:sent last packet!\n", __func__);
	} else if (hwt_tx_cfm) {
		hwt_tx_hdr->Params = !(sent_num % hwt_tx_cfm);
	}

	return 1;
}
#endif

#endif /* CONFIG_XRADIO_DEBUGFS */
