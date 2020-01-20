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