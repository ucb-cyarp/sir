/**
 * SIR: Simple Interrupt Reporter
 * A kernel driver for requesting the interrupt count for the current CPU
 * since the system last booted.
 * 
 * Based on examples from "Linux Device Drivers 3rd Ed." by J. Corbert, 
 * A. Rubini, G. Kroah-Hartman
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kern_levels.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/mutex.h> //Changed since LDD3
#include <linux/kallsyms.h>

//#define SIR_DEBUG
#include "sir_internal.h"

MODULE_AUTHOR("Christopher Yarp");
MODULE_DESCRIPTION("SIR: Simple Interrupt Reporter");
MODULE_LICENSE("Dual BSD/GPL");

// ==== Global Vars ====
// ++ Device Numbers ++
dev_t dev = 0;
int sir_major = -1;
int sir_minor = -1;

// ++ Char Device ++
//Because only 1 subdevice exists and 
//no data is shared between subdevices
//or different open file handles to the
//same device, the cdev structure
//is not stored within another
//structure.

//TODO: Change to have a device for each CPU
struct cdev *cdevp = NULL;

// ++ Char Driver Supported Operations ++
struct file_operations sir_fops = {
	.owner =          THIS_MODULE,
	.llseek =         sir_llseek,
	.read =           sir_read,
	// .unlocked_ioctl = sir_ioctl, //This changed from LDD3 (see https://lwn.net/Articles/119652/, thanks https://unix.stackexchange.com/questions/4711/what-is-the-difference-between-ioctl-unlocked-ioctl-and-compat-ioctl)
	.open =           sir_open,
	.release =        sir_release,
};

// ++ Function pointer for interrupt ++
//The function which returns the number of archetecture
//specific interrupts is unfortunatly not exported like
//kstat_cpu_irqs_sum is.  Without it, we do not get the 
//full picture of what interupts have occured on a 
//particular CPU.  One solution is to find the 
//address for this function at runtime and assign
//it to a function pointer.  That is what the function
//pointer below is for.

//The function prototype was taken from 

//TODO: This is a hacky solution to the problem as
//the normal compiler checks are bypassed.  Therefore,
//need to keep an eye on the function to make sure
//the function prototype does not change.

u64 (*arch_irq_stat_cpu_local) (unsigned int cpu) = NULL;


//==== Char Driver Functons ====

//Each time the device is opened, a small amount
//of data is allocated to contain the last interrupt
//count returned.  This is only used if the reader
//requests less than the number of bytes used
//to store the interrupt count.
int sir_open(struct inode *inode, struct file *filp)
{
    //inode->i_cdev has a pointer to the cdev structure created durring init
    //However, since sir only defines one subdevice, it is not needed

    //**** Allocate Data for Partial Reads ****
    struct partial_read_state* partial_state = (struct partial_read_state*) kmalloc(sizeof(struct partial_read_state), GFP_KERNEL);
    if(partial_state == NULL){
        printk(KERN_WARNING "sir: Could not allocate data for partial interrupt reads\n");
        return -1;
    }
    partial_state->interrupts = 0;
    partial_state->ind = 0;
    mutex_init(&(partial_state->lock));

    filp->private_data = partial_state;

    printkd(KERN_INFO "sir: Opened Device\n");

    return 0;
}

//The data 
int sir_release(struct inode *inode, struct file *filp)
{
    //**** Free Partial Read Data ****
    kfree(filp->private_data);

    printkd(KERN_INFO "sir: Released Device\n");

    return 0;
}

//Does not perform any action, the file pointer is kept at the
//same point
loff_t sir_llseek(struct file *filp, loff_t off, int whence){
    printkd(KERN_INFO "sir: Seek\n");
    return filp->f_pos;
}


//Gets the number of interrupts since boot for the CPU from which
//this function is being called.  
// NOTE: There is a lock which is used if multiple threads share access
//       to the same file.  Avoid this by having each thread open its own
//       file handle.
// TODO: Validate this is the expected CPU using a char device per CPU
// Semantics:
// * If any partial results from another call are present, they are returned
// * If there are no partial results, the current interrupt count is fetched and returned
ssize_t sir_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct partial_read_state* partial_state = (struct partial_read_state*) filp->private_data;
    ssize_t final_count = 0;

    //TODO: Check cpu even when there is partial data
    printkd(KERN_INFO "sir: Read\n");

    //To protect against multiple threads having the sample file handle, a mutex is used
    mutex_lock(&(partial_state->lock));

    //First, check if any partial read results are present
    
    if(partial_state->ind != 0){
        int remaining_partial;
        int remaining_to_write;

        printkd("sir: Returning previous partial result\n");

        if(partial_state->ind >= sizeof(partial_state->interrupts)){
            printk(KERN_WARNING "sir: Unexpected index durring read: %d\n", partial_state->ind);
            mutex_unlock(&(partial_state->lock));
            return -EFAULT;
        }

        //Partial data avail
        remaining_partial = sizeof(partial_state->interrupts) - partial_state->ind;
        final_count = SIR_MIN(remaining_partial, count); 

        //Set the final count to what was actually copied
        remaining_to_write = copy_to_user(buf, &(partial_state->interrupts), final_count);
        if(remaining_to_write < 0){
            printk(KERN_WARNING "sir: Error when copying result to user: %ld\n", final_count);
            mutex_unlock(&(partial_state->lock));
            return -EFAULT;
        }

        if(final_count == remaining_partial){
            partial_state->ind = 0;
        }else{
            partial_state->ind += final_count;
        }
    }else{
        int remaining_to_write;
        //No partial data avail, get new data
        int cpu = get_cpu(); //Also disables premption which is important for the kstat functions
        //This only gets the non-arch specific interrupts
        partial_state->interrupts = kstat_cpu_irqs_sum(cpu); //Thanks to https://stackoverflow.com/questions/3700536/get-interrupt-counters-like-proc-interrupts-from-code for pointing in the right direction
        partial_state->interrupts += arch_irq_stat_cpu_local(cpu); //This gets the archetecture specific interrupts
        put_cpu(); //Re-enables premption

        printkd("sir: CPU: %d, Interrupts: %lld\n", cpu, partial_state->interrupts);

        final_count = SIR_MIN(sizeof(partial_state->interrupts), count);

        remaining_to_write = copy_to_user(buf, &(partial_state->interrupts), final_count);
        if(remaining_to_write < 0){
            printk(KERN_WARNING "sir: Error when copying result to user: %ld\n", final_count);
            return -EFAULT;
        }

        if(final_count == sizeof(partial_state->interrupts)){
            partial_state->ind = 0;
        }else{
            partial_state->ind = final_count;
        }
    }

    *f_pos += final_count;

    mutex_unlock(&(partial_state->lock));

    return final_count;
}

// //As an alternative to using the char driver, the current interrupt
// //can be accessed using a ioctl call.
// long sir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
// {

// }

// ==== Init / Cleanup Functions ====
static void sir_cleanup(void)
{
    if(cdevp != NULL){
        cdev_del(cdevp);
        printkd(KERN_INFO "sir: Unregistered sir0\n");
    }

    if(sir_major >= 0){
        unregister_chrdev_region(dev, 1);
        printkd(KERN_INFO "sir: Unregistered Region\n");
    }
}

static int sir_init(void)
{
    //**** Get a pointer to arch_irq_stat_cpu ****
    //Thanks for the pointer https://stackoverflow.com/questions/40431194/how-do-i-access-any-kernel-symbol-in-a-kernel-module
    //This is unfortuantly a suboptomal solution that will need to be kept track of.
    int status;
    preempt_disable();
    arch_irq_stat_cpu_local = (typeof(arch_irq_stat_cpu_local)) kallsyms_lookup_name("arch_irq_stat_cpu");
    preempt_enable();
    if(arch_irq_stat_cpu_local == NULL)
    {
        printk(KERN_WARNING "sir: Unable to find arch_irq_stat_cpu");
        sir_cleanup();
        return -ENOMEM;
    }

    //**** Create Device ****
    status = alloc_chrdev_region(&dev, 0, 1, "sir");
    if(status < 0){
        //Error Allocating Device
        sir_cleanup();
        return -ENOMEM;
    }

    sir_major = MAJOR(dev);
    sir_minor = MINOR(dev);
    
    printkd(KERN_INFO "sir: Dev Number - Major: %d, Minor: %d\n", sir_major, sir_minor);

    //**** Create and Register Char Dev ****
    cdevp = cdev_alloc();
    if(cdevp == NULL){
        //Error Allocating Char Dev
        sir_cleanup();
        return -ENOMEM;
    }
    cdevp->ops = &sir_fops;
    cdevp->owner = THIS_MODULE;

    status = cdev_add(cdevp, dev, 1);
    if(status < 0){
        //Registration failed
        sir_cleanup();
        return -ENOMEM;
    }
    printk(KERN_INFO "sir: Registered sir0\n");

    printk(KERN_INFO "sir: Startup Complete\n");
    return 0;

}

static void sir_exit(void)
{
    sir_cleanup();
    printk(KERN_INFO "sir: Shutdown\n");
}

//++ Assign Module Init/Cleanup Functions ++ 
module_init(sir_init);
module_exit(sir_exit);

