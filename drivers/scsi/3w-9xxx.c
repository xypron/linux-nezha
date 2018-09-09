// SPDX-License-Identifier: GPL-2.0
/*
   3w-9xxx.c -- 3ware 9000 Storage Controller Driver for Linux.

   Written By: Adam Radford <aradford@gmail.com>
   Modifications By: Tom Couch
   Modifications By: Saumel Holland <samuel@sholland.org>

   Copyright (C) 2004-2009 Applied Micro Circuits Corporation.
   Copyright (C) 2010 LSI Corporation.

   Bugs/Comments/Suggestions should be mailed to:
   aradford@gmail.com
*/

#define TW_DRIVER_NAME "3w-9xxx"
#define pr_fmt(fmt) TW_DRIVER_NAME ": " fmt

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "3w-9xxx.h"

#define twa_err(d, ...) shost_printk(KERN_ERR, (d)->host, __VA_ARGS__)
#define twa_warn(d, ...) shost_printk(KERN_WARNING, (d)->host, __VA_ARGS__)
#define twa_notice(d, ...) shost_printk(KERN_NOTICE, (d)->host, __VA_ARGS__)
#define twa_info(d, ...) shost_printk(KERN_INFO, (d)->host, __VA_ARGS__)
#define twa_dbg(d, ...) shost_printk(KERN_DEBUG, (d)->host, __VA_ARGS__)

/*******************************
 * Globals
 */

static struct class *twa_class;
static unsigned int twa_major;
static DECLARE_BITMAP(twa_minor, TW_MAX_MINORS);

/*******************************
 * Module parameters
 */

static int use_msi = 0;
module_param(use_msi, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_msi, "Use message signaled interrupts (default = 0)");

/*******************************
 * Prototypes
 */

static int twa_init_controller(struct twa_device *twa_dev, bool reset,
			       int timeout);
static void twa_shutdown(struct pci_dev *pdev);

/*******************************
 * Messages
 */

static char *twa_aen_severity_table[] = {
	"ERROR", "WARNING", "INFO", "DEBUG"
};

static char *twa_get_aen_severity(unsigned int code)
{
	if (code < TW_AEN_SEVERITY_ERROR || code > TW_AEN_SEVERITY_DEBUG)
		return NULL;

	return twa_aen_severity_table[code - TW_AEN_SEVERITY_ERROR];
}

static struct twa_message twa_aen_table[] = {
	{0x0000, "AEN queue empty"},
	{0x0001, "Controller reset occurred"},
	{0x0002, "Degraded unit detected"},
	{0x0003, "Controller error occurred"},
	{0x0004, "Background rebuild failed"},
	{0x0005, "Background rebuild done"},
	{0x0006, "Incomplete unit detected"},
	{0x0007, "Background initialize done"},
	{0x0008, "Unclean shutdown detected"},
	{0x0009, "Drive timeout detected"},
	{0x000A, "Drive error detected"},
	{0x000B, "Rebuild started"},
	{0x000C, "Background initialize started"},
	{0x000D, "Entire logical unit was deleted"},
	{0x000E, "Background initialize failed"},
	{0x000F, "SMART attribute exceeded threshold"},
	{0x0010, "Power supply reported AC under range"},
	{0x0011, "Power supply reported DC out of range"},
	{0x0012, "Power supply reported a malfunction"},
	{0x0013, "Power supply predicted malfunction"},
	{0x0014, "Battery charge is below threshold"},
	{0x0015, "Fan speed is below threshold"},
	{0x0016, "Temperature sensor is above threshold"},
	{0x0017, "Power supply was removed"},
	{0x0018, "Power supply was inserted"},
	{0x0019, "Drive was removed from a bay"},
	{0x001A, "Drive was inserted into a bay"},
	{0x001B, "Drive bay cover door was opened"},
	{0x001C, "Drive bay cover door was closed"},
	{0x001D, "Product case was opened"},
	{0x0020, "Prepare for shutdown (power-off)"},
	{0x0021, "Downgrade UDMA mode to lower speed"},
	{0x0022, "Upgrade UDMA mode to higher speed"},
	{0x0023, "Sector repair completed"},
	{0x0024, "Sbuf memory test failed"},
	{0x0025, "Error flushing cached write data to array"},
	{0x0026, "Drive reported data ECC error"},
	{0x0027, "DCB has checksum error"},
	{0x0028, "DCB version is unsupported"},
	{0x0029, "Background verify started"},
	{0x002A, "Background verify failed"},
	{0x002B, "Background verify done"},
	{0x002C, "Bad sector overwritten during rebuild"},
	{0x002D, "Background rebuild error on source drive"},
	{0x002E, "Replace failed because replacement drive too small"},
	{0x002F, "Verify failed because array was never initialized"},
	{0x0030, "Unsupported ATA drive"},
	{0x0031, "Synchronize host/controller time"},
	{0x0032, "Spare capacity is inadequate for some units"},
	{0x0033, "Background migration started"},
	{0x0034, "Background migration failed"},
	{0x0035, "Background migration done"},
	{0x0036, "Verify detected and fixed data/parity mismatch"},
	{0x0037, "SO-DIMM incompatible"},
	{0x0038, "SO-DIMM not detected"},
	{0x0039, "Corrected Sbuf ECC error"},
	{0x003A, "Drive power on reset detected"},
	{0x003B, "Background rebuild paused"},
	{0x003C, "Background initialize paused"},
	{0x003D, "Background verify paused"},
	{0x003E, "Background migration paused"},
	{0x003F, "Corrupt flash file system detected"},
	{0x0040, "Flash file system repaired"},
	{0x0041, "Unit number assignments were lost"},
	{0x0042, "Error during read of primary DCB"},
	{0x0043, "Latent error found in backup DCB"},
	{0x00FC, "Recovered/finished array membership update"},
	{0x00FD, "Handler lockup"},
	{0x00FE, "Retrying PCI transfer"},
	{0x00FF, "AEN queue is full"},
	{0xFFFF, NULL}
};

static struct twa_message twa_error_table[] = {
	{0x0100, "SGL entry contains zero data"},
	{0x0101, "Invalid command opcode"},
	{0x0102, "SGL entry has unaligned address"},
	{0x0103, "SGL size does not match command"},
	{0x0104, "SGL entry has illegal length"},
	{0x0105, "Command packet is not aligned"},
	{0x0106, "Invalid request ID"},
	{0x0107, "Duplicate request ID"},
	{0x0108, "ID not locked"},
	{0x0109, "LBA out of range"},
	{0x010A, "Logical unit not supported"},
	{0x010B, "Parameter table does not exist"},
	{0x010C, "Parameter index does not exist"},
	{0x010D, "Invalid field in CDB"},
	{0x010E, "Specified port has invalid drive"},
	{0x010F, "Parameter item size mismatch"},
	{0x0110, "Failed memory allocation"},
	{0x0111, "Memory request too large"},
	{0x0112, "Out of memory segments"},
	{0x0113, "Invalid address to deallocate"},
	{0x0114, "Out of memory"},
	{0x0115, "Out of heap"},
	{0x0120, "Double degrade"},
	{0x0121, "Drive not degraded"},
	{0x0122, "Reconstruct error"},
	{0x0123, "Replace not accepted"},
	{0x0124, "Replace drive capacity too small"},
	{0x0125, "Sector count not allowed"},
	{0x0126, "No spares left"},
	{0x0127, "Reconstruct error"},
	{0x0128, "Unit is offline"},
	{0x0129, "Cannot update status to DCB"},
	{0x0130, "Invalid stripe handle"},
	{0x0131, "Handle that was not locked"},
	{0x0132, "Handle that was not empty"},
	{0x0133, "Handle has different owner"},
	{0x0140, "IPR has parent"},
	{0x0150, "Illegal Pbuf address alignment"},
	{0x0151, "Illegal Pbuf transfer length"},
	{0x0152, "Illegal Sbuf address alignment"},
	{0x0153, "Illegal Sbuf transfer length"},
	{0x0160, "Command packet too large"},
	{0x0161, "SGL exceeds maximum length"},
	{0x0162, "SGL has too many entries"},
	{0x0170, "Insufficient resources for rebuilder"},
	{0x0171, "Verify error (data != parity)"},
	{0x0180, "Requested segment not in directory of this DCB"},
	{0x0181, "DCB segment has unsupported version"},
	{0x0182, "DCB segment has checksum error"},
	{0x0183, "DCB support (settings) segment invalid"},
	{0x0184, "DCB UDB (unit descriptor block) segment invalid"},
	{0x0185, "DCB GUID (globally unique identifier) segment invalid"},
	{0x01A0, "Could not clear Sbuf"},
	{0x01C0, "Flash identify failed"},
	{0x01C1, "Flash out of bounds"},
	{0x01C2, "Flash verify error"},
	{0x01C3, "Flash file object not found"},
	{0x01C4, "Flash file already present"},
	{0x01C5, "Flash file system full"},
	{0x01C6, "Flash file not present"},
	{0x01C7, "Flash file size error"},
	{0x01C8, "Bad flash file checksum"},
	{0x01CA, "Corrupt flash file system detected"},
	{0x01D0, "Invalid field in parameter list"},
	{0x01D1, "Parameter list length error"},
	{0x01D2, "Parameter item is not changeable"},
	{0x01D3, "Parameter item is not saveable"},
	{0x0200, "UDMA CRC error"},
	{0x0201, "Internal CRC error"},
	{0x0202, "Data ECC error"},
	{0x0203, "ADP level 1 error"},
	{0x0204, "Port timeout"},
	{0x0205, "Drive power on reset"},
	{0x0206, "ADP level 2 error"},
	{0x0207, "Soft reset failed"},
	{0x0208, "Drive not ready"},
	{0x0209, "Unclassified port error"},
	{0x020A, "Drive aborted command"},
	{0x0210, "Internal CRC error"},
	{0x0211, "PCI abort error"},
	{0x0212, "PCI parity error"},
	{0x0213, "Port handler error"},
	{0x0214, "Token interrupt count error"},
	{0x0215, "Timeout waiting for PCI transfer"},
	{0x0216, "Corrected buffer ECC"},
	{0x0217, "Uncorrected buffer ECC"},
	{0x0230, "Unsupported command during flash recovery"},
	{0x0231, "Next image buffer expected"},
	{0x0232, "Binary image architecture incompatible"},
	{0x0233, "Binary image has no signature"},
	{0x0234, "Binary image has bad checksum"},
	{0x0235, "Image downloaded overflowed buffer"},
	{0x0240, "I2C device not found"},
	{0x0241, "I2C transaction aborted"},
	{0x0242, "SO-DIMM parameter(s) incompatible using defaults"},
	{0x0243, "SO-DIMM unsupported"},
	{0x0248, "SPI transfer status error"},
	{0x0249, "SPI transfer timeout error"},
	{0x0250, "Invalid unit descriptor size in CreateUnit"},
	{0x0251, "Unit descriptor size exceeds data buffer in CreateUnit"},
	{0x0252, "Invalid value in CreateUnit descriptor"},
	{0x0253, "Inadequate disk space to support descriptor in CreateUnit"},
	{0x0254, "Unable to create data channel for this unit descriptor"},
	{0x0255, "CreateUnit descriptor specifies a drive already in use"},
	{0x0256, "Unable to write configuration to all disks during CreateUnit"},
	{0x0257, "CreateUnit does not support this descriptor version"},
	{0x0258, "Invalid subunit for RAID 0 or 5 in CreateUnit"},
	{0x0259, "Too many descriptors in CreateUnit"},
	{0x025A, "Invalid configuration specified in CreateUnit descriptor"},
	{0x025B, "Invalid LBA offset specified in CreateUnit descriptor"},
	{0x025C, "Invalid stripelet size specified in CreateUnit descriptor"},
	{0x0260, "SMART attribute exceeded threshold"},
	{0xFFFF, NULL}
};

static char *twa_get_string(struct twa_message *table, unsigned int code)
{
	for (; table->code != code && table->code != 0xFFFF; ++table);
	return table->code == code ? table->text : "Unknown";
}

/*******************************
 * Request and buffer management
 */

/*
 * Determine if we should get a DMA mapping for the scatter-gather list. Use a
 * preallocated buffer instead of a mapped SGL for small, single entry buffers.
 */
static bool twa_command_mapped(struct scsi_cmnd *scmd)
{
	return scsi_sg_count(scmd) > 1 || scsi_bufflen(scmd) > TW_SECTOR_SIZE;
}

static bool twa_is_passthru(struct scsi_cmnd *scmd)
{
	return scmd->cmnd[0] == ATA_12 || scmd->cmnd[0] == ATA_16;
}

/*
 * Find and reserve a request ID, and initialize the request structure.
 *
 * Locking: acquires the lock for this request.
 */
static int twa_begin_request(struct twa_device *twa_dev, struct scsi_cmnd *scmd)
{
	struct twa_request *request;
	int request_id;
	int start = 0;

	do {
		request_id = find_next_bit(twa_dev->free_requests,
					   TW_MAX_REQUESTS, start);
		/* If the bitmap is full, we queued too many requests */
		BUG_ON(request_id == TW_MAX_REQUESTS && start == 0);
		if (request_id == TW_MAX_REQUESTS) {
			start = 0;
			continue;
		}
		start = request_id;
	} while (!test_and_clear_bit(request_id, twa_dev->free_requests));

	request = &twa_dev->requests[request_id];

	/* Since the bit was set in the free request bitmap, this request can
	 * never have been in any state but free */
	atomic_set(&request->state, TW_STATE_STARTED);
	request->scmd  = scmd;

	return request_id;
}

/*
 * Free resources for a completed request.
 *
 * Locking: releases the lock for this request.
 */
static void twa_end_request(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];


	if (request_id == atomic_read(&twa_dev->aen_request_id))
		atomic_set(&twa_dev->aen_request_id, TW_INVALID_REQUEST);
	if (request_id == atomic_read(&twa_dev->ioctl_request_id))
		atomic_set(&twa_dev->ioctl_request_id, TW_INVALID_REQUEST);
	request->scmd  = NULL;
	/* Whatever the state is, make it free */
	atomic_set(&request->state, TW_STATE_FREE);
	set_bit(request_id, twa_dev->free_requests);
}

/*
 * Abort and free resources for an in-progress request.
 *
 * MUST ONLY BE CALLED with the host_lock locked, to prevent starting new
 * requests.
 *
 * Locking: acquires and releases the lock for this request.
 */
static void twa_abort_request(struct twa_device *twa_dev, int request_id,
			      int reason)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	int state;

	/* If the request is being process, spin until it is posted/pended */
	while ((state = atomic_read(&request->state)) == TW_STATE_STARTED);
	/* If the request won't touch the controller, let it finish normally */
	if (state == TW_STATE_FREE || state == TW_STATE_COMPLETED)
		return;
	/* If racing with the ISR, it might complete the request; allow it */
	if (atomic_cmpxchg(&request->state, state, TW_STATE_ABORTED) != state)
		return;

	/* Now the request is in TW_STATE_ABORTED and we have the old state */
	if (state == TW_STATE_PENDING) {
		atomic_dec(&twa_dev->stats.pending_requests);
		clear_bit(request_id, twa_dev->pending_requests);
	}
	if (state == TW_STATE_POSTED)
		atomic_dec(&twa_dev->stats.posted_requests);

	if (request_id == atomic_read(&twa_dev->aen_request_id)) {
		atomic_set(&twa_dev->aen_request_id, TW_INVALID_REQUEST);
	} else if (request_id == atomic_read(&twa_dev->ioctl_request_id)) {
		atomic_set(&twa_dev->ioctl_request_id, TW_INVALID_REQUEST);
		complete(&twa_dev->ioctl_done);
	} else {
		if (twa_command_mapped(request->scmd))
			scsi_dma_unmap(request->scmd);
		request->scmd->result = reason << 16;
		request->scmd->scsi_done(request->scmd);
	}

	twa_end_request(twa_dev, request_id);
}

static void twa_bump_stat(atomic_t *current_stat, atomic_t *max_stat)
{
	int max   = atomic_read(max_stat);
	int prev  = -1;
	int value = atomic_inc_return(current_stat);

	while (value > max && max != prev) {
		prev = max;
		max = atomic_cmpxchg(max_stat, prev, value);
	}
}

static void twa_update_stat(atomic_t *current_stat, atomic_t *max_stat,
			    int value)
{
	int max  = atomic_read(max_stat);
	int prev = -1;

	atomic_set(current_stat, value);
	while (value > max && max != prev) {
		prev = max;
		max = atomic_cmpxchg(max_stat, prev, value);
	}
}

/* This function will check the status register for unexpected bits and print
 * readable messages from status register errors */
static int twa_check_status(struct twa_device *twa_dev, u32 status)
{
	/* Check for various error conditions and handle them appropriately */
	if (status & TW_STATUS_PCI_PARITY_ERROR) {
		twa_warn(twa_dev, "PCI Parity Error: clearing\n");
		writel(TW_CONTROL_CLEAR_PARITY_ERROR,
		       twa_dev->base + TW_CONTROL_REG);
	}

	if (status & TW_STATUS_PCI_ABORT) {
		twa_warn(twa_dev, "PCI Abort: clearing\n");
		writel(TW_CONTROL_CLEAR_PCI_ABORT,
		       twa_dev->base + TW_CONTROL_REG);
		pci_write_config_word(twa_dev->pdev, PCI_STATUS,
				      TW_PCI_CLEAR_PCI_ABORT);
	}

	if (status & TW_STATUS_QUEUE_ERROR) {
		if (((twa_dev->pdev->device != PCI_DEVICE_ID_3WARE_9650SE) &&
		     (twa_dev->pdev->device != PCI_DEVICE_ID_3WARE_9690SA)) ||
		    !test_bit(TW_IN_RESET, &twa_dev->flags))
			twa_warn(twa_dev, "Controller Queue Error: clearing\n");
		writel(TW_CONTROL_CLEAR_QUEUE_ERROR,
		       twa_dev->base + TW_CONTROL_REG);
	}

	if (status & TW_STATUS_MICROCONTROLLER_ERROR) {
		twa_err(twa_dev, "Microcontroller Error: clearing\n");
		return -EIO;
	}

	return 0;
}

/*******************************
 * SCSI callback implementations
 */

/*
 * Initialize a command packet for an internal or SCSI command.
 *
 * Locking: callers must be holding the lock for this request.
 */
static void twa_init_scsi_cmd(struct twa_command_packet *packet, int request_id,
			      u8 unit, u8 lun)
{
	struct twa_command_9xxx *cmd = &packet->command_9xxx;

	packet->header.status.error    = 0;
	packet->header.status.severity = 0;
	packet->header.header_size     = sizeof(struct twa_command_header);

	cmd->opcode          = TW_OP_EXECUTE_SCSI;
	cmd->unit            = unit;
	cmd->request_id__lun = TW_REQ_LUN_IN(request_id, lun);
	cmd->status          = 0;
	cmd->sgl_offset      = TW_MAX_CDB_LENGTH;
	cmd->sgl_entries     = 0;
}

/* This function will attempt to post a command packet to the board */
static int twa_post_command_packet(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	dma_addr_t command = request->packet_dma + TW_COMMAND_OFFSET;
	unsigned long flags;
	u32 status;
	int ret;

	BUG_ON(atomic_read(&request->state) != TW_STATE_STARTED &&
	       atomic_read(&request->state) != TW_STATE_COMPLETED);

	/* Last chance sanity check */
	if (WARN_ON(test_bit(TW_IN_RESET, &twa_dev->flags) &&
		    request->scmd))
		return -EBUSY;

	spin_lock_irqsave(&twa_dev->queue_lock, flags);

	/* For 9650SE, write the low word first */
	if (twa_dev->pdev->device == PCI_DEVICE_ID_3WARE_9650SE ||
	    twa_dev->pdev->device == PCI_DEVICE_ID_3WARE_9690SA) {
		writel(command, twa_dev->base + TW_COMMAND_QUEUE_LARGE_REG);
	}

	status = readl(twa_dev->base + TW_STATUS_REG);
	ret = twa_check_status(twa_dev, status);
	if (ret)
		goto out_unlock;

	/* FIXME: Swap command and response interrupt handling in ISR to remove
	 * the need for this check to prevent starvation. */
	if (status & TW_STATUS_COMMAND_QUEUE_FULL ||
	    atomic_read(&twa_dev->stats.pending_requests)) {
		/* Only pend internal driver commands */
		if (request->scmd) {
			ret = -EBUSY;
			goto out_unlock;
		}
		/* Couldn't post the command packet, so we do it later */
		atomic_set(&request->state, TW_STATE_PENDING);
		set_bit(request_id, twa_dev->pending_requests);
		twa_bump_stat(&twa_dev->stats.pending_requests,
			      &twa_dev->stats.max_pending_requests);
		/* Have the controller tell use when it can accept commands */
		writel(TW_CONTROL_UNMASK_COMMAND_INTERRUPT,
		       twa_dev->base + TW_CONTROL_REG);
	} else {
		if ((twa_dev->pdev->device == PCI_DEVICE_ID_3WARE_9650SE) ||
		    (twa_dev->pdev->device == PCI_DEVICE_ID_3WARE_9690SA)) {
			/* Now write the upper word */
			writel(command >> 32,
			       twa_dev->base + TW_COMMAND_QUEUE_LARGE_REG + 4);
		} else if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT)) {
			writeq(command, twa_dev->base + TW_COMMAND_QUEUE_LARGE_REG);
		} else {
			writel(command, twa_dev->base + TW_COMMAND_QUEUE_REG);
		}
		atomic_set(&request->state, TW_STATE_POSTED);
		twa_bump_stat(&twa_dev->stats.posted_requests,
			      &twa_dev->stats.max_posted_requests);
	}

	ret = 0;

out_unlock:
	spin_unlock_irqrestore(&twa_dev->queue_lock, flags);

	return ret;
}

/*
 * Initialize a command packet for an ATA passthru command.
 *
 * Locking: callers must be holding the lock for this request.
 */
static int twa_execute_passthru(struct twa_device *twa_dev, int request_id,
				struct scsi_cmnd *scmd)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_pass *cmd = &packet->command_pass;
	enum dma_data_direction dir = scmd->sc_data_direction;
	int ret;

	/* Fill out the command packet */
	memset(packet, 0, sizeof(struct twa_command_packet));
	packet->header.status.error    = 0;
	packet->header.status.severity = 0;
	packet->header.header_size     = sizeof(struct twa_command_header);

	cmd->size       = TW_PASS_COMMAND_SIZE(0);
	cmd->request_id = request_id;
	cmd->unit       = scmd->device->id;
	cmd->status     = 0;
	cmd->flags      = 0x1; /* from smartmontools */

	if (dir == DMA_NONE) {
		cmd->opcode__sgl_offset = TW_OPSGL_IN(TW_OP_ATA_PASSTHROUGH, 0);
		cmd->param              = cpu_to_le16(0x8);
	} else {
		/* SGL offset == offsetof(cmd, sgl) / sizeof(u32) */
		cmd->opcode__sgl_offset = TW_OPSGL_IN(TW_OP_ATA_PASSTHROUGH, 5);
		cmd->param              = cpu_to_le16(dir == DMA_FROM_DEVICE ? 0xd : 0xf);
	}

	if (scmd->cmnd[0] == ATA_16) {
		/* Copy SCSI ATA_16 CDB fields to command packet */
		cmd->features     = cpu_to_le16(scmd->cmnd[3] << 8 | scmd->cmnd[4]);
		cmd->sector_count = cpu_to_le16(scmd->cmnd[5] << 8 | scmd->cmnd[6]);
		cmd->lba_low      = cpu_to_le16(scmd->cmnd[7] << 8 | scmd->cmnd[8]);
		cmd->lba_mid      = cpu_to_le16(scmd->cmnd[9] << 8 | scmd->cmnd[10]);
		cmd->lba_high     = cpu_to_le16(scmd->cmnd[11] << 8 | scmd->cmnd[12]);
		cmd->device       = scmd->cmnd[13];
		cmd->command      = scmd->cmnd[14];
	} else {
		/* Copy SCSI ATA_12 CDB fields to command packet */
		cmd->features     = cpu_to_le16(scmd->cmnd[3]);
		cmd->sector_count = cpu_to_le16(scmd->cmnd[4]);
		cmd->lba_low      = cpu_to_le16(scmd->cmnd[5]);
		cmd->lba_mid      = cpu_to_le16(scmd->cmnd[6]);
		cmd->lba_high     = cpu_to_le16(scmd->cmnd[7]);
		cmd->device       = scmd->cmnd[8];
		cmd->command      = scmd->cmnd[9];
	}

	if (twa_command_mapped(scmd)) {
		struct scatterlist *sg;
		int count = scsi_dma_map(scmd);
		int i;

		if (count < 0)
			return count;
		scsi_for_each_sg(scmd, sg, count, i) {
			cmd->sgl[i].address = TW_CPU_TO_SGL(sg_dma_address(sg));
			cmd->sgl[i].length  = cpu_to_le32(sg_dma_len(sg));
		}
		cmd->size = TW_PASS_COMMAND_SIZE(count);
	} else if (scsi_sg_count(scmd) > 0) {
		if (scmd->sc_data_direction == DMA_TO_DEVICE ||
		    scmd->sc_data_direction == DMA_BIDIRECTIONAL) {
			scsi_sg_copy_to_buffer(scmd, request->buffer,
					       scsi_bufflen(scmd));
		}
		cmd->sgl[0].address = TW_CPU_TO_SGL(request->buffer_dma);
		cmd->sgl[0].length  = cpu_to_le32(scsi_bufflen(scmd));
		cmd->size           = TW_PASS_COMMAND_SIZE(1);
	}

	ret = twa_post_command_packet(twa_dev, request_id);

	/* Failed to give packet to hardware; unmap its DMA */
	if (ret && twa_command_mapped(scmd))
		scsi_dma_unmap(scmd);

	return ret;
}

/*
 * Build a command packet from a SCSI command and post it to the controller.
 *
 * Locking: callers must be holding the lock for this request.
 */
static int twa_execute_scsi(struct twa_device *twa_dev, int request_id,
			    struct scsi_cmnd *scmd)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_9xxx *cmd = &packet->command_9xxx;
	int sectors = 0;
	int ret;

	twa_init_scsi_cmd(packet, request_id, scmd->device->id, scmd->device->lun);
	memcpy(cmd->cdb, scmd->cmnd, TW_MAX_CDB_LENGTH);

	/* Map sglist from scsi layer to cmd packet */
	if (twa_command_mapped(scmd)) {
		struct scatterlist *sg;
		int count = scsi_dma_map(scmd);
		int i;

		if (count < 0)
			return count;
		scsi_for_each_sg(scmd, sg, count, i) {
			cmd->sgl[i].address = TW_CPU_TO_SGL(sg_dma_address(sg));
			cmd->sgl[i].length  = cpu_to_le32(sg_dma_len(sg));
		}
		cmd->sgl_entries = cpu_to_le16(count);
	} else if (scsi_sg_count(scmd) > 0) {
		if (scmd->sc_data_direction == DMA_TO_DEVICE ||
		    scmd->sc_data_direction == DMA_BIDIRECTIONAL) {
			scsi_sg_copy_to_buffer(scmd, request->buffer,
					       scsi_bufflen(scmd));
		}
		cmd->sgl[0].address = TW_CPU_TO_SGL(request->buffer_dma);
		cmd->sgl[0].length  = cpu_to_le32(scsi_bufflen(scmd));
		cmd->sgl_entries    = cpu_to_le16(1);
	}

	/* Update statistics */
	if (scmd->cmnd[0] == READ_6 || scmd->cmnd[0] == WRITE_6)
		sectors = scmd->cmnd[4];
	twa_update_stat(&twa_dev->stats.sectors,
			&twa_dev->stats.max_sectors, sectors);
	twa_update_stat(&twa_dev->stats.sgl_entries,
			&twa_dev->stats.max_sgl_entries, scsi_sg_count(scmd));

	ret = twa_post_command_packet(twa_dev, request_id);

	/* Failed to give packet to hardware; unmap its DMA */
	if (ret && twa_command_mapped(scmd))
		scsi_dma_unmap(scmd);

	return ret;
}

/*
 * Build a packet for a sense request command and post it to the controller.
 *
 * Locking: callers must be holding the lock for this request.
 */
static int twa_execute_sense_request(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_9xxx *cmd = &packet->command_9xxx;

	twa_init_scsi_cmd(packet, request_id, 0, 0);

	/* Initialize the CDB */
	memset(cmd->cdb, 0, sizeof(cmd->cdb));
	cmd->cdb[0] = REQUEST_SENSE;        /* opcode */
	cmd->cdb[4] = TW_ALLOCATION_LENGTH; /* allocation length */

	/* Initialize the sglist */
	cmd->sgl[0].address = TW_CPU_TO_SGL(request->buffer_dma);
	cmd->sgl[0].length  = cpu_to_le32(TW_SECTOR_SIZE);
	cmd->sgl_entries    = cpu_to_le16(1);

	return twa_post_command_packet(twa_dev, request_id);
}

/* This function will sync firmware time with the host time */
static int twa_execute_sync_time(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_7xxx *cmd = &packet->command_7xxx;
	struct twa_param_9xxx *param = request->buffer;
	time64_t localtime;
	u32 schedulertime;

	/* Convert UTC to seconds since last Sunday 12:00AM local time */
	localtime = (ktime_get_real_seconds() - (sys_tz.tz_minuteswest * 60));
	div_u64_rem(localtime - (3 * 86400), 604800, &schedulertime);

	/* Fill out the command packet */
	packet->header.status.error    = 0;
	packet->header.status.severity = 0;
	packet->header.header_size     = sizeof(struct twa_command_header);

	/* SGL offset == offsetof(cmd, sgl) / sizeof(u32) */
	cmd->opcode__sgl_offset = TW_OPSGL_IN(TW_OP_SET_PARAM, 2);
	cmd->request_id         = request_id;
	cmd->unit__host_id      = 0;
	cmd->status             = 0;
	cmd->flags              = 0;
	cmd->param_count        = cpu_to_le16(1);

	cmd->sgl[0].address = TW_CPU_TO_SGL(request->buffer);
	cmd->sgl[0].length  = cpu_to_le32(TW_SECTOR_SIZE);
	cmd->size           = TW_PARAM_COMMAND_SIZE(1);

	/* Setup the parameter descriptor */
	param->table_id        = cpu_to_le16(TW_TIMEKEEP_TABLE | 0x8000);
	param->parameter_id    = cpu_to_le16(0x3); /* SchedulerTime */
	param->parameter_size  = cpu_to_le16(4);
	*(__le32 *)param->data = cpu_to_le32(schedulertime);

	/* Now post the command */
	return twa_post_command_packet(twa_dev, request_id);
}

/*
 * Queue a SCSI command from the mid-level.
 */
static int twa_queue(struct Scsi_Host *shost, struct scsi_cmnd *scmd)
{
	struct twa_device *twa_dev = shost_priv(shost);
	int request_id = twa_begin_request(twa_dev, scmd);
	int ret;

	/* Ensure the firmware supports LUNs if attempting to use one */
	if (scmd->device->lun > shost->max_lun) {
		scmd->result = DID_BAD_TARGET << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	/* Refuse requests while resetting the controller */
	if (test_bit(TW_IN_RESET, &twa_dev->flags)) {
		twa_end_request(twa_dev, request_id);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* Create and send a command packet for the request */
	if (twa_is_passthru(scmd))
		ret = twa_execute_passthru(twa_dev, request_id, scmd);
	else
		ret = twa_execute_scsi(twa_dev, request_id, scmd);
	if (ret) {
		/* Error: clean up and mark the request as delayed/failed */
		twa_end_request(twa_dev, request_id);
		if (ret == -EBUSY)
			return SCSI_MLQUEUE_HOST_BUSY;
		twa_err(twa_dev, "Executing SCSI command failed with %d\n", ret);
		scmd->result = DID_ERROR << 16;
		scmd->scsi_done(scmd);
	}

	return 0;
}

/*
 * Reset the host as the last-resort error handler.
 */
static int twa_eh_host_reset(struct scsi_cmnd *scmd)
{
	struct twa_device *twa_dev = shost_priv(scmd->device->host);
	unsigned long flags;
	int i;
	int resets = atomic_read(&twa_dev->stats.resets);
	int ret = FAILED;

	twa_err(twa_dev, "Command 0x%x timed out, resetting card\n",
		scmd->cmnd[0]);

	/* Block until ioctls and other resets are complete */
	mutex_lock(&twa_dev->ioctl_lock);

	/* If another reset happened while waiting, assume it fixed things */
	if (atomic_read(&twa_dev->stats.resets) > resets) {
		ret = SUCCESS;
		goto out_unlock_mutex;
	}

	/* Ensure nothing else in the driver is touching the card */
	set_bit(TW_IN_RESET, &twa_dev->flags);

	/* Block any further interrupts */
	writel(TW_CONTROL_DISABLE_INTERRUPTS |
	       TW_CONTROL_MASK_COMMAND_INTERRUPT |
	       TW_CONTROL_MASK_RESPONSE_INTERRUPT,
	       twa_dev->base + TW_CONTROL_REG);

	/* Prevent the SCSI mid-level from queueing any more requests */
	spin_lock_irqsave(twa_dev->host->host_lock, flags);

	/* Abort all requests that are in progress */
	for (i = 0; i < TW_MAX_REQUESTS; ++i)
		twa_abort_request(twa_dev, i, DID_RESET);

	WARN_ON(atomic_read(&twa_dev->stats.posted_requests));
	WARN_ON(atomic_read(&twa_dev->stats.pending_requests));
	WARN_ON(!bitmap_full(twa_dev->free_requests, TW_MAX_REQUESTS));

	spin_unlock_irqrestore(twa_dev->host->host_lock, flags);

	/* Reset the controller. FIXME: magic number */
	if (twa_init_controller(twa_dev, true, 60)) {
		twa_err(twa_dev, "Failed to perform SCSI EH host reset\n");
		goto out_unlock;
	}

	atomic_inc(&twa_dev->stats.resets);
	ret = SUCCESS;

out_unlock:
	clear_bit(TW_IN_RESET, &twa_dev->flags);
	writel(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT |
	       TW_CONTROL_ENABLE_INTERRUPTS |
	       TW_CONTROL_UNMASK_RESPONSE_INTERRUPT,
	       twa_dev->base + TW_CONTROL_REG);
out_unlock_mutex:
	mutex_unlock(&twa_dev->ioctl_lock);

	return ret;
}

/*
 * Set host-specific parameters when a disk comes online.
 */
static int twa_slave_configure(struct scsi_device *sdev)
{
	/* Force a 60 second command timeout */
	blk_queue_rq_timeout(sdev->request_queue, 2 * HZ);

	return 0;
}

/*
 * Calculate unit geometry in terms of cylinders/heads/sectors.
 */
static int twa_bios_param(struct scsi_device *sdev, struct block_device *bdev,
			  sector_t capacity, int params[])
{
	int cylinders, heads, sectors;

	if (capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		cylinders = sector_div(capacity, heads * sectors);
	} else {
		heads = 64;
		sectors = 32;
		cylinders = sector_div(capacity, heads * sectors);
	}

	params[0] = heads;
	params[1] = sectors;
	params[2] = cylinders;

	return 0;
}

/*******************************
 * sysfs attributes
 */

static ssize_t twa_show_stats(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct twa_device *twa_dev = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE,
		        "3w-9xxx Driver version: %s\n"
		        "Current commands posted:   %4u\n"
		        "Max commands posted:       %4u\n"
		        "Current pending commands:  %4u\n"
		        "Max pending commands:      %4u\n"
		        "Last sgl length:           %4u\n"
		        "Max sgl length:            %4u\n"
		        "Last sector count:         %4u\n"
		        "Max sector count:          %4u\n"
		        "SCSI Host Resets:          %4u\n"
		        "AEN's:                     %4u\n",
		        TW_DRIVER_VERSION,
		        atomic_read(&twa_dev->stats.posted_requests),
		        atomic_read(&twa_dev->stats.max_posted_requests),
		        atomic_read(&twa_dev->stats.pending_requests),
		        atomic_read(&twa_dev->stats.max_pending_requests),
		        atomic_read(&twa_dev->stats.sgl_entries),
		        atomic_read(&twa_dev->stats.max_sgl_entries),
		        atomic_read(&twa_dev->stats.sectors),
		        atomic_read(&twa_dev->stats.max_sectors),
		        atomic_read(&twa_dev->stats.resets),
		        atomic_read(&twa_dev->stats.aens));
}

static struct device_attribute twa_host_stats_attr = {
	.attr = {
		.name = "stats",
		.mode = S_IRUGO,
	},
	.show = twa_show_stats,
};

static struct device_attribute *twa_host_attrs[] = {
	&twa_host_stats_attr,
	NULL,
};

static struct scsi_host_template twa_host_template = {
	.module			= THIS_MODULE,
	.name			= "3ware 9000 Storage Controller",
	.queuecommand		= twa_queue,
	.eh_host_reset_handler	= twa_eh_host_reset,
	.slave_configure	= twa_slave_configure,
	.change_queue_depth	= scsi_change_queue_depth,
	.bios_param		= twa_bios_param,
	.can_queue		= TW_MAX_REQUESTS - 2,
	.this_id		= -1,
	.sg_tablesize		= TW_APACHE_MAX_SGL_LENGTH,
	.max_sectors		= TW_MAX_SECTORS,
	.cmd_per_lun		= TW_MAX_CMDS_PER_LUN,
	.emulated		= 1,
	.no_write_same		= 1,
	.shost_attrs		= twa_host_attrs,
};

/*******************************
 * Interrupt handling and command completion
 */

static int twa_report_sense_error(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_header *header = &request->packet->header;
	size_t error_desc_len;
	char *error_desc;
	char *error_str;
	int error;

	/* Don't print errors for logical unit not supported during scan */
	error = le16_to_cpu(header->status.error);
	if (WARN_ON_ONCE(error == 0) ||
	    error == TW_ERROR_LOGICAL_UNIT_NOT_SUPPORTED ||
	    error == TW_ERROR_UNIT_OFFLINE)
		return 0;

	error_desc = header->status.error_desc;
	error_desc_len = strlen(error_desc);
	error_str = error_desc + error_desc_len + 1;
	if (error_desc_len >= TW_ERROR_DESC_LENGTH - 1 || !error_str[0])
		error_str = twa_get_string(twa_error_table, error);

	twa_err(twa_dev, "ERROR 0x%04X: %s:%s\n", error, error_str, error_desc);

	return error;
}

/* This function will queue an event */
static void twa_report_aen(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_header *header = &request->packet->header;
	struct twa_event *event;
	size_t error_desc_len;
	char *error_desc;
	char *error_str;
	int event_id;
	u16 aen;

	aen = le16_to_cpu(header->status.error);
	atomic_inc(&twa_dev->stats.aens);

	error_desc = header->status.error_desc;
	error_desc_len = strlen(error_desc);
	error_str = error_desc + error_desc_len + 1;
	if (error_desc_len < TW_ERROR_DESC_LENGTH - 1 && error_str[0])
		error_desc_len += 1 + strlen(error_str);
	else
		error_str = twa_get_string(twa_aen_table, aen);

	/* Fill out event info */
	event_id = atomic_inc_return(&twa_dev->event_sequence_id);
	event    = &twa_dev->event_queue[event_id % TW_EVENT_QUEUE_LENGTH];

	/* Check for clobber: entire queue has filled since last ioctl read */
	if (event->retrieved == TW_AEN_NOT_RETRIEVED)
		twa_dev->aen_clobbered = 1;

	event->sequence_id    = event_id;
	/* event->time_stamp_sec overflows in y2106 */
	event->time_stamp_sec = (u32)(ktime_get_real_seconds() -
				      (sys_tz.tz_minuteswest * 60));
	event->aen_code       = aen;
	event->severity       = TW_SEV_OUT(header->status.severity);
	event->retrieved      = TW_AEN_NOT_RETRIEVED;
	event->repeat_count   = 0;
	event->parameter_len  = error_desc_len;
	memcpy(event->parameter_data, error_desc, error_desc_len);

	if (event->severity != TW_AEN_SEVERITY_DEBUG)
		twa_warn(twa_dev, "AEN: %s (0x%04X): %s:%s\n",
			 twa_get_aen_severity(event->severity),
			 aen, error_str, error_desc);
}

/*
 * Complete an AEN read.
 *
 * Locking: callers must be holding the lock for this request.
 */
static void twa_complete_aen(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_9xxx *cmd = &packet->command_9xxx;
	u16 aen;

	if (cmd->status)
		twa_report_sense_error(twa_dev, request_id);

	aen = le16_to_cpu(packet->header.status.error);
	if (aen == TW_AEN_SYNC_TIME_WITH_HOST) {
		/* Reuse the request to send the time to the controller */
		if (!twa_execute_sync_time(twa_dev, request_id))
			return;
	} else if (aen != TW_AEN_QUEUE_EMPTY) {
		twa_report_aen(twa_dev, request_id);
		/* Reuse the request to keep reading AEN's from the queue */
		if (!twa_execute_sense_request(twa_dev, request_id))
			return;
	}

	twa_end_request(twa_dev, request_id);
}

/*
 * Complete a SCSI command.
 *
 * Locking: callers must be holding the lock for this request.
 */
static void twa_complete_scsi(struct twa_device *twa_dev, int request_id)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_9xxx *cmd = &packet->command_9xxx;
	struct scsi_cmnd *scmd = request->scmd;

	if (twa_command_mapped(scmd)) {
		scsi_dma_unmap(scmd);
	} else if (scsi_sg_count(scmd) > 0 &&
	    (scmd->sc_data_direction == DMA_FROM_DEVICE ||
	     scmd->sc_data_direction == DMA_BIDIRECTIONAL)) {
		scsi_sg_copy_from_buffer(scmd, request->buffer, scsi_bufflen(scmd));
	}

	/* Check for command packet errors */
	scmd->result = cmd->status << 1;
	if (cmd->status) {
		twa_report_sense_error(twa_dev, request_id);
		memcpy(scmd->sense_buffer, packet->header.sense_data, TW_SENSE_DATA_LENGTH);
	}

	/* Report underflow or residual bytes for requests with a single sg */
	if (!twa_is_passthru(scmd) && scsi_sg_count(scmd) <= 1 && !cmd->status) {
		u32 transferred = le32_to_cpu(cmd->sgl[0].length);
		if (transferred < scmd->underflow)
			scmd->result |= DID_ERROR << 16;
		if (transferred < scsi_bufflen(scmd))
			scsi_set_resid(scmd, scsi_bufflen(scmd) - transferred);
	}

	scmd->scsi_done(scmd);
	twa_end_request(twa_dev, request_id);
}

/*
 * Interrupt service routine.
 */
static irqreturn_t twa_interrupt(int irq, void *dev_id)
{
	struct twa_device *twa_dev = dev_id;
	struct twa_request *request;
	int request_id;
	u32 status;

	/* Read the controller status */
	status = readl(twa_dev->base + TW_STATUS_REG);

	/* Check if this is our interrupt, otherwise bail */
	if (!(status & TW_STATUS_VALID_INTERRUPT))
		return IRQ_NONE;

	/* If we are resetting, bail */
	if (test_bit(TW_IN_RESET, &twa_dev->flags))
		return IRQ_HANDLED;

	/* Check controller for errors, and clear them if possible */
	if (twa_check_status(twa_dev, status)) {
		writel(TW_CONTROL_CLEAR_ALL_INTERRUPTS, twa_dev->base + TW_CONTROL_REG);
		return IRQ_HANDLED;
	}

	/* Handle host interrupt */
	if (status & TW_STATUS_HOST_INTERRUPT)
		writel(TW_CONTROL_CLEAR_HOST_INTERRUPT, twa_dev->base + TW_CONTROL_REG);

	/* Handle attention interrupt */
	if (status & TW_STATUS_ATTENTION_INTERRUPT) {
		writel(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT, twa_dev->base + TW_CONTROL_REG);
		/* If there is an outstanding request, it will be completed
		 * with the responses below; otherwise, initiate one. */
		if (atomic_read(&twa_dev->aen_request_id) == TW_INVALID_REQUEST) {
			request_id = twa_begin_request(twa_dev, NULL);
			request = &twa_dev->requests[request_id];
			if (!twa_execute_sense_request(twa_dev, request_id))
				atomic_set(&twa_dev->aen_request_id, request_id);
			else
				twa_end_request(twa_dev, request_id);
		}
	}

	/* Handle response interrupt */
	for (;;) {
		/* Check for valid status before each drain. Stop draining the
		 * queue if the interrupt is cleared, so we don't steal
		 * synchronous commands from twa_eh_host_reset. */
		status = readl(twa_dev->base + TW_STATUS_REG);
		if (twa_check_status(twa_dev, status)) {
			writel(TW_CONTROL_CLEAR_ALL_INTERRUPTS, twa_dev->base + TW_CONTROL_REG);
			break;
		}
		if ((status & TW_STATUS_RESPONSE_QUEUE_EMPTY) ||
		   !(status & TW_STATUS_RESPONSE_INTERRUPT))
			break;
		request_id = TW_RESID_OUT(readl(twa_dev->base + TW_RESPONSE_QUEUE_REG));
		request = &twa_dev->requests[request_id];
		/* This can happen if racing with twa_abort_request */
		if (atomic_cmpxchg(&request->state, TW_STATE_POSTED,
				   TW_STATE_COMPLETED) != TW_STATE_POSTED) {
			BUG_ON(!test_bit(TW_IN_RESET, &twa_dev->flags));
			continue;
		}
		atomic_dec(&twa_dev->stats.posted_requests);
		if (request_id == atomic_read(&twa_dev->aen_request_id))
			twa_complete_aen(twa_dev, request_id);
		else if (request_id == atomic_read(&twa_dev->ioctl_request_id))
			complete(&twa_dev->ioctl_done);
		else
			twa_complete_scsi(twa_dev, request_id);
	}

	/* Handle command interrupt */
	if (status & TW_STATUS_COMMAND_INTERRUPT) {
		writel(TW_CONTROL_MASK_COMMAND_INTERRUPT,
		       twa_dev->base + TW_CONTROL_REG);
		while (atomic_read(&twa_dev->stats.pending_requests)) {
			/* Check for space in the queue before trying to post pending
			 * commands. Stop if if the interrupt is cleared, so we don't
			 * post commands during twa_eh_host_reset. */
			status = readl(twa_dev->base + TW_STATUS_REG);
			if (twa_check_status(twa_dev, status)) {
				writel(TW_CONTROL_CLEAR_ALL_INTERRUPTS, twa_dev->base + TW_CONTROL_REG);
				break;
			}
			if ((status & TW_STATUS_COMMAND_QUEUE_FULL) ||
			   !(status & TW_STATUS_COMMAND_INTERRUPT))
				break;
			writel(TW_CONTROL_UNMASK_COMMAND_INTERRUPT,
			       twa_dev->base + TW_CONTROL_REG);
			request_id = find_first_bit(twa_dev->pending_requests, TW_MAX_REQUESTS);
			request = &twa_dev->requests[request_id];
			/* This can happen if racing with twa_abort_request */
			if (atomic_cmpxchg(&request->state, TW_STATE_PENDING,
					   TW_STATE_STARTED) != TW_STATE_PENDING) {
				BUG_ON(!test_bit(TW_IN_RESET, &twa_dev->flags));
				/* Resetting host; don't send more commands */
				break;
			}
			/* Prepare request for retry */
			atomic_dec(&twa_dev->stats.pending_requests);
			clear_bit(request_id, twa_dev->pending_requests);
			/* Retry sending the command to the controller */
			if (twa_post_command_packet(twa_dev, request_id))
				twa_end_request(twa_dev, request_id);
		}
	}

	return IRQ_HANDLED;
}

/*******************************
 * Initialization and reset
 */

/**
 * twa_drain_response_queue - drain the P-chip/large response queue
 *
 * @twa_dev: the 3w-9xxx controller instance
 *
 * @return: 0 or a negative error code
 *
 * Only applicable to the 9550SX and newer.
 *
 * Interrupts: no restrictions.
 *
 * Locking: no restrictions.
 */
static int twa_drain_response_queue_large(struct twa_device *twa_dev)
{
	unsigned long before = jiffies;

	if (twa_dev->pdev->device == PCI_DEVICE_ID_3WARE_9000)
		return 0;

	do {
		u32 reg = readl(twa_dev->base + TW_RESPONSE_QUEUE_LARGE_REG);
		if ((reg & TW_9550SX_DRAIN_COMPLETED) == TW_9550SX_DRAIN_COMPLETED) {
			msleep(TW_PCHIP_SETTLE_TIME_MS);
			return 0;
		}
		msleep(1);
	} while (time_after(before + 30 * HZ, jiffies)); /* FIXME: magic number */

	return -ETIMEDOUT;
}

/**
 * twa_drain_response_queue - drain the response queue
 *
 * @twa_dev: the 3w-9xxx controller instance
 *
 * @return: 0 or a negative error code
 *
 * Interrupts: no restrictions.
 *
 * Locking: no restrictions.
 */
static int twa_drain_response_queue(struct twa_device *twa_dev)
{
	int count = TW_MAX_RESPONSE_DRAIN;

	do {
		u32 reg = readl(twa_dev->base + TW_STATUS_REG);
		int ret = twa_check_status(twa_dev, reg);
		if (ret)
			return ret;
		if (reg & TW_STATUS_RESPONSE_QUEUE_EMPTY)
			return 0;
		readl(twa_dev->base + TW_RESPONSE_QUEUE_REG);
	} while (--count);

	return -ETIMEDOUT;
}

/**
 * twa_poll_status - poll the status register for one or more flags
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @flags: bits that must be set in the status register
 * @timeout: maximum number of seconds to wait for the bits to be set
 *
 * @return: 0 or a negative error code
 *
 * Interrupts: no restrictions.
 *
 * Locking: no restrictions.
 */
static int twa_poll_status(struct twa_device *twa_dev, u32 flags, int timeout)
{
	unsigned long before = jiffies;

	do {
		u32 reg = readl(twa_dev->base + TW_STATUS_REG);
		int ret = twa_check_status(twa_dev, reg);
		if (ret)
			return ret;
		if ((reg & flags) == flags)
			return 0;
		msleep(50);
	} while (time_after(before + timeout * HZ, jiffies));

	return -ETIMEDOUT;
}

/**
 * twa_poll_status_gone - poll the status register for the absence of flags
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @flags: bits that must be cleared in the status register
 * @timeout: maximum number of seconds to wait for the bits to be cleared
 *
 * @return: 0 or a negative error code
 *
 * Interrupts: no restrictions.
 *
 * Locking: no restrictions.
 */
static int twa_poll_status_gone(struct twa_device *twa_dev, u32 flags,
				int timeout)
{
	unsigned long before = jiffies;

	do {
		u32 reg = readl(twa_dev->base + TW_STATUS_REG);
		int ret = twa_check_status(twa_dev, reg);
		if (ret)
			return ret;
		if ((reg & flags) == 0)
			return 0;
		msleep(50);
	} while (time_after(before + timeout * HZ, jiffies));

	return -ETIMEDOUT;
}

/**
 * twa_poll_response - poll for a response to a synchronous command
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @request_id: number of the request/expected response
 * @timeout: maximum number of seconds to wait for the response to arrive
 *
 * @return: 0 or a negative error code
 *
 * Interrupts: the controller must have interrupts disabled.
 *
 * Locking: callers must be holding the lock for this request.
 */
static int twa_poll_response(struct twa_device *twa_dev, int request_id,
			     int timeout)
{
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	int response_id;
	int ret;

	BUG_ON(atomic_read(&request->state) != TW_STATE_POSTED);

	ret = twa_poll_status_gone(twa_dev, TW_STATUS_RESPONSE_QUEUE_EMPTY,
				   timeout);
	if (ret)
		return ret;

	/* Don't know what to do with responses to other requests... */
	response_id = TW_RESID_OUT(readl(twa_dev->base + TW_RESPONSE_QUEUE_REG));
	if (response_id != request_id) {
		twa_err(twa_dev, "Unexpected request ID while polling for response\n");
		return -EIO;
	}

	/* Mark the request as completed */
	atomic_dec(&twa_dev->stats.posted_requests);
	atomic_set(&request->state, TW_STATE_COMPLETED);

	/* The response could be in any of the three command formats, but the
	 * status field is at the same offset in all of them */
	if (packet->command_9xxx.status)
		ret = twa_report_sense_error(twa_dev, request_id);

	return ret;
}

/**
 * twa_init_connection - send an INIT_CONNECTION command to the controller
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @message_credits: determines if this is a runtime or shutdown connection
 * @version: for runtime connections, version info to pass to the controller
 * @result: where to store the result word from the controller response packet
 *
 * @return: 0 or a negative error code
 *
 * Interrupts: the controller must have interrupts disabled.
 */
static int twa_init_connection(struct twa_device *twa_dev, int message_credits,
			       struct twa_version *version, u32 *result)
{
	int request_id = twa_begin_request(twa_dev, NULL);
	struct twa_request *request = &twa_dev->requests[request_id];
	struct twa_command_packet *packet = request->packet;
	struct twa_command_init *cmd = &packet->command_init;
	int features = 0;
	int ret;

	/* Turn on 64-bit sgl support if we need to */
	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
		features |= TW_FEATURE_64BIT_DMA;
	if (message_credits == TW_INIT_MESSAGE_CREDITS)
		features |= TW_EXTENDED_INIT_CONNECT;

	/* Initialize command packet */
	memset(packet, 0, sizeof(struct twa_command_packet));
	packet->header.header_size = sizeof(struct twa_command_header);
	cmd->opcode             = TW_OP_INIT_CONNECTION;
	cmd->request_id         = request_id;
	cmd->message_credits    = cpu_to_le16(message_credits);
	cmd->features           = cpu_to_le32(features);

	if (features & TW_EXTENDED_INIT_CONNECT) {
		cmd->size       = TW_INIT_COMMAND_PACKET_SIZE_EXTENDED;
		cmd->fw_srl     = cpu_to_le16(version->srl);
		cmd->fw_arch_id = cpu_to_le16(TW_9000_ARCH_ID);
		cmd->fw_branch  = cpu_to_le16(version->branch);
		cmd->fw_build   = cpu_to_le16(version->build);
	} else {
		cmd->size       = TW_INIT_COMMAND_PACKET_SIZE;
	}

	/* Send command packet to the board */
	ret = twa_post_command_packet(twa_dev, request_id);
	if (ret) {
		twa_err(twa_dev, "Failed to send init connection request\n");
		goto err_end_request;
	}

	/* Poll for completion. FIXME: magic number */
	ret = twa_poll_response(twa_dev, request_id, 30);
	if (ret) {
		twa_err(twa_dev, "No valid response during init connection\n");
		goto err_end_request;
	}

	if (features & TW_EXTENDED_INIT_CONNECT) {
		struct twa_version *fw = &twa_dev->compat_info.firmware;
		fw->srl    = le16_to_cpu(cmd->fw_srl);
		fw->branch = le16_to_cpu(cmd->fw_branch);
		fw->build  = le16_to_cpu(cmd->fw_build);
		*result    = le32_to_cpu(cmd->result);
	}

err_end_request:
	twa_end_request(twa_dev, request_id);

	return ret;
}

/**
 * twa_check_compat - check controller firmware for compatibility
 *
 * @twa_dev: the 3w-9xxx controller instance
 *
 * @return: 0 or a negative error code
 *
 * Negotiates the features supported by the driver and firmware, based on the
 * minimum and maximum versions supported by this driver.
 *
 * Interrupts: the controller must have interrupts disabled.
 */
static int twa_check_compat(struct twa_device *twa_dev)
{
	struct twa_version *version;
	int ret;
	u32 result = 0;

	/* Fill the compatibility struct with the known fixed values */
	strlcpy(twa_dev->compat_info.driver_version, TW_DRIVER_VERSION,
		sizeof(twa_dev->compat_info.driver_version));
	twa_dev->compat_info.max = (struct twa_version) {
		.srl = TW_CURRENT_DRIVER_SRL,
		.branch = TW_CURRENT_DRIVER_BRANCH,
		.build = TW_CURRENT_DRIVER_BUILD,
	};
	twa_dev->compat_info.min = (struct twa_version) {
		.srl = TW_BASE_FW_SRL,
		.branch = TW_BASE_FW_BRANCH,
		.build = TW_BASE_FW_BUILD,
	};

	/* Negotiate based on the maximum compatible firmware version */
	version = &twa_dev->compat_info.max;
	ret = twa_init_connection(twa_dev, TW_CONNECTION_RUN, version, &result);
	if (ret) {
		twa_err(twa_dev, "Connection failed while checking firmware\n");
		return ret;
	}

	if (result & TW_CTLR_FW_COMPATIBLE) {
		twa_dev->compat_info.working = *version;
		return 0;
	}

	/* Negotiate based on the minimum (base) compatible firmware version */
	version = &twa_dev->compat_info.min;
	ret = twa_init_connection(twa_dev, TW_CONNECTION_RUN, version, &result);
	if (ret) {
		twa_err(twa_dev, "Connection failed while checking firmware\n");
		return ret;
	}

	if (result & TW_CTLR_FW_COMPATIBLE) {
		twa_dev->compat_info.working = *version;
		return 0;
	}

	return -ENODEV;
}

/**
 * twa_drain_aen_queue - synchronously drain the AEN queue
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @did_reset: if this drain is in response to a controller reset
 *
 * @return: 0 or a negative error code
 *
 * Synchronously requests AEN information from the controller until the queue is
 * empty, reporting each AEN as appropriate.
 *
 * Interrupts: the controller must have interrupts disabled.
 */
static int twa_drain_aen_queue(struct twa_device *twa_dev, bool did_reset)
{
	int request_id = twa_begin_request(twa_dev, NULL);
	struct twa_request *request = &twa_dev->requests[request_id];
	u16 aen;
	int count = 0;
	int ret;

	do {
		/* Send sense request command to the controller */
		ret = twa_execute_sense_request(twa_dev, request_id);
		if (ret) {
			twa_err(twa_dev, "Error executing sense request: %d\n", ret);
			break;
		}

		/* Now poll for completion. FIXME: magic number */
		ret = twa_poll_response(twa_dev, request_id, 30);
		if (ret) {
			twa_err(twa_dev, "No valid response draining AEN queue: %d\n", ret);
			break;
		}

		aen = le16_to_cpu(request->packet->header.status.error);
		if (aen == TW_AEN_QUEUE_EMPTY) {
			/* After a reset, there should be a reset event... */
			//ret = did_reset ? -EIO : 0;
			if (did_reset)
				twa_err(twa_dev, "Missing reset event!\n");
			ret = 0;
			break;
		}
		count++;

		/* After a reset, expect and don't report a reset event */
		if (aen == TW_AEN_SOFT_RESET && did_reset) {
			did_reset = false;
			continue;
		}

		/* Never report requests to synchronize the time */
		if (aen == TW_AEN_SYNC_TIME_WITH_HOST)
			continue;

		twa_report_aen(twa_dev, request_id);
	} while (count < TW_MAX_AEN_DRAIN);

	if (count == TW_MAX_AEN_DRAIN)
		ret = -EIO;

	twa_end_request(twa_dev, request_id);

	return ret;
}

/**
 * twa_init_controller - initialize the controller, possibly resetting it
 *
 * @twa_dev: the 3w-9xxx controller instance
 * @reset: flag to reset immediately, or first try initialization without reset
 * @timeout: max number of seconds to wait for the controller to become ready
 *
 * @return: 0 or a negative error code
 *
 * Performs a soft reset of the controller (if requested or initialization
 * failed the first time). Then sets up a connection to the controller and
 * drains all queues.
 *
 * Interrupts: the controller must have interrupts disabled.
 */
static int twa_init_controller(struct twa_device *twa_dev, bool reset,
			       int timeout)
{
	u32 status_flags = TW_STATUS_MICROCONTROLLER_READY;
	int tries;
	int ret;

	for (tries = 0; tries < TW_MAX_RESET_TRIES; ++tries) {
		/* Request a reset if initialization failed before */
		if (tries > 0)
			reset = true;

		/* Reset the controller if requested */
		if (reset) {
			writel(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT |
			       TW_CONTROL_CLEAR_HOST_INTERRUPT |
			       TW_CONTROL_CLEAR_ERROR_STATUS |
			       TW_CONTROL_MASK_COMMAND_INTERRUPT |
			       TW_CONTROL_MASK_RESPONSE_INTERRUPT |
			       TW_CONTROL_DISABLE_INTERRUPTS |
			       TW_CONTROL_ISSUE_SOFT_RESET,
			       twa_dev->base + TW_CONTROL_REG);

			/* After reset, expect an attention interrupt */
			status_flags |= TW_STATUS_ATTENTION_INTERRUPT;

			/* Drain the P-chip/large response queue */
			ret = twa_drain_response_queue_large(twa_dev);
			if (ret) {
				twa_warn(twa_dev, "Failed to clear large response queue during reset: %d\n", ret);
				continue;
			}
		}

		/* Make sure the controller is in a good state */
		ret = twa_poll_status(twa_dev, status_flags, timeout);
		if (ret) {
			twa_warn(twa_dev, "Controller not ready during reset: %d\n", ret);
			continue;
		}

		/* Drain the response queue */
		ret = twa_drain_response_queue(twa_dev);
		if (ret) {
			twa_warn(twa_dev, "Failed to clear response queue during reset: %d\n", ret);
			continue;
		}

		/* Check for firmware compatibility */
		ret = twa_check_compat(twa_dev);
		if (ret) {
			twa_err(twa_dev, "Incompatible firmware detected during reset: %d\n", ret);
			break;
		}

		/* Drain the AEN queue */
		ret = twa_drain_aen_queue(twa_dev, reset);
		if (ret) {
			twa_warn(twa_dev, "AEN drain failed during reset: %d\n", ret);
			continue;
		}

		/* If we got here, the controller is in a good state */
		return 0;
	}

	return ret;
}

/*******************************
 * Character device (ioctl) operations
 */

static const struct file_operations twa_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
};

/*******************************
 * Device probe/remove and power management
 */

/**
 * twa_get_minor - reserve a device ID (used for character device minor)
 *
 * @return: an integer < TW_MAX_MINORS on success, or TW_MAX_MINORS on error
 *
 * Locking: this function is atomic.
 */
static unsigned int twa_get_minor(void)
{
	unsigned int minor;

	do {
		minor = find_first_zero_bit(twa_minor, TW_MAX_MINORS);
	} while (minor < TW_MAX_MINORS && test_and_set_bit(minor, twa_minor));

	return minor;
}

/**
 * twa_release_minor - release a device ID (used for character device minor)
 *
 * @minor: the minor previously reserved for this device
 *
 * Locking: this function is atomic.
 */
static void twa_release_minor(unsigned int minor)
{
	if (minor < TW_MAX_MINORS)
		clear_bit(minor, twa_minor);
}

/**
 * twa_init_requests - allocate coherent DMA and initialize request data
 *
 * @twa_dev: the 3w-9xxx controller instance
 *
 * @return: 0 or a negative error code
 *
 * Allocates a command packet buffer and a one-sector bounce buffer for each
 * supported request. To minimize overhead, the packet and bounce buffers are
 * each carved out of a single coherent DMA allocation.
 */
static int twa_init_requests(struct twa_device *twa_dev)
{
	void *buffers;
	void *packets;
	dma_addr_t buffers_dma;
	dma_addr_t packets_dma;
	int i;

	buffers = dma_alloc_coherent(&twa_dev->pdev->dev,
				     TW_MAX_REQUESTS * TW_SECTOR_SIZE,
				     &buffers_dma, GFP_KERNEL);
	if (!buffers)
		return -ENOMEM;
	packets = dma_alloc_coherent(&twa_dev->pdev->dev,
				     TW_MAX_REQUESTS * sizeof(struct twa_command_packet),
				     &packets_dma, GFP_KERNEL);
	if (!packets) {
		dma_free_coherent(&twa_dev->pdev->dev,
				  TW_MAX_REQUESTS * TW_SECTOR_SIZE,
				  buffers, buffers_dma);
		return -ENOMEM;
	}

	for (i = 0; i < TW_MAX_REQUESTS; ++i) {
		struct twa_request *request = &twa_dev->requests[i];

		request->scmd       = NULL;
		request->buffer     = buffers + i * TW_SECTOR_SIZE;
		request->packet     = packets + i * sizeof(struct twa_command_packet);
		request->buffer_dma = buffers_dma + i * TW_SECTOR_SIZE;
		request->packet_dma = packets_dma + i * sizeof(struct twa_command_packet);
		atomic_set(&request->state, TW_STATE_FREE);
	}

	return 0;
}

/**
 * twa_free_requests - free coherent DMA resources used communicate requests
 *                        to the controller
 *
 * @twa_dev: the 3w-9xxx controller instance
 */
static void twa_free_requests(struct twa_device *twa_dev)
{
	dma_free_coherent(&twa_dev->pdev->dev,
			  TW_MAX_REQUESTS * TW_SECTOR_SIZE,
			  twa_dev->requests[0].buffer,
			  twa_dev->requests[0].buffer_dma);
	dma_free_coherent(&twa_dev->pdev->dev,
			  TW_MAX_REQUESTS * sizeof(struct twa_command_packet),
			  twa_dev->requests[0].packet,
			  twa_dev->requests[0].packet_dma);
}

/**
 * twa_probe - attach a 3w-9xxx controller to the system
 *
 * @pdev: the PCI device containing this controller
 * @dev_id: the PCI vendor/device ID from twa_pci_table
 *
 * @return: 0 or a negative error code
 *
 * Sets up the PCI device, SCSI host, and character device for this controller.
 */
static int twa_probe(struct pci_dev *pdev, const struct pci_device_id *dev_id)
{
	struct device *dev;
	struct Scsi_Host *host;
	struct twa_device *twa_dev;
	unsigned int minor = twa_get_minor();
	int bar = pdev->device == PCI_DEVICE_ID_3WARE_9000 ? 1 : 2;
	int ret;

	if (minor == TW_MAX_MINORS) {
		dev_err(&pdev->dev, "Probe failed: too many controllers\n");
		return -ENOSPC;
	}

	/* Perform generic PCI setup */
	ret = pci_enable_device_mem(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device: %d\n", ret);
		goto err_release_minor;
	}
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	ret = pci_request_region(pdev, bar, TW_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to reserve MMIO region: %d\n", ret);
		goto err_disable_device;
	}

	/* Try using 64-bit DMA first, but fall back to 32-bit DMA */
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		goto err_disable_device;
	}

	/* Allocate a SCSI host instance with our private instance data */
	host = scsi_host_alloc(&twa_host_template, sizeof(struct twa_device));
	if (!host) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate SCSI host\n");
		goto err_disable_device;
	}

	/* Initialize device instance data */
	twa_dev = shost_priv(host);
	memset(twa_dev, 0, sizeof(struct twa_device));
	twa_dev->host = host;
	twa_dev->pdev = pdev;
	cdev_init(&twa_dev->cdev, &twa_fops);
	twa_dev->cdev.owner = THIS_MODULE;
	cdev_set_parent(&twa_dev->cdev, &pdev->dev.kobj);
	bitmap_fill(twa_dev->free_requests, TW_MAX_REQUESTS);
	spin_lock_init(&twa_dev->queue_lock);
	atomic_set(&twa_dev->aen_request_id, TW_INVALID_REQUEST);
	atomic_set(&twa_dev->event_sequence_id, -1);
	init_completion(&twa_dev->ioctl_done);
	mutex_init(&twa_dev->ioctl_lock);
	atomic_set(&twa_dev->ioctl_request_id, TW_INVALID_REQUEST);

	/* Initialize device request data */
	ret = twa_init_requests(twa_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize requests: %d\n", ret);
		goto err_free_host;
	}

	/* Map MMIO region */
	twa_dev->base = pci_ioremap_bar(pdev, bar);
	if (!twa_dev->base) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Failed to map MMIO region\n");
		goto err_free_requests;
	}

	/* Link the initialized instance data to the PCI device */
	pci_set_drvdata(pdev, twa_dev);

	/* Disable interrupts on the controller */
	writel(TW_CONTROL_DISABLE_INTERRUPTS, twa_dev->base + TW_CONTROL_REG);

	/* Initialize the controller. FIXME: magic number */
	ret = twa_init_controller(twa_dev, false, 60);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize controller: %d\n", ret);
		goto err_iounmap;
	}

	/* Try to activate MSI, if enabled. FIXME: pci_alloc_irq_vectors() */
	if (use_msi && pdev->device != PCI_DEVICE_ID_3WARE_9000 &&
	    !pci_enable_msi(pdev))
		set_bit(TW_USING_MSI, &twa_dev->flags);

	/* Now setup the interrupt handler */
	ret = request_irq(pdev->irq, twa_interrupt, IRQF_SHARED, TW_DRIVER_NAME,
			  twa_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
		goto err_shutdown;
	}

	/* Enable interrupts on the controller */
	writel(TW_CONTROL_ENABLE_INTERRUPTS |
	       TW_CONTROL_UNMASK_RESPONSE_INTERRUPT,
	       twa_dev->base + TW_CONTROL_REG);

	/* Set SCSI host-specific parameters */
	host->max_channel = 0;
	host->max_id      = pdev->device == PCI_DEVICE_ID_3WARE_9650SE ||
			    pdev->device == PCI_DEVICE_ID_3WARE_9690SA ?
				TW_MAX_UNITS_9650SE : TW_MAX_UNITS;
	host->max_lun     = TW_MAX_LUNS(twa_dev->compat_info.working.srl);
	host->unique_id   = minor;
	host->max_cmd_len = TW_MAX_CDB_LENGTH;

	/* Register the host with the SCSI mid layer */
	ret = scsi_add_host(host, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register SCSI host: %d\n", ret);
		goto err_shutdown;
	}

	/* Register the management character device */
	ret = cdev_add(&twa_dev->cdev, MKDEV(twa_major, minor), 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add character device: %d\n", ret);
		goto err_remove_host;
	}

	/* Create the character device node */
	dev = device_create(twa_class, &pdev->dev, twa_dev->cdev.dev, NULL,
			    "twa%u", minor);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(&pdev->dev, "Failed to create chardev node: %d\n", ret);
		goto err_remove_cdev;
	}

	/* Finally, scan the host */
	scsi_scan_host(host);

	return 0;

err_remove_cdev:
	cdev_del(&twa_dev->cdev);
err_remove_host:
	scsi_remove_host(host);
err_shutdown:
	twa_shutdown(pdev);
	pci_disable_msi(pdev);
err_iounmap:
	iounmap(twa_dev->base);
err_free_requests:
	twa_free_requests(twa_dev);
err_free_host:
	scsi_host_put(host);
err_disable_device:
	pci_disable_device(pdev);
	pci_release_region(pdev, bar);
err_release_minor:
	twa_release_minor(minor);

	return ret;
}

/**
 * twa_remove - detach a 3w-9xxx controller from the system
 *
 * @pdev: the PCI device containing this controller
 *
 * Undoes all of initialization done in twa_probe.
 */
static void twa_remove(struct pci_dev *pdev)
{
	struct twa_device *twa_dev = pci_get_drvdata(pdev);
	unsigned int minor = MINOR(twa_dev->cdev.dev);
	int bar = pdev->device == PCI_DEVICE_ID_3WARE_9000 ? 1 : 2;

	device_destroy(twa_class, twa_dev->cdev.dev);
	cdev_del(&twa_dev->cdev);
	scsi_remove_host(twa_dev->host);
	twa_shutdown(pdev);
	pci_disable_msi(pdev);
	iounmap(twa_dev->base);
	twa_free_requests(twa_dev);
	scsi_host_put(twa_dev->host);
	pci_disable_device(pdev);
	pci_release_region(pdev, bar);
	twa_release_minor(minor);
}

/**
 * twa_shutdown - prepare a controller for shutdown
 *
 * @pdev: the PCI device containing this controller
 *
 * Disables interrupts and tells the controller we are shutting down.
 */
static void twa_shutdown(struct pci_dev *pdev)
{
	struct twa_device *twa_dev = pci_get_drvdata(pdev);
	unsigned long flags;
	int i;

	twa_notice(twa_dev, "Shutting down\n");

	/* Block any further interrupts */
	writel(TW_CONTROL_DISABLE_INTERRUPTS |
	       TW_CONTROL_MASK_COMMAND_INTERRUPT |
	       TW_CONTROL_MASK_RESPONSE_INTERRUPT,
	       twa_dev->base + TW_CONTROL_REG);

	free_irq(pdev->irq, twa_dev);

	/* Prevent the SCSI mid-level from queueing any more requests */
	spin_lock_irqsave(twa_dev->host->host_lock, flags);

	/* Abort all requests that are in progress */
	for (i = 0; i < TW_MAX_REQUESTS; ++i)
		twa_abort_request(twa_dev, i, DID_RESET);

	WARN_ON(atomic_read(&twa_dev->stats.posted_requests));
	WARN_ON(atomic_read(&twa_dev->stats.pending_requests));
	WARN_ON(!bitmap_full(twa_dev->free_requests, TW_MAX_REQUESTS));

	spin_unlock_irqrestore(twa_dev->host->host_lock, flags);

	/* Tell the card we are shutting down */
	if (twa_init_connection(twa_dev, TW_CONNECTION_SHUTDOWN, NULL, NULL)) {
		twa_err(twa_dev, "Connection shutdown failed\n");
	} else {
		twa_notice(twa_dev, "Shutdown complete\n");
	}

	/* Clear all interrupts just before exit */
	writel(TW_CONTROL_CLEAR_ALL_INTERRUPTS, twa_dev->base + TW_CONTROL_REG);
}

static struct pci_device_id twa_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_9000)   },
	{ PCI_DEVICE(PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_9550SX) },
	{ PCI_DEVICE(PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_9650SE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_9690SA) },
	{ }
};
MODULE_DEVICE_TABLE(pci, twa_pci_table);

static struct pci_driver twa_driver = {
	.name		= TW_DRIVER_NAME,
	.id_table	= twa_pci_table,
	.probe		= twa_probe,
	.remove		= twa_remove,
	.shutdown	= twa_shutdown
};

/**
 * twa_init - initialize the 3w-9xxx driver
 *
 * @return: 0 or a negative error code
 *
 * Allocates device numbers for character devices and registers a PCI driver.
 */
static int __init twa_init(void)
{
	dev_t devt;
	int ret;

	pr_info("3ware 9000 Storage Controller Driver for Linux v%s\n",
		TW_DRIVER_VERSION);

	twa_class = class_create(THIS_MODULE, TW_DEVICE_NAME);
	if (IS_ERR(twa_class)) {
		pr_err("Failed to create character device class\n");
		return PTR_ERR(twa_class);
	}

	ret = alloc_chrdev_region(&devt, 0, TW_MAX_MINORS, TW_DEVICE_NAME);
	if (ret) {
		pr_err("Failed to register character device region\n");
		goto err_destroy_class;
	}
	twa_major = MAJOR(devt);

	ret = pci_register_driver(&twa_driver);
	if (ret) {
		pr_err("Failed to register PCI driver\n");
		goto err_unregister_chrdev_region;
	}

	return 0;

err_unregister_chrdev_region:
	unregister_chrdev_region(devt, TW_MAX_MINORS);
err_destroy_class:
	class_destroy(twa_class);

	return ret;
}

/*
 * twa_exit - deinitialize the 3w-9xxx driver
 *
 * Removes the PCI driver and character devices.
 */
static void __exit twa_exit(void)
{
	pci_unregister_driver(&twa_driver);
	unregister_chrdev_region(MKDEV(twa_major, 0), TW_MAX_MINORS);
	class_destroy(twa_class);
}

module_init(twa_init);
module_exit(twa_exit);

MODULE_AUTHOR("LSI");
MODULE_DESCRIPTION("3ware 9000 Storage Controller Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(TW_DRIVER_VERSION);
