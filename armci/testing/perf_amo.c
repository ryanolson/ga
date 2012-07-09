/* Test Rmw Performance
 * The number of processes are increases from 2 to the number of 
 * processes present in the job */

#include "armci.h"
#include "message.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int me;
static int nproc;
static int size[] = {2,4,8,16,32,64,128,256,512,1024,0}; /* 0 is sentinal */

#define FETCH_AND_ADD  0
#define FETCH_AND_ADD_LONG  1
#define SWAP 2
#define SWAP_LONG 3

#define MAX_MESSAGE_SIZE 1024
#define MEDIUM_MESSAGE_SIZE 8192
#define ITER_SMALL 10000
#define ITER_LARGE 10000

#define WARMUP 20
static void fill_array(double *arr, int count, int which);
static void rmw_test(size_t buffer_size, int op);

double dclock()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return(tv.tv_sec * 1.0e6 + (double)tv.tv_usec);
}

int main(int argc, char **argv)
{
    int i;

    ARMCI_Init_args(&argc, &argv);
    me = armci_msg_me();
    nproc = armci_msg_nproc();

    /* This test only works for two processes */

    if (0 == me) {
        printf("#Processes     avg time (us)\n");
        printf("\n\n");
    }

    if (0 == me) {
        printf("#PNNL ARMCI Rmw-Fetch and Add Long Test\n");
        printf("\n\n");
    }
    rmw_test(MAX_MESSAGE_SIZE, FETCH_AND_ADD_LONG);

    if (0 == me)
        printf("\n\n");


    if (0 == me) {
        printf("#PNNL ARMCI Rmw-Fetch and Add Test\n");
    }
    rmw_test(MAX_MESSAGE_SIZE, FETCH_AND_ADD);
    if (0 == me)
        printf("\n\n");
    
   
    if (0 == me) {
        printf("#PNNL ARMCI Rmw-Swap Long Test\n");
    }
    rmw_test(MAX_MESSAGE_SIZE, SWAP_LONG);
   
    if (0 == me)
        printf("\n\n");
    
    
    if (0 == me) {
        printf("#PNNL ARMCI Rmw-Swap Test\n");
    }
    rmw_test(MAX_MESSAGE_SIZE, SWAP);
    
    if (0 == me)
        printf("\n\n");
    ARMCI_Finalize();
    
    armci_msg_finalize();

    return 0;
}


static void fill_array(double *arr, int count, int which)
{
    int i;

    for (i = 0; i < count; i++) {
        arr[i] = i * 8.23 + which * 2.89;
    }
}


static void rmw_test(size_t buffer_size, int op)
{
    void **dst_ptr;
    void **put_buf;
    void **get_buf;
    int i;
    double *times;
    double total_time = 0;

    dst_ptr = (void*)malloc(nproc * sizeof(void*));
    put_buf = (void*)malloc(nproc * sizeof(void*));
    get_buf = (void*)malloc(nproc * sizeof(void*));
    times = (double*)malloc(nproc * sizeof(double));
    ARMCI_Malloc(dst_ptr, buffer_size);
    ARMCI_Malloc(put_buf, buffer_size);
    ARMCI_Malloc(get_buf, buffer_size);

    /* initialize what we're putting */
    fill_array((double*)put_buf[me], buffer_size/sizeof(double), me);

    int msg_size = 2;

    /* All processes perform Rmw on process 0*/
    int dst = 0;
    int scale = 1;
    double t_start, t_end;

    int j;
    int iter = ITER_LARGE;

    int part_proc;

    for (part_proc = 2; part_proc <= nproc; part_proc *= 2) {
        if (me < part_proc) {
            for (j= 0; j < iter + WARMUP; ++j) {

                if (WARMUP == j) {
                    t_start = dclock();
                }

                switch (op) {
                    case FETCH_AND_ADD:
                        ARMCI_Rmw(ARMCI_FETCH_AND_ADD,
                                put_buf[me], dst_ptr[dst], 1, dst);
                        break;
                    case FETCH_AND_ADD_LONG:
                        ARMCI_Rmw(ARMCI_FETCH_AND_ADD_LONG,
                                put_buf[me], dst_ptr[dst], 1, dst);
                        break;
                    case SWAP:
                        ARMCI_Rmw(ARMCI_SWAP,
                                put_buf[me], dst_ptr[dst], 1, dst);
                        break;
                    case SWAP_LONG:
                        ARMCI_Rmw(ARMCI_SWAP_LONG,
                                put_buf[me], dst_ptr[dst], 1, dst);
                        break;
                    default:
                        ARMCI_Error("oops", 1);
                }
            }
        }
        ARMCI_Barrier();
        /* calculate total time and average time */
        t_end = dclock();


        if (0 == me) {
            printf("%5zu\t\t%6.2f\n",
                    part_proc,
                    ((t_end  - t_start))/iter);
        }
    }
    
    ARMCI_Free(dst_ptr[me]);
    ARMCI_Free(put_buf[me]);
    ARMCI_Free(get_buf[me]);
    free(dst_ptr);
    free(put_buf);
    free(get_buf);
    free(times);
}
