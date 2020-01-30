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
    
    #include "sir.h" //Get the numbers defined for IOCTL calls

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
        SIR_INTERRUPT_TYPE irq_std; //Standard interrupts (not x86 specific)

        //x86 Specific Interrupts (Including Local Timer)
        //These are stored in the irq_stat structure except for the mce ones.
        //The mce information is not stored in the irq_stat structure but are per_cpu.
        //defined in arch/x86/kernel/cpu/mcheck/mce.c and declared in arch/x86/include/asm/mce.h
        SIR_INTERRUPT_TYPE irq_nmi;       //NMI: Non-maskable interrupts             (__nmi_count)
        SIR_INTERRUPT_TYPE irq_loc;       //LOC: Local timer interrupts              (apic_timer_irqs)
        SIR_INTERRUPT_TYPE irq_spu;       //SPU: Spurious interrupts                 (irq_spurious_count)
        SIR_INTERRUPT_TYPE irq_pmi;       //PMI: Performance monitoring interrupts   (apic_perf_irqs)
        SIR_INTERRUPT_TYPE irq_iwi;       //IWI: IRQ work interrupts                 (apic_irq_work_irqs)
        SIR_INTERRUPT_TYPE irq_rtr;       //RTR: APIC ICR read retries               (icr_read_retry_count)
        SIR_INTERRUPT_TYPE irq_plt;       //PLT: Platform interrupts                 (x86_platform_ipis)
        SIR_INTERRUPT_TYPE irq_res;       //RES: Rescheduling interrupts             (irq_resched_count)
        SIR_INTERRUPT_TYPE irq_cal;       //CAL: Function call interrupts            (irq_call_count)
        SIR_INTERRUPT_TYPE irq_tlb;       //TLB: TLB shootdowns                      (irq_tlb_count)
        SIR_INTERRUPT_TYPE irq_trm;       //TRM: Thermal event interrupts            (irq_thermal_count)
        SIR_INTERRUPT_TYPE irq_thr;       //THR: Threshold APIC interrupts           (irq_threshold_count)
        SIR_INTERRUPT_TYPE irq_dfr;       //DFR: Deferred Error APIC interrupts      (irq_deferred_error_count)
        SIR_INTERRUPT_TYPE mce_exception; //MCE: Machine check exceptions            (per_cpu(mce_exception_count, j))
        SIR_INTERRUPT_TYPE mce_poll;      //MCP: Machine check polls                 (per_cpu(mce_poll_count, j))
        SIR_INTERRUPT_TYPE irq_hyp;       //HYP: Hypervisor callback interrupts      (irq_hv_callback_count)
        SIR_INTERRUPT_TYPE irq_pin;       //PIN: Posted-interrupt notification event (kvm_posted_intr_ipis)
        SIR_INTERRUPT_TYPE irq_npi;       //NPI: Nested posted-interrupt event       (kvm_posted_intr_nested_ipis)
        SIR_INTERRUPT_TYPE irq_piw;       //PIW: Posted-interrupt wakeup event       (kvm_posted_intr_wakeup_ipis)

        //Excluding: ERR and MIS entries since they appear to be global (in any case are atomic)

        SIR_INTERRUPT_TYPE arch_irq_stat_sum; //This is the sum of the above interrupts but is gathered using arch_irq_stat_cpu(unsigned int cpu)
                                              //This is collected because some variables are not exported but are read by this function

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