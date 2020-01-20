#ifndef _H_SIR
#define _H_SIR

#include <linux/ioctl.h>
//Userspace accessible parameters for the SIR driver

#define SIR_IOCTL_MAGIC 0xA5
#define SIR_IOCTL_GET _IOR(SIR_IOCTL_MAGIC, 0, long)

#define SIR_INTERRUPT_TYPE u64

#endif