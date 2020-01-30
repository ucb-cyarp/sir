/**
 * A tool for testing sir from a C program
 * with a specific core affinity
 * 
 * This tool is most helpful when debug
 * printing in sir is enabled.  The results
 * can be correlated with the sir messages
 * in dmesg
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include "../module/sir.h"

#define SIR_TEST_ITERS 4

typedef struct 
{
    int cpu;
    FILE* file;
} thread_args_t;

void* read_sir_thread(void* arg){
    thread_args_t *args = (thread_args_t*) arg;
    uint64_t buf = 0;

    printf("char Driver:\n");
    for(int i = 0; i<SIR_TEST_ITERS; i++)
    {
        int size = fread(&buf, sizeof(buf), 1, args->file);
        if(size != 1){
            printf("Unexpected number of elements!\n");
            break;
        }

        printf("Interrupts: %ld\n", buf);
    }

    printf("ioctl Driver:\n");

    for(int i = 0; i<SIR_TEST_ITERS; i++)
    {
        uint64_t interrupts = 0;
        int status = ioctl(fileno(args->file), SIR_IOCTL_GET, &interrupts);
        if(status < 0){
            printf("ioctl error!\n");
            perror(NULL);
            break;
        }

        printf("Interrupts: %ld\n", interrupts);
    }

    printf("ioctl Detail Driver:\n");
    for(int i = 0; i<SIR_TEST_ITERS; i++)
    {
        printf("Snapshot\n");
        struct sir_report report;
        int status = ioctl(fileno(args->file), SIR_IOCTL_GET_DETAILED, &report);
        if(status < 0){
            printf("ioctl error!\n");
            perror(NULL);
            break;
        }
        printf("\tirq_std: %ld\n", report.irq_std);
        printf("\tirq_nmi: %ld\n", report.irq_nmi);
        printf("\tirq_loc: %ld\n", report.irq_loc);
        printf("\tirq_spu: %ld\n", report.irq_spu);
        printf("\tirq_pmi: %ld\n", report.irq_pmi);
        printf("\tirq_iwi: %ld\n", report.irq_iwi);
        printf("\tirq_rtr: %ld\n", report.irq_rtr);
        printf("\tirq_plt: %ld\n", report.irq_plt);
        printf("\tirq_res: %ld\n", report.irq_res);
        printf("\tirq_cal: %ld\n", report.irq_cal);
        printf("\tirq_tlb: %ld\n", report.irq_tlb);
        printf("\tirq_trm: %ld\n", report.irq_trm);
        printf("\tirq_thr: %ld\n", report.irq_thr);
        printf("\tirq_dfr: %ld\n", report.irq_dfr);
        printf("\tmce_exception: %ld\n", report.mce_exception);
        printf("\tmce_poll: %ld\n", report.mce_poll);
        printf("\tirq_hyp: %ld\n", report.irq_hyp);
        printf("\tirq_pin: %ld\n", report.irq_pin);
        printf("\tirq_npi: %ld\n", report.irq_npi);
        printf("\tirq_piw: %ld\n", report.irq_piw);
        printf("\tarch_irq_stat_sum: %ld\n", report.arch_irq_stat_sum);
        
        SIR_INTERRUPT_TYPE irq_sum = 0;
        irq_sum += report.irq_nmi;
        irq_sum += report.irq_loc;
        irq_sum += report.irq_spu;
        irq_sum += report.irq_pmi;
        irq_sum += report.irq_iwi;
        irq_sum += report.irq_rtr;
        irq_sum += report.irq_plt;
        irq_sum += report.irq_res;
        irq_sum += report.irq_cal;
        irq_sum += report.irq_tlb;
        irq_sum += report.irq_trm;
        irq_sum += report.irq_thr;
        irq_sum += report.irq_dfr;
        irq_sum += report.mce_exception;
        irq_sum += report.mce_poll;
        irq_sum += report.irq_hyp;
        irq_sum += report.irq_pin;
        irq_sum += report.irq_npi;
        irq_sum += report.irq_piw;

        printf("\tUnaccounted Interrupts: %ld\n", report.arch_irq_stat_sum-irq_sum);
    }

    return NULL;
}

void print_help()
{
    printf("Usage: sir_char_reader CPU\n");
    printf("\tCPU = CPU to run the test on\n");
}

int main(int argc, char* argv[]){
    //**** Parse Arguments ****
    if(argc < 2){
        printf("Error: No CPU Supplied\n\n");
        print_help();
        return 1;
    }

    int cpu = atoi(argv[1]);

    printf("Running on CPU: %d\n", cpu);

    //**** Setup the Thread ****
    FILE* sir_file = fopen("/dev/sir0", "r");

    thread_args_t args;
    args.cpu = cpu;
    args.file = sir_file;

    if(sir_file == NULL){
        perror("Unable to open /dev/sir0");
        return 1;
    }

    cpu_set_t cpu_set;
    pthread_t pthread;
    pthread_attr_t pthread_attr;

    int status = pthread_attr_init(&pthread_attr);
    if(status != 0){
        printf("Problem initializing pthread_attr\n");
        exit(1);
    }

    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);
    status = pthread_attr_setaffinity_np(&pthread_attr, sizeof(cpu_set_t), &cpu_set);
    if(status != 0){
        printf("Problem setting thread CPU affinity\n");
        exit(1);
    }

    //**** Start Thread ****
    status = pthread_create(&pthread, &pthread_attr, read_sir_thread, &args);
    if(status != 0){
        printf("Problem creating thread\n");
        exit(1);
    }

    void *rtn;
    status = pthread_join(pthread, &rtn);
    if(status != 0){
        printf("Problem joining thread\n");
        exit(1);
    }

    fclose(sir_file);

    free(rtn);

    return 0;
}