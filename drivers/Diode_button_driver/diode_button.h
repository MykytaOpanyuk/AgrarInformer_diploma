/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UAPI_LINUX_DIODE_BUTTON_H
#define _UAPI_LINUX_DIODE_BUTTON_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Chosen to be unique w.r.t. Documentation/ioctl/ioctl-number */
#define HW3_IOCTL_MAGIC	                        0x91

#define DIODE_BUTTON_IOC_SET_LED                _IOWR(HW3_IOCTL_MAGIC, 0, int)
#define DIODE_BUTTON_IOC_GET_LED                _IOR(HW3_IOCTL_MAGIC, 1, int)
#define DIODE_BUTTON_IOC_KERN_CONTROL           _IOW(HW3_IOCTL_MAGIC, 2, int)

#define WRITE_BUF_LEN                           10
#define READ_BUF_LEN                            2 /* 1 character and \0 */

#endif /* _UAPI_LINUX_DIODE_BUTTON_H */
