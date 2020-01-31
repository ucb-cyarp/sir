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
#include <linux/irqflags.h>
#include <asm/hardirq.h>
#include <asm/mce.h>
#include <asm/desc.h>

//#define SIR_DEBUG
#include "sir_internal.h"

MODULE_AUTHOR("Christopher Yarp");
MODULE_DESCRIPTION("SIR: Simple Interrupt Reporter");
MODULE_LICENSE("Dual BSD/GPL");

// ==== Externally Defined Symbols ====
// This is the structure in which IRQ stats for each CPU are
// stored.  It is declared in arch/x86/include/asm/hardirq.h
// and defined in arch/x86/kernel/irq.c
// The structure irq_cpustat_t is defined in 
// arch/x86/include/asm/hardirq.h
DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define irq_stats(cpu)    (&per_cpu(irq_stat, cpu))

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
	.unlocked_ioctl = sir_ioctl, //This changed from LDD3 (see https://lwn.net/Articles/119652/, thanks https://unix.stackexchange.com/questions/4711/what-is-the-difference-between-ioctl-unlocked-ioctl-and-compat-ioctl)
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

    //Reset Internal Struct
    partial_state->irq_std = 0;       //Standard interrupts (not x86 specific)
    partial_state->irq_nmi = 0;       //NMI: Non-maskable interrupts             (__nmi_count)
    partial_state->irq_loc = 0;       //LOC: Local timer interrupts              (apic_timer_irqs)
    partial_state->irq_spu = 0;       //SPU: Spurious interrupts                 (irq_spurious_count)
    partial_state->irq_pmi = 0;       //PMI: Performance monitoring interrupts   (apic_perf_irqs)
    partial_state->irq_iwi = 0;       //IWI: IRQ work interrupts                 (apic_irq_work_irqs)
    partial_state->irq_rtr = 0;       //RTR: APIC ICR read retries               (icr_read_retry_count)
    partial_state->irq_plt = 0;       //PLT: Platform interrupts                 (x86_platform_ipis)
    partial_state->irq_res = 0;       //RES: Rescheduling interrupts             (irq_resched_count)
    partial_state->irq_cal = 0;       //CAL: Function call interrupts            (irq_call_count)
    partial_state->irq_tlb = 0;       //TLB: TLB shootdowns                      (irq_tlb_count)
    partial_state->irq_trm = 0;       //TRM: Thermal event interrupts            (irq_thermal_count)
    partial_state->irq_thr = 0;       //THR: Threshold APIC interrupts           (irq_threshold_count)
    partial_state->irq_dfr = 0;       //DFR: Deferred Error APIC interrupts      (irq_deferred_error_count)
    partial_state->mce_exception = 0; //MCE: Machine check exceptions            (per_cpu(mce_exception_count, j))
    partial_state->mce_poll = 0;      //MCP: Machine check polls                 (per_cpu(mce_poll_count, j))
    partial_state->irq_hyp = 0;       //HYP: Hypervisor callback interrupts      (irq_hv_callback_count)
    partial_state->irq_pin = 0;       //PIN: Posted-interrupt notification event (kvm_posted_intr_ipis)
    partial_state->irq_npi = 0;       //NPI: Nested posted-interrupt event       (kvm_posted_intr_nested_ipis)
    partial_state->irq_piw = 0;       //PIW: Posted-interrupt wakeup event       (kvm_posted_intr_wakeup_ipis)
    partial_state->arch_irq_stat_sum = 0;
    partial_state->softirq_sum = 0;
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
//
//
// NOTE: IOCTL and the char driver use the same internal state and may clobber each other's
//       state if used together.  IOCLT resets the read index to 0 which causes
//       the 
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

        if(partial_state->ind >= sizeof(partial_state->irq_std)){
            printk(KERN_WARNING "sir: Unexpected index durring read: %d\n", partial_state->ind);
            mutex_unlock(&(partial_state->lock));
            return -EFAULT;
        }

        //Partial data avail
        remaining_partial = sizeof(partial_state->irq_std) - partial_state->ind;
        final_count = SIR_MIN(remaining_partial, count); 

        //Set the final count to what was actually copied
        remaining_to_write = copy_to_user(buf, &(partial_state->irq_std), final_count);
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
        unsigned long irq_flags;

        //No partial data avail, get new data
        int cpu = get_cpu(); //Also disables premption which is important for the kstat functions

        //Disable Interrupts to get accurate interrupt and softirq counts
        //This is based on the "Disabling all interrupts" section of Ch. 10 of LDD3
        local_irq_save(irq_flags);

        //This only gets the non-arch specific interrupts
        partial_state->irq_std = kstat_cpu_irqs_sum(cpu); //Thanks to https://stackoverflow.com/questions/3700536/get-interrupt-counters-like-proc-interrupts-from-code for pointing in the right direction
        partial_state->irq_std += arch_irq_stat_cpu_local(cpu); //This gets the archetecture specific interrupts
        
        //Re-enable interrupts before copying results to user
        local_irq_restore(irq_flags);
        
        put_cpu(); //Re-enables premption

        printkd("sir: CPU: %d, Interrupts: %lld\n", cpu, partial_state->irq_std);

        final_count = SIR_MIN(sizeof(partial_state->irq_std), count);

        remaining_to_write = copy_to_user(buf, &(partial_state->irq_std), final_count);
        if(remaining_to_write < 0){
            printk(KERN_WARNING "sir: Error when copying result to user: %ld\n", final_count);
            return -EFAULT;
        }

        if(final_count == sizeof(partial_state->irq_std)){
            partial_state->ind = 0;
        }else{
            partial_state->ind = final_count;
        }
    }

    *f_pos += final_count;

    mutex_unlock(&(partial_state->lock));

    return final_count;
}

//This gets the sum of softirqs for the cpu.  It is similar to show_softirqs in fs/proc/softirqs.c
inline void get_softirqs(int cpu, SIR_INTERRUPT_TYPE* softirq_sum){
    int i;
    SIR_INTERRUPT_TYPE sum = 0;
    for(i = 0; i<NR_SOFTIRQS; i++){
        sum += kstat_softirqs_cpu(i, cpu); //Get softirq i count.  Can get the name using softirq_to_name[i]
    }

    *softirq_sum = sum;
}

inline void get_interrupts(int cpu, struct partial_read_state* partial_state){
    //This function stores the indevidual components of the sum that arch_irq_stat_cpu 
    //computes.
    partial_state->irq_std = kstat_cpu_irqs_sum(cpu); //Get the standard (non x86 specific) interrupts

    partial_state->irq_nmi = irq_stats(cpu)->__nmi_count;
    #ifdef CONFIG_X86_LOCAL_APIC
        partial_state->irq_loc = irq_stats(cpu)->apic_timer_irqs;
        partial_state->irq_spu = irq_stats(cpu)->irq_spurious_count;
        partial_state->irq_pmi = irq_stats(cpu)->apic_perf_irqs;
        partial_state->irq_iwi = irq_stats(cpu)->apic_irq_work_irqs;
        partial_state->irq_rtr = irq_stats(cpu)->icr_read_retry_count;

        //TODO: x86_platform_ipi_callback is not exported
        //      Will not track this for now.  This is tracked
        //      by arch_irq_stat_cpu_local however so can be obtained via that
        //
        // if (x86_platform_ipi_callback) {
        //     partial_state->irq_plt = irq_stats(cpu)->x86_platform_ipis;
        // }else{
            partial_state->irq_plt = 0;
        // }
    #else
        partial_state->irq_loc = 0;
        partial_state->irq_spu = 0;
        partial_state->irq_pmi = 0;
        partial_state->irq_iwi = 0;
        partial_state->irq_rtr = 0;
        partial_state->irq_plt = 0;
    #endif

    #ifdef CONFIG_SMP
		partial_state->irq_res = irq_stats(cpu)->irq_resched_count;
		partial_state->irq_cal = irq_stats(cpu)->irq_call_count;
		partial_state->irq_tlb = irq_stats(cpu)->irq_tlb_count;
    #else
        partial_state->irq_res = 0;
		partial_state->irq_cal = 0;
		partial_state->irq_tlb = 0;
    #endif

    #ifdef CONFIG_X86_THERMAL_VECTOR
		partial_state->irq_trm = irq_stats(cpu)->irq_thermal_count;
    #else
        partial_state->irq_trm = 0;
    #endif

    #ifdef CONFIG_X86_MCE_THRESHOLD
		partial_state->irq_thr = irq_stats(cpu)->irq_threshold_count;
    #else
        partial_state->irq_thr = 0;
    #endif

    #ifdef CONFIG_X86_MCE_AMD
		partial_state->irq_dfr = irq_stats(cpu)->irq_deferred_error_count;
    #else
        partial_state->irq_dfr = 0;
    #endif

    #ifdef CONFIG_X86_MCE
        //TODO: The mce_exception_count and mce_poll_count variables are
        //      not exported and are defined in arch/x86/kernel/cpu/mcheck/mce.c
        //      Will not track for now bit is tracked in arch_irq_stat_cpu_local
        //
        // partial_state->mce_exception = per_cpu(mce_exception_count, cpu);
		// partial_state->mce_poll = per_cpu(mce_poll_count, cpu);
        partial_state->mce_exception = 0;
		partial_state->mce_poll = 0;
    #else
        partial_state->mce_exception = 0;
		partial_state->mce_poll = 0;
    #endif

    #if IS_ENABLED(CONFIG_HYPERV) || defined(CONFIG_XEN)
        //TODO: system_vectors is not exported and will not be tracked for now
        // if (test_bit(HYPERVISOR_CALLBACK_VECTOR, system_vectors)) {
        //     partial_state->irq_hyp = irq_stats(cpu)->irq_hv_callback_count;
        // }else{
            partial_state->irq_hyp = 0;
        // }
    #else
        partial_state->irq_hyp = 0;
    #endif

    #ifdef CONFIG_HAVE_KVM
		partial_state->irq_pin = irq_stats(cpu)->kvm_posted_intr_ipis;
		partial_state->irq_npi = irq_stats(cpu)->kvm_posted_intr_nested_ipis;
		partial_state->irq_piw = irq_stats(cpu)->kvm_posted_intr_wakeup_ipis;
    #else
        partial_state->irq_pin = 0;
		partial_state->irq_npi = 0;
		partial_state->irq_piw = 0;
    #endif

    //TODO: Collect the sum of interrupts using the provided function.  This also sums up some of
    //      interrupts that were not exported and not tracked above.
    partial_state->arch_irq_stat_sum = arch_irq_stat_cpu_local(cpu);

    get_softirqs(cpu, &(partial_state->softirq_sum));
}

inline void copy_interrupt_report(struct partial_read_state* partial_state, struct sir_report* report){
        report->irq_std = partial_state->irq_std;       //Standard interrupts (not x86 specific)
        //x86 Specific Interrupts
        report->irq_nmi = partial_state->irq_nmi;       //NMI: Non-maskable interrupts             (__nmi_count)
        report->irq_loc = partial_state->irq_loc;       //LOC: Local timer interrupts              (apic_timer_irqs)
        report->irq_spu = partial_state->irq_spu;       //SPU: Spurious interrupts                 (irq_spurious_count)
        report->irq_pmi = partial_state->irq_pmi;       //PMI: Performance monitoring interrupts   (apic_perf_irqs)
        report->irq_iwi = partial_state->irq_iwi;       //IWI: IRQ work interrupts                 (apic_irq_work_irqs)
        report->irq_rtr = partial_state->irq_rtr;       //RTR: APIC ICR read retries               (icr_read_retry_count)
        report->irq_plt = partial_state->irq_plt;       //PLT: Platform interrupts                 (x86_platform_ipis)
        report->irq_res = partial_state->irq_res;       //RES: Rescheduling interrupts             (irq_resched_count)
        report->irq_cal = partial_state->irq_cal;       //CAL: Function call interrupts            (irq_call_count)
        report->irq_tlb = partial_state->irq_tlb;       //TLB: TLB shootdowns                      (irq_tlb_count)
        report->irq_trm = partial_state->irq_trm;       //TRM: Thermal event interrupts            (irq_thermal_count)
        report->irq_thr = partial_state->irq_thr;       //THR: Threshold APIC interrupts           (irq_threshold_count)
        report->irq_dfr = partial_state->irq_dfr;       //DFR: Deferred Error APIC interrupts      (irq_deferred_error_count)
        report->mce_exception = partial_state->mce_exception; //MCE: Machine check exceptions            (per_cpu(mce_exception_count, j))
        report->mce_poll = partial_state->mce_poll;     //MCP: Machine check polls                 (per_cpu(mce_poll_count, j))
        report->irq_hyp = partial_state->irq_hyp;       //HYP: Hypervisor callback interrupts      (irq_hv_callback_count)
        report->irq_pin = partial_state->irq_pin;       //PIN: Posted-interrupt notification event (kvm_posted_intr_ipis)
        report->irq_npi = partial_state->irq_npi;       //NPI: Nested posted-interrupt event       (kvm_posted_intr_nested_ipis)
        report->irq_piw = partial_state->irq_piw;       //PIW: Posted-interrupt wakeup event       (kvm_posted_intr_wakeup_ipis)
        report->arch_irq_stat_sum = partial_state->arch_irq_stat_sum;
        report->softirq_sum = partial_state->softirq_sum;
}

//As an alternative to using the char driver, the current interrupt
//can be accessed using a ioctl call.
//The value is returned to a pointer provided from the userspace in ARG
//This pointer must be to a 64 bit value.
//This method was used instead of directly using the return value
//due to how the kenel inspects the return value for negative numbers.
//This would require additional calls to IOCTL to determine the MSB
//as the MSB would need to be stripped from any return value
long sir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct partial_read_state* partial_state = (struct partial_read_state*) filp->private_data;
    unsigned long irq_flags = 0;
    long rtn_val = -EINVAL;
    int cpu;

    printkd(KERN_INFO "sir: ioctl cmd: %x arg: %lx\n", cmd, arg);

    mutex_lock(&(partial_state->lock));

    cpu = get_cpu();

    //Disable Interrupts to get accurate interrupt and softirq counts
    //This is based on the "Disabling all interrupts" section of Ch. 10 of LDD3
    local_irq_save(irq_flags);

    if(cmd == SIR_IOCTL_GET)
    {
        u64* rtn_ptr = (u64*) arg;
        SIR_INTERRUPT_TYPE irq_sum;
        partial_state->irq_std = kstat_cpu_irqs_sum(cpu);
        partial_state->arch_irq_stat_sum = arch_irq_stat_cpu_local(cpu);
        
        //Re-enable interrupts before copying results to user
        local_irq_restore(irq_flags);

        irq_sum = partial_state->irq_std + partial_state->arch_irq_stat_sum;
        copy_to_user(rtn_ptr, &(irq_sum), sizeof(irq_sum));
        printkd(KERN_INFO "sir: ioctl get (CPU %d): %lld\n", cpu, irq_sum);
        rtn_val = 0; //Success
    } else if(cmd == SIR_IOCTL_GET_DETAILED){
        struct sir_report* rtn_ptr = (struct sir_report*) arg;
        struct sir_report report;
        get_interrupts(cpu, partial_state);

        //Re-enable interrupts before copying results to user
        local_irq_restore(irq_flags);

        copy_interrupt_report(partial_state, &report);
        copy_to_user(rtn_ptr, &report, sizeof(report));
        printkd(KERN_INFO "sir: ioctl get detail (CPU %d)\n", cpu);
        rtn_val = 0; //Success
    }else {
        //Re-enable interrupts before copying results to user
        local_irq_restore(irq_flags);

        rtn_val = -ENOTTY;
        printkd(KERN_INFO "sir: ioctl default: %ld\n", rtn_val);
    }

    mutex_unlock(&(partial_state->lock));

    put_cpu();

    return rtn_val;
}

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

    #if !(CONFIG_X86)
        printk(KERN_WARNING "sir: This module only supports x86");
        sir_cleanup();
        return -EFAULT;
    #endif

    preempt_disable();
    arch_irq_stat_cpu_local = (typeof(arch_irq_stat_cpu_local)) kallsyms_lookup_name("arch_irq_stat_cpu");
    preempt_enable();
    if(arch_irq_stat_cpu_local == NULL)
    {
        printk(KERN_WARNING "sir: Unable to find arch_irq_stat_cpu");
        sir_cleanup();
        return -EFAULT;
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

