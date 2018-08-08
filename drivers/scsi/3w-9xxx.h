/*
   3w-9xxx.h -- 3ware 9000 Storage Controller device driver for Linux.

   Written By: Adam Radford <aradford@gmail.com>
   Modifications By: Tom Couch

   Copyright (C) 2004-2009 Applied Micro Circuits Corporation.
   Copyright (C) 2010 LSI Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   NO WARRANTY
   THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
   LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
   solely responsible for determining the appropriateness of using and
   distributing the Program and assumes all risks associated with its
   exercise of rights under this Agreement, including but not limited to
   the risks and costs of program errors, damage to or loss of data,
   programs or equipment, and unavailability or interruption of operations.

   DISCLAIMER OF LIABILITY
   NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
   TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
   USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
   HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   Bugs/Comments/Suggestions should be mailed to:
   aradford@gmail.com
*/

#ifndef _3W_9XXX_H
#define _3W_9XXX_H

/* Control register bit definitions */
#define TW_CONTROL_CLEAR_PARITY_ERROR		0x00800000
#define TW_CONTROL_CLEAR_QUEUE_ERROR		0x00400000
#define TW_CONTROL_CLEAR_PCI_ABORT		0x00100000
#define TW_CONTROL_CLEAR_HOST_INTERRUPT		0x00080000
#define TW_CONTROL_CLEAR_ATTENTION_INTERRUPT	0x00040000
#define TW_CONTROL_MASK_COMMAND_INTERRUPT	0x00020000
#define TW_CONTROL_MASK_RESPONSE_INTERRUPT	0x00010000
#define TW_CONTROL_UNMASK_COMMAND_INTERRUPT	0x00008000
#define TW_CONTROL_UNMASK_RESPONSE_INTERRUPT	0x00004000
#define TW_CONTROL_CLEAR_ERROR_STATUS		0x00000200
#define TW_CONTROL_ISSUE_SOFT_RESET		0x00000100
#define TW_CONTROL_ENABLE_INTERRUPTS		0x00000080
#define TW_CONTROL_DISABLE_INTERRUPTS		0x00000040
#define TW_CONTROL_ISSUE_HOST_INTERRUPT		0x00000020
#define TW_CONTROL_CLEAR_ALL_INTERRUPTS		TW_STATUS_VALID_INTERRUPT

/* Status register bit definitions */
#define TW_STATUS_MAJOR_VERSION_MASK		0xf0000000
#define TW_STATUS_MINOR_VERSION_MASK		0x0f000000
#define TW_STATUS_PCI_PARITY_ERROR		0x00800000
#define TW_STATUS_QUEUE_ERROR			0x00400000
#define TW_STATUS_MICROCONTROLLER_ERROR		0x00200000
#define TW_STATUS_PCI_ABORT			0x00100000
#define TW_STATUS_HOST_INTERRUPT		0x00080000
#define TW_STATUS_ATTENTION_INTERRUPT		0x00040000
#define TW_STATUS_COMMAND_INTERRUPT		0x00020000
#define TW_STATUS_RESPONSE_INTERRUPT		0x00010000
#define TW_STATUS_COMMAND_QUEUE_FULL		0x00008000
#define TW_STATUS_RESPONSE_QUEUE_EMPTY		0x00004000
#define TW_STATUS_MICROCONTROLLER_READY		0x00002000
#define TW_STATUS_COMMAND_QUEUE_EMPTY		0x00001000
#define TW_STATUS_EXPECTED_BITS			0x00002000
#define TW_STATUS_UNEXPECTED_BITS		0x00f00000
#define TW_STATUS_VALID_INTERRUPT		0x00df0000

/* PCI related defines */
#define TW_PCI_CLEAR_PARITY_ERRORS		0xc100
#define TW_PCI_CLEAR_PCI_ABORT			0x2000

/* Command packet opcodes used by the driver */
#define TW_OP_INIT_CONNECTION			0x1
#define TW_OP_GET_PARAM				0x12
#define TW_OP_SET_PARAM				0x13
#define TW_OP_EXECUTE_SCSI			0x10
#define TW_OP_DOWNLOAD_FIRMWARE			0x16
#define TW_OP_RESET				0x1c

/* Asynchronous Event Notification (AEN) codes used by the driver */
#define TW_AEN_QUEUE_EMPTY			0x0000
#define TW_AEN_SOFT_RESET			0x0001
#define TW_AEN_SYNC_TIME_WITH_HOST		0x0031

#define TW_AEN_NOT_RETRIEVED			0x1
#define TW_AEN_RETRIEVED			0x2
#define TW_AEN_SEVERITY_DEBUG			0x4
#define TW_AEN_SEVERITY_ERROR			0x1

/* Command state defines */
enum {
	TW_STATE_FREE,      /* Initial state; available */
	TW_STATE_STARTED,   /* Resources reserved; in use */
	TW_STATE_PENDING,   /* Waiting to be posted; available */
	TW_STATE_POSTED,    /* Posted to the controller; available */
	TW_STATE_COMPLETED, /* Completed by the controller; in use */
	TW_STATE_ABORTED,   /* Aborted by a host reset; in use */
};

/* Compatibility defines */
#define TW_9000_ARCH_ID				0x5
#define TW_BASE_FW_BRANCH			0
#define TW_BASE_FW_BUILD			1
#define TW_BASE_FW_SRL				24
#define TW_CURRENT_DRIVER_BRANCH		0
#define TW_CURRENT_DRIVER_BUILD			0
#define TW_CURRENT_DRIVER_SRL			35
#define TW_FW_SRL_LUNS_SUPPORTED		28

/* Device flags */
#define TW_IN_ATTENTION_LOOP			0
#define TW_IN_RESET				1
#define TW_USING_MSI				3

/* Misc defines */
#define TW_9550SX_DRAIN_COMPLETED		0xffff
#define TW_AEN_WAIT_TIME			1000
#define TW_ALIGNMENT_9000			4	/* 4 bytes */
#define TW_ALIGNMENT_9000_SGL			0x3
#define TW_ALLOCATION_LENGTH			128
#define TW_BUNDLED_FW_SAFE_TO_FLASH		0x4
#define TW_COMMAND_OFFSET			128 /* 128 bytes */
#define TW_CONNECTION_RUN			256
#define TW_CONNECTION_SHUTDOWN			1
#define TW_CTLR_FW_COMPATIBLE			0x2
#define TW_CTLR_FW_RECOMMENDS_FLASH		0x8
#define TW_DRIVER				TW_MESSAGE_SOURCE_LINUX_DRIVER
#define TW_ERROR_LOGICAL_UNIT_NOT_SUPPORTED	0x10a
#define TW_ERROR_UNIT_OFFLINE			0x128
#define TW_EVENT_SOURCE_AEN			0x1000
#define TW_EVENT_SOURCE_COMMAND			0x1001
#define TW_EVENT_SOURCE_DRIVER			0x1003
#define TW_EVENT_SOURCE_PCHIP			0x1002
#define TW_EXTENDED_INIT_CONNECT		BIT(1)
#define TW_FEATURE_64BIT_DMA			BIT(0)
#define TW_INFORMATION_TABLE			0x0403
#define TW_INIT_COMMAND_PACKET_SIZE		0x3
#define TW_INIT_COMMAND_PACKET_SIZE_EXTENDED	0x6
#define TW_INIT_MESSAGE_CREDITS			0x100
#define TW_INVALID_REQUEST			-1
#define TW_IOCTL_CHRDEV_TIMEOUT			60 /* 60 seconds */
#define TW_IOCTL_ERROR_OS_EFAULT		-EFAULT // Bad address
#define TW_IOCTL_ERROR_OS_EINTR			-EINTR	// Interrupted system call
#define TW_IOCTL_ERROR_OS_EINVAL		-EINVAL // Invalid argument
#define TW_IOCTL_ERROR_OS_EIO			-EIO // I/O error
#define TW_IOCTL_ERROR_OS_ENODEV		-ENODEV // No such device
#define TW_IOCTL_ERROR_OS_ENOMEM		-ENOMEM // Out of memory
#define TW_IOCTL_ERROR_OS_ENOTTY		-ENOTTY // Not a typewriter
#define TW_IOCTL_ERROR_OS_ERESTARTSYS		-ERESTARTSYS // Restart system call
#define TW_IOCTL_ERROR_STATUS_AEN_CLOBBER	0x1004 // AEN clobber occurred
#define TW_IOCTL_ERROR_STATUS_LOCKED		0x1002 // Already locked
#define TW_IOCTL_ERROR_STATUS_NOT_LOCKED	0x1001 // Not locked
#define TW_IOCTL_ERROR_STATUS_NO_MORE_EVENTS	0x1003 // No more events
#define TW_IOCTL_FIRMWARE_PASS_THROUGH		0x108
#define TW_IOCTL_GET_COMPATIBILITY_INFO		0x101
#define TW_IOCTL_GET_FIRST_EVENT		0x103
#define TW_IOCTL_GET_LAST_EVENT			0x102
#define TW_IOCTL_GET_LOCK			0x106
#define TW_IOCTL_GET_NEXT_EVENT			0x104
#define TW_IOCTL_GET_PREVIOUS_EVENT		0x105
#define TW_IOCTL_RELEASE_LOCK			0x107
#define TW_IOCTL_WAIT_TIME			(1 * HZ) /* 1 second */
#define TW_ISR_DONT_COMPLETE			2
#define TW_ISR_DONT_RESULT			3
#define TW_MAX_AEN_DRAIN			255
#define TW_MAX_CDB_LENGTH			16
#define TW_MAX_CMDS_PER_LUN			254
#define TW_MAX_RESET_TRIES			2
#define TW_MAX_RESPONSE_DRAIN			256
#define TW_MAX_SECTORS				256
#define TW_MAX_SENSE_LENGTH			256
#define TW_MAX_MINORS				32
#define TW_MAX_UNITS				16
#define TW_MAX_UNITS_9650SE			32
#define TW_MESSAGE_SOURCE_CONTROLLER_ERROR	3
#define TW_MESSAGE_SOURCE_CONTROLLER_EVENT	4
#define TW_MESSAGE_SOURCE_LINUX_DRIVER		6
#define TW_MESSAGE_SOURCE_LINUX_OS		9
#define TW_PARAM_BIOSVER			4
#define TW_PARAM_BIOSVER_LENGTH			16
#define TW_PARAM_FWVER				3
#define TW_PARAM_FWVER_LENGTH			16
#define TW_PARAM_PORTCOUNT			3
#define TW_PARAM_PORTCOUNT_LENGTH		1
#define TW_PCHIP_SETTLE_TIME_MS			500
#define TW_MAX_REQUESTS				256
#define TW_EVENT_QUEUE_LENGTH			TW_MAX_REQUESTS /* XXX: could be anything */
#define TW_Q_START				0
#define TW_SECTOR_SIZE				512
#define TW_SENSE_DATA_LENGTH			18
#define TW_STATUS_CHECK_CONDITION		2
#define TW_TIMEKEEP_TABLE			0x040a
#define TW_VERSION_TABLE			0x0402
#define TW_OS					TW_MESSAGE_SOURCE_LINUX_OS

#ifndef PCI_DEVICE_ID_3WARE_9000
#define PCI_DEVICE_ID_3WARE_9000 0x1002
#endif
#ifndef PCI_DEVICE_ID_3WARE_9550SX
#define PCI_DEVICE_ID_3WARE_9550SX 0x1003
#endif
#ifndef PCI_DEVICE_ID_3WARE_9650SE
#define PCI_DEVICE_ID_3WARE_9650SE 0x1004
#endif
#ifndef PCI_DEVICE_ID_3WARE_9690SA
#define PCI_DEVICE_ID_3WARE_9690SA 0x1005
#endif

/* Bitmask macros to eliminate bitfields */

/* opcode: 5, reserved: 3 */
#define TW_OPRES_IN(op) (op & 0x1f)
#define TW_OP_OUT(x) (x & 0x1f)

/* opcode: 5, sgloffset: 3 */
#define TW_OPSGL_IN(op) ((2 << 5) | (op & 0x1f))
#define TW_SGL_OUT(x) ((x >> 5) & 0x7)

/* severity: 3, reserved: 5 */
#define TW_SEV_OUT(x) (x & 0x7)

/* reserved_1: 4, response_id: 8, reserved_2: 20 */
#define TW_RESID_OUT(x) ((x >> 4) & 0xff)

/* request_id: 12, lun: 4 */
#define TW_REQ_LUN_IN(request_id, lun) cpu_to_le16(((lun & 0xf) << 12) | (request_id & 0xfff))
#define TW_LUN_OUT(lun) ((lun >> 12) & 0xf)

/* Macros */
#define TW_CONTROL_REG			0x00
#define TW_STATUS_REG			0x04
#define TW_COMMAND_QUEUE_REG		0x08
#define TW_RESPONSE_QUEUE_REG		0x0c
#define TW_COMMAND_QUEUE_LARGE_REG	0x20
#define TW_RESPONSE_QUEUE_LARGE_REG	0x30




#define TW_DEVICE_NAME "twa"
#define TW_DRIVER_VERSION "2.26.02.014"
#define TW_DRIVER_VERSION_LENGTH 32

#define TW_ERROR_DESC_LENGTH 98



#define TW_PRINTK(h,a,b,c) { \
if (h) \
printk(KERN_WARNING "3w-9xxx: scsi%d: ERROR: (0x%02X:0x%04X): %s.\n",h->host_no,a,b,c); \
else \
printk(KERN_WARNING "3w-9xxx: ERROR: (0x%02X:0x%04X): %s.\n",a,b,c); \
}
#define TW_MAX_LUNS(srl) (srl < TW_FW_SRL_LUNS_SUPPORTED ? 1 : 16)
#define TW_COMMAND_SIZE(sgls)		(2 + ((sgls) * (sizeof(struct twa_sgl_entry) / 4))) /* number of 32-bit words including sgl for 7xxx */
#if IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT)
typedef __le64 twa_dma_addr_t;
#define TW_CPU_TO_SGL(x)		cpu_to_le64(x)
#define TW_APACHE_MAX_SGL_LENGTH	72
#define TW_APACHE_PADDING_LENGTH	8 /* Pad entire command structure to multiple of 16 bytes. */
#define TW_ESCALADE_MAX_SGL_LENGTH	41
#define TW_ESCALADE_PADDING_LENGTH	12 /* Pad entire command structure to multiple of 16 bytes. */
#else
typedef __le32 twa_dma_addr_t;
#define TW_CPU_TO_SGL(x)		cpu_to_le32(x)
#define TW_APACHE_MAX_SGL_LENGTH	109
#define TW_APACHE_PADDING_LENGTH	0 /* Pad entire command structure to multiple of 16 bytes. */
#define TW_ESCALADE_MAX_SGL_LENGTH	62
#define TW_ESCALADE_PADDING_LENGTH	8 /* Pad entire command structure to multiple of 16 bytes. */
#endif

/* AEN string type */
struct twa_message {
	u16	code;
	char	*text;
};

/* Controller parameter descriptor */
struct twa_param_9xxx {
	__le16	table_id;
	__le16	parameter_id;
	__le16	parameter_size;
	__le16	actual_parameter_size;
	u8	data[];
} __packed;

/* Scatter-gather list entry */
struct twa_sgl_entry {
	twa_dma_addr_t		address;
	__le32			length;
} __packed;

/* Command packet header: 128 bytes */
struct twa_command_header {
	u8			sense_data[TW_SENSE_DATA_LENGTH];
	struct {
		u8		__reserved[4];
		__le16		error;
		u8		__padding;
		u8		severity;
		char		error_desc[TW_ERROR_DESC_LENGTH];
	} status;
	u8			header_size;
	u8			__reserved[2];
	u8			sense_size;
} __packed;

/* Command data for INIT_CONNECTION command */
struct twa_command_init {
	u8			opcode;
	u8			size;
	u8			request_id;
	u8			__reserved;
	u8			status;
	u8			flags;
	__le16			message_credits;
	__le32			features;
	__le16			fw_srl;
	__le16			fw_arch_id;
	__le16			fw_branch;
	__le16			fw_build;
	__le32			result;
} __packed;

/* Command data for 7000+ controllers: 512 bytes */
struct twa_command_7xxx {
	u8			opcode__sgl_offset;
	u8			size;
	u8			request_id;
	u8			unit__host_id;
	u8			status;
	u8			flags;
	__le16			param_count;
	struct twa_sgl_entry	sgl[TW_ESCALADE_MAX_SGL_LENGTH];
	u8			__padding[TW_ESCALADE_PADDING_LENGTH];
} __packed;

/* Command data for 9000+ controllers: 896 bytes */
struct twa_command_9xxx {
	u8			opcode;
	u8			unit;
	__le16			request_id__lun;
	u8			status;
	u8			sgl_offset;
	__le16			sgl_entries;
	u8			cdb[TW_MAX_CDB_LENGTH];
	struct twa_sgl_entry	sgl[TW_APACHE_MAX_SGL_LENGTH];
	u8			__padding[TW_APACHE_PADDING_LENGTH];
} __packed;

/* Union of the possible command packet formats: 1024 bytes maximum */
struct twa_command_packet {
	struct twa_command_header	header;
	union {
		struct twa_command_init	command_init;
		struct twa_command_7xxx	command_7xxx;
		struct twa_command_9xxx	command_9xxx;
	};
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_ioctl_command {
	u32	control_code;
	u32	status;
	u32	unique_id;
	u32	sequence_id;
	u32	os_specific;
	u32	buffer_length;
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_version {
	u16	srl;
	u16	branch;
	u16	build;
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_compat_info {
	char			driver_version[TW_DRIVER_VERSION_LENGTH];
	struct twa_version	working;  /* Negotiated compatibility level */
	struct twa_version	max;      /* Driver maximim firmware version */
	struct twa_version	min;      /* Driver minimum firmware version */
	struct twa_version	firmware; /* Firmware stored in controller */
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_event {
	u32	sequence_id;
	u32	time_stamp_sec;
	u16	aen_code;
	u8	severity;
	u8	retrieved; /* by userspace */
	u8	repeat_count;
	u8	parameter_len;
	char	parameter_data[TW_ERROR_DESC_LENGTH];
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_lock {
	u32	timeout_msec;
	u32	time_remaining_msec;
	u32	force_flag;
} __packed;

/* Userspace ABI: used in ioctl buffer */
struct twa_ioctl {
	struct twa_ioctl_command	ioctl_command;
	u8				__padding[512 - sizeof(struct twa_ioctl_command)];
	struct twa_command_packet	firmware_command;
	union {
		struct twa_compat_info	compat_info;
		struct twa_event	event;
		struct twa_lock		lock;
		u8			buffer[2048 * TW_MAX_SECTORS];
	};
} __packed;

struct twa_request {
	struct scsi_cmnd		*scmd;
	void				*buffer;
	struct twa_command_packet	*packet;
	dma_addr_t			buffer_dma;
	dma_addr_t			packet_dma;
	atomic_t			state;
};

struct twa_stats {
	atomic_t	posted_requests;
	atomic_t	max_posted_requests;
	atomic_t	pending_requests;
	atomic_t	max_pending_requests;
	atomic_t	sectors;
	atomic_t	max_sectors;
	atomic_t	sgl_entries;
	atomic_t	max_sgl_entries;
	atomic_t	aens;
	atomic_t	resets;
};

struct twa_device {
	struct Scsi_Host	*host;
	struct pci_dev		*pdev;
	struct cdev		cdev;

	u8 __iomem		*base;
	long			flags;

	struct twa_request	requests[TW_MAX_REQUESTS];
	unsigned long		free_requests[BITS_TO_LONGS(TW_MAX_REQUESTS)];
	unsigned long		pending_requests[BITS_TO_LONGS(TW_MAX_REQUESTS)];

	spinlock_t		queue_lock;

	bool			aen_clobbered; /* too many events since read by userspace */
	atomic_t		aen_request_id;
	struct twa_event	event_queue[TW_EVENT_QUEUE_LENGTH];
	atomic_t		event_sequence_id;

	struct completion	ioctl_done;
	struct mutex		ioctl_lock;
	ktime_t			ioctl_lock_time;
	atomic_t		ioctl_request_id;

	struct twa_stats	stats;
	struct twa_compat_info	compat_info;
};

#endif /* _3W_9XXX_H */
