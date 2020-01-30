#ifndef _H_SIR
#define _H_SIR

#include <linux/ioctl.h>
//Userspace accessible parameters for the SIR driver

#define SIR_IOCTL_MAGIC 0xA5
#define SIR_IOCTL_GET _IOR(SIR_IOCTL_MAGIC, 0, long)
#define SIR_IOCTL_GET_DETAILED _IOR(SIR_IOCTL_MAGIC, 1, long)

#define SIR_INTERRUPT_TYPE u64

struct sir_report{
        SIR_INTERRUPT_TYPE irq_std;       //Standard interrupts (not x86 specific)
        //x86 Specific Interrupts
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

        SIR_INTERRUPT_TYPE arch_irq_stat_sum; //This is the sum of the above interrupts but is gathered using arch_irq_stat_cpu(unsigned int cpu)
                                              //This is collected because some variables are not exported but are read by this function
};

#endif