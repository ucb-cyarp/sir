#ifndef _H_SIR_INTERNAL
#define _H_SIR_INTERNAL
    #include <linux/init.h>
    #include <linux/module.h>
    #include <linux/kern_levels.h>
    #include <linux/types.h>
    #include <linux/fs.h>
    #include <linux/kdev_t.h>
    #include <linux/cdev.h>
    #include <linux/types.h>
    #include <linux/mutex.h>

    //==== Init Functions ====
    static void sir_cleanup(void);
    static int sir_init(void);
    static void sir_exit(void);

    //==== Supported Char Driver Functions ====
    int sir_open(struct inode *inode, struct file *filp);
    int sir_release(struct inode *inode, struct file *filp);
    loff_t sir_llseek(struct file *filp, loff_t off, int whence);
    ssize_t sir_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
    long sir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

    // ==== Structure for partial reads ====
    struct partial_read_state{
        unsigned int interrupts;
        char ind;
        struct mutex lock;
    } ;

    // ==== Define a debug print macro ====
    #ifdef SIR_DEBUG
        #define printkd(...) printk(__VA_ARGS__)
    #else
        #define printkd(...) 
    #endif

    //==== Helper Function ====
    #define SIR_MIN(x, y) (((x) < (y)) ? (x) : (y))

#endif