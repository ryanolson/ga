
#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <stdio.h>

#include <mpi.h>
#include "armci.h"
#include "parmci.h"

static int me;
static int nproc;


static long count_PARMCI_Acc = 0;
static long count_PARMCI_AccS = 0;
static long count_PARMCI_AccV = 0;
static long count_PARMCI_AllFence = 0;
static long count_PARMCI_Barrier = 0;
static long count_PARMCI_Create_mutexes = 0;
static long count_PARMCI_Destroy_mutexes = 0;
static long count_PARMCI_Fence = 0;
static long count_PARMCI_Finalize = 0;
static long count_PARMCI_Free = 0;
static long count_PARMCI_Free_local = 0;
static long count_PARMCI_Get = 0;
static long count_PARMCI_GetS = 0;
static long count_PARMCI_GetV = 0;
static long count_PARMCI_GetValueDouble = 0;
static long count_PARMCI_GetValueFloat = 0;
static long count_PARMCI_GetValueInt = 0;
static long count_PARMCI_GetValueLong = 0;
static long count_PARMCI_Init = 0;
static long count_PARMCI_Init_args = 0;
static long count_PARMCI_Initialized = 0;
static long count_PARMCI_Lock = 0;
static long count_PARMCI_Malloc = 0;
static long count_PARMCI_Malloc_local = 0;
static long count_PARMCI_Memat = 0;
static long count_PARMCI_Memctl = 0;
static long count_PARMCI_Memdt = 0;
static long count_PARMCI_Memget = 0;
static long count_PARMCI_NbAccS = 0;
static long count_PARMCI_NbAccV = 0;
static long count_PARMCI_NbGet = 0;
static long count_PARMCI_NbGetS = 0;
static long count_PARMCI_NbGetV = 0;
static long count_PARMCI_NbPut = 0;
static long count_PARMCI_NbPutS = 0;
static long count_PARMCI_NbPutV = 0;
static long count_PARMCI_NbPutValueDouble = 0;
static long count_PARMCI_NbPutValueFloat = 0;
static long count_PARMCI_NbPutValueInt = 0;
static long count_PARMCI_NbPutValueLong = 0;
static long count_PARMCI_Put = 0;
static long count_PARMCI_PutS = 0;
static long count_PARMCI_PutS_flag = 0;
static long count_PARMCI_PutS_flag_dir = 0;
static long count_PARMCI_PutV = 0;
static long count_PARMCI_PutValueDouble = 0;
static long count_PARMCI_PutValueFloat = 0;
static long count_PARMCI_PutValueInt = 0;
static long count_PARMCI_PutValueLong = 0;
static long count_PARMCI_Put_flag = 0;
static long count_PARMCI_Rmw = 0;
static long count_PARMCI_Test = 0;
static long count_PARMCI_Unlock = 0;
static long count_PARMCI_Wait = 0;
static long count_PARMCI_WaitAll = 0;
static long count_PARMCI_WaitProc = 0;
static long count_parmci_msg_barrier = 0;
static long count_parmci_msg_group_barrier = 0;
static long count_parmci_notify = 0;
static long count_parmci_notify_wait = 0;

static double time_PARMCI_Acc = 0;
static double time_PARMCI_AccS = 0;
static double time_PARMCI_AccV = 0;
static double time_PARMCI_AllFence = 0;
static double time_PARMCI_Barrier = 0;
static double time_PARMCI_Create_mutexes = 0;
static double time_PARMCI_Destroy_mutexes = 0;
static double time_PARMCI_Fence = 0;
static double time_PARMCI_Finalize = 0;
static double time_PARMCI_Free = 0;
static double time_PARMCI_Free_local = 0;
static double time_PARMCI_Get = 0;
static double time_PARMCI_GetS = 0;
static double time_PARMCI_GetV = 0;
static double time_PARMCI_GetValueDouble = 0;
static double time_PARMCI_GetValueFloat = 0;
static double time_PARMCI_GetValueInt = 0;
static double time_PARMCI_GetValueLong = 0;
static double time_PARMCI_Init = 0;
static double time_PARMCI_Init_args = 0;
static double time_PARMCI_Initialized = 0;
static double time_PARMCI_Lock = 0;
static double time_PARMCI_Malloc = 0;
static double time_PARMCI_Malloc_local = 0;
static double time_PARMCI_Memat = 0;
static double time_PARMCI_Memctl = 0;
static double time_PARMCI_Memdt = 0;
static double time_PARMCI_Memget = 0;
static double time_PARMCI_NbAccS = 0;
static double time_PARMCI_NbAccV = 0;
static double time_PARMCI_NbGet = 0;
static double time_PARMCI_NbGetS = 0;
static double time_PARMCI_NbGetV = 0;
static double time_PARMCI_NbPut = 0;
static double time_PARMCI_NbPutS = 0;
static double time_PARMCI_NbPutV = 0;
static double time_PARMCI_NbPutValueDouble = 0;
static double time_PARMCI_NbPutValueFloat = 0;
static double time_PARMCI_NbPutValueInt = 0;
static double time_PARMCI_NbPutValueLong = 0;
static double time_PARMCI_Put = 0;
static double time_PARMCI_PutS = 0;
static double time_PARMCI_PutS_flag = 0;
static double time_PARMCI_PutS_flag_dir = 0;
static double time_PARMCI_PutV = 0;
static double time_PARMCI_PutValueDouble = 0;
static double time_PARMCI_PutValueFloat = 0;
static double time_PARMCI_PutValueInt = 0;
static double time_PARMCI_PutValueLong = 0;
static double time_PARMCI_Put_flag = 0;
static double time_PARMCI_Rmw = 0;
static double time_PARMCI_Test = 0;
static double time_PARMCI_Unlock = 0;
static double time_PARMCI_Wait = 0;
static double time_PARMCI_WaitAll = 0;
static double time_PARMCI_WaitProc = 0;
static double time_parmci_msg_barrier = 0;
static double time_parmci_msg_group_barrier = 0;
static double time_parmci_notify = 0;
static double time_parmci_notify_wait = 0;


int ARMCI_Acc(int optype, void *scale, void *src, void *dst, int bytes, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Acc;
    local_start = MPI_Wtime();
    return_value = PARMCI_Acc(optype, scale, src, dst, bytes, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Acc += local_stop - local_start;
    return return_value;
}


int ARMCI_AccS(int optype, void *scale, void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_AccS;
    local_start = MPI_Wtime();
    return_value = PARMCI_AccS(optype, scale, src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_AccS += local_stop - local_start;
    return return_value;
}


int ARMCI_AccV(int op, void *scale, armci_giov_t *darr, int len, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_AccV;
    local_start = MPI_Wtime();
    return_value = PARMCI_AccV(op, scale, darr, len, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_AccV += local_stop - local_start;
    return return_value;
}


void ARMCI_AllFence()
{
    double local_start, local_stop;
    ++count_PARMCI_AllFence;
    local_start = MPI_Wtime();
    PARMCI_AllFence();
    local_stop = MPI_Wtime();
    time_PARMCI_AllFence += local_stop - local_start;
}


void ARMCI_Barrier()
{
    double local_start, local_stop;
    ++count_PARMCI_Barrier;
    local_start = MPI_Wtime();
    PARMCI_Barrier();
    local_stop = MPI_Wtime();
    time_PARMCI_Barrier += local_stop - local_start;
}


int ARMCI_Create_mutexes(int num)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Create_mutexes;
    local_start = MPI_Wtime();
    return_value = PARMCI_Create_mutexes(num);
    local_stop = MPI_Wtime();
    time_PARMCI_Create_mutexes += local_stop - local_start;
    return return_value;
}


int ARMCI_Destroy_mutexes()
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Destroy_mutexes;
    local_start = MPI_Wtime();
    return_value = PARMCI_Destroy_mutexes();
    local_stop = MPI_Wtime();
    time_PARMCI_Destroy_mutexes += local_stop - local_start;
    return return_value;
}


void ARMCI_Fence(int proc)
{
    double local_start, local_stop;
    ++count_PARMCI_Fence;
    local_start = MPI_Wtime();
    PARMCI_Fence(proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Fence += local_stop - local_start;
}


int ARMCI_Free(void *ptr)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Free;
    local_start = MPI_Wtime();
    return_value = PARMCI_Free(ptr);
    local_stop = MPI_Wtime();
    time_PARMCI_Free += local_stop - local_start;
    return return_value;
}


int ARMCI_Free_local(void *ptr)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Free_local;
    local_start = MPI_Wtime();
    return_value = PARMCI_Free_local(ptr);
    local_stop = MPI_Wtime();
    time_PARMCI_Free_local += local_stop - local_start;
    return return_value;
}


int ARMCI_Get(void *src, void *dst, int bytes, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Get;
    local_start = MPI_Wtime();
    return_value = PARMCI_Get(src, dst, bytes, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Get += local_stop - local_start;
    return return_value;
}


int ARMCI_GetS(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetS;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetS(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetS += local_stop - local_start;
    return return_value;
}


int ARMCI_GetV(armci_giov_t *darr, int len, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetV;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetV(darr, len, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetV += local_stop - local_start;
    return return_value;
}


double ARMCI_GetValueDouble(void *src, int proc)
{
    double return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetValueDouble;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetValueDouble(src, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetValueDouble += local_stop - local_start;
    return return_value;
}


float ARMCI_GetValueFloat(void *src, int proc)
{
    float return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetValueFloat;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetValueFloat(src, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetValueFloat += local_stop - local_start;
    return return_value;
}


int ARMCI_GetValueInt(void *src, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetValueInt;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetValueInt(src, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetValueInt += local_stop - local_start;
    return return_value;
}


long ARMCI_GetValueLong(void *src, int proc)
{
    long return_value;
    double local_start, local_stop;
    ++count_PARMCI_GetValueLong;
    local_start = MPI_Wtime();
    return_value = PARMCI_GetValueLong(src, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_GetValueLong += local_stop - local_start;
    return return_value;
}


int ARMCI_Initialized()
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Initialized;
    local_start = MPI_Wtime();
    return_value = PARMCI_Initialized();
    local_stop = MPI_Wtime();
    time_PARMCI_Initialized += local_stop - local_start;
    return return_value;
}


void ARMCI_Lock(int mutex, int proc)
{
    double local_start, local_stop;
    ++count_PARMCI_Lock;
    local_start = MPI_Wtime();
    PARMCI_Lock(mutex, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Lock += local_stop - local_start;
}


int ARMCI_Malloc(void **ptr_arr, armci_size_t bytes)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Malloc;
    local_start = MPI_Wtime();
    return_value = PARMCI_Malloc(ptr_arr, bytes);
    local_stop = MPI_Wtime();
    time_PARMCI_Malloc += local_stop - local_start;
    return return_value;
}


void* ARMCI_Malloc_local(armci_size_t bytes)
{
    void* return_value;
    double local_start, local_stop;
    ++count_PARMCI_Malloc_local;
    local_start = MPI_Wtime();
    return_value = PARMCI_Malloc_local(bytes);
    local_stop = MPI_Wtime();
    time_PARMCI_Malloc_local += local_stop - local_start;
    return return_value;
}


void* ARMCI_Memat(armci_meminfo_t *meminfo, long offset)
{
    void* return_value;
    double local_start, local_stop;
    ++count_PARMCI_Memat;
    local_start = MPI_Wtime();
    return_value = PARMCI_Memat(meminfo, offset);
    local_stop = MPI_Wtime();
    time_PARMCI_Memat += local_stop - local_start;
    return return_value;
}


void ARMCI_Memctl(armci_meminfo_t *meminfo)
{
    double local_start, local_stop;
    ++count_PARMCI_Memctl;
    local_start = MPI_Wtime();
    PARMCI_Memctl(meminfo);
    local_stop = MPI_Wtime();
    time_PARMCI_Memctl += local_stop - local_start;
}


void ARMCI_Memdt(armci_meminfo_t *meminfo, long offset)
{
    double local_start, local_stop;
    ++count_PARMCI_Memdt;
    local_start = MPI_Wtime();
    PARMCI_Memdt(meminfo, offset);
    local_stop = MPI_Wtime();
    time_PARMCI_Memdt += local_stop - local_start;
}


void ARMCI_Memget(size_t bytes, armci_meminfo_t *meminfo, int memflg)
{
    double local_start, local_stop;
    ++count_PARMCI_Memget;
    local_start = MPI_Wtime();
    PARMCI_Memget(bytes, meminfo, memflg);
    local_stop = MPI_Wtime();
    time_PARMCI_Memget += local_stop - local_start;
}


int ARMCI_NbAccS(int optype, void *scale, void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbAccS;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbAccS(optype, scale, src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbAccS += local_stop - local_start;
    return return_value;
}


int ARMCI_NbAccV(int op, void *scale, armci_giov_t *darr, int len, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbAccV;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbAccV(op, scale, darr, len, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbAccV += local_stop - local_start;
    return return_value;
}


int ARMCI_NbGet(void *src, void *dst, int bytes, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbGet;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbGet(src, dst, bytes, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbGet += local_stop - local_start;
    return return_value;
}


int ARMCI_NbGetS(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbGetS;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbGetS(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbGetS += local_stop - local_start;
    return return_value;
}


int ARMCI_NbGetV(armci_giov_t *darr, int len, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbGetV;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbGetV(darr, len, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbGetV += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPut(void *src, void *dst, int bytes, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPut;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPut(src, dst, bytes, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPut += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutS(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutS;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutS(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutS += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutV(armci_giov_t *darr, int len, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutV;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutV(darr, len, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutV += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutValueDouble(double src, void *dst, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutValueDouble;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutValueDouble(src, dst, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutValueDouble += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutValueFloat(float src, void *dst, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutValueFloat;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutValueFloat(src, dst, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutValueFloat += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutValueInt(int src, void *dst, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutValueInt;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutValueInt(src, dst, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutValueInt += local_stop - local_start;
    return return_value;
}


int ARMCI_NbPutValueLong(long src, void *dst, int proc, armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_NbPutValueLong;
    local_start = MPI_Wtime();
    return_value = PARMCI_NbPutValueLong(src, dst, proc, nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_NbPutValueLong += local_stop - local_start;
    return return_value;
}


int ARMCI_Put(void *src, void *dst, int bytes, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Put;
    local_start = MPI_Wtime();
    return_value = PARMCI_Put(src, dst, bytes, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Put += local_stop - local_start;
    return return_value;
}


int ARMCI_PutS(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutS;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutS(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutS += local_stop - local_start;
    return return_value;
}


int ARMCI_PutS_flag(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int *flag, int val, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutS_flag;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutS_flag(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, flag, val, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutS_flag += local_stop - local_start;
    return return_value;
}


int ARMCI_PutS_flag_dir(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int *flag, int val, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutS_flag_dir;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutS_flag_dir(src_ptr, src_stride_arr, dst_ptr, dst_stride_arr, count, stride_levels, flag, val, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutS_flag_dir += local_stop - local_start;
    return return_value;
}


int ARMCI_PutV(armci_giov_t *darr, int len, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutV;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutV(darr, len, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutV += local_stop - local_start;
    return return_value;
}


int ARMCI_PutValueDouble(double src, void *dst, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutValueDouble;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutValueDouble(src, dst, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutValueDouble += local_stop - local_start;
    return return_value;
}


int ARMCI_PutValueFloat(float src, void *dst, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutValueFloat;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutValueFloat(src, dst, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutValueFloat += local_stop - local_start;
    return return_value;
}


int ARMCI_PutValueInt(int src, void *dst, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutValueInt;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutValueInt(src, dst, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutValueInt += local_stop - local_start;
    return return_value;
}


int ARMCI_PutValueLong(long src, void *dst, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_PutValueLong;
    local_start = MPI_Wtime();
    return_value = PARMCI_PutValueLong(src, dst, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_PutValueLong += local_stop - local_start;
    return return_value;
}


int ARMCI_Put_flag(void *src, void *dst, int bytes, int *f, int v, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Put_flag;
    local_start = MPI_Wtime();
    return_value = PARMCI_Put_flag(src, dst, bytes, f, v, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Put_flag += local_stop - local_start;
    return return_value;
}


int ARMCI_Rmw(int op, void *ploc, void *prem, int extra, int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Rmw;
    local_start = MPI_Wtime();
    return_value = PARMCI_Rmw(op, ploc, prem, extra, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Rmw += local_stop - local_start;
    return return_value;
}


int ARMCI_Test(armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Test;
    local_start = MPI_Wtime();
    return_value = PARMCI_Test(nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_Test += local_stop - local_start;
    return return_value;
}


void ARMCI_Unlock(int mutex, int proc)
{
    double local_start, local_stop;
    ++count_PARMCI_Unlock;
    local_start = MPI_Wtime();
    PARMCI_Unlock(mutex, proc);
    local_stop = MPI_Wtime();
    time_PARMCI_Unlock += local_stop - local_start;
}


int ARMCI_Wait(armci_hdl_t *nb_handle)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_Wait;
    local_start = MPI_Wtime();
    return_value = PARMCI_Wait(nb_handle);
    local_stop = MPI_Wtime();
    time_PARMCI_Wait += local_stop - local_start;
    return return_value;
}


int ARMCI_WaitAll()
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_WaitAll;
    local_start = MPI_Wtime();
    return_value = PARMCI_WaitAll();
    local_stop = MPI_Wtime();
    time_PARMCI_WaitAll += local_stop - local_start;
    return return_value;
}


int ARMCI_WaitProc(int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_PARMCI_WaitProc;
    local_start = MPI_Wtime();
    return_value = PARMCI_WaitProc(proc);
    local_stop = MPI_Wtime();
    time_PARMCI_WaitProc += local_stop - local_start;
    return return_value;
}


void armci_msg_barrier()
{
    double local_start, local_stop;
    ++count_parmci_msg_barrier;
    local_start = MPI_Wtime();
    parmci_msg_barrier();
    local_stop = MPI_Wtime();
    time_parmci_msg_barrier += local_stop - local_start;
}


void armci_msg_group_barrier(ARMCI_Group *group)
{
    double local_start, local_stop;
    ++count_parmci_msg_group_barrier;
    local_start = MPI_Wtime();
    parmci_msg_group_barrier(group);
    local_stop = MPI_Wtime();
    time_parmci_msg_group_barrier += local_stop - local_start;
}


int armci_notify(int proc)
{
    int return_value;
    double local_start, local_stop;
    ++count_parmci_notify;
    local_start = MPI_Wtime();
    return_value = parmci_notify(proc);
    local_stop = MPI_Wtime();
    time_parmci_notify += local_stop - local_start;
    return return_value;
}


int armci_notify_wait(int proc, int *pval)
{
    int return_value;
    double local_start, local_stop;
    ++count_parmci_notify_wait;
    local_start = MPI_Wtime();
    return_value = parmci_notify_wait(proc, pval);
    local_stop = MPI_Wtime();
    time_parmci_notify_wait += local_stop - local_start;
    return return_value;
}

int ARMCI_Init()
{
    ++count_PARMCI_Init;
    PARMCI_Init();
    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
}

int ARMCI_Init_args(int *argc, char ***argv)
{
    ++count_PARMCI_Init_args;
    PARMCI_Init_args(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &me);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
}

void ARMCI_Finalize()
{
    ++count_PARMCI_Finalize;
    PARMCI_Finalize();
    /* don't dump info if terminate more than once */
    if (1 == count_PARMCI_Finalize) {

        long total_count = 0;
        double total_time = 0.0;

        MPI_Reduce(&count_PARMCI_Acc, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Acc, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Acc,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Acc, time_PARMCI_Acc, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_AccS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_AccS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_AccS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_AccS, time_PARMCI_AccS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_AccV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_AccV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_AccV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_AccV, time_PARMCI_AccV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_AllFence, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_AllFence, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_AllFence,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_AllFence, time_PARMCI_AllFence, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Barrier, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Barrier, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Barrier,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Barrier, time_PARMCI_Barrier, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Create_mutexes, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Create_mutexes, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Create_mutexes,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Create_mutexes, time_PARMCI_Create_mutexes, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Destroy_mutexes, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Destroy_mutexes, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Destroy_mutexes,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Destroy_mutexes, time_PARMCI_Destroy_mutexes, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Fence, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Fence, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Fence,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Fence, time_PARMCI_Fence, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Finalize, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Finalize, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Finalize,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Finalize, time_PARMCI_Finalize, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Free, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Free, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Free,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Free, time_PARMCI_Free, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Free_local, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Free_local, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Free_local,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Free_local, time_PARMCI_Free_local, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Get, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Get, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Get,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Get, time_PARMCI_Get, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetS, time_PARMCI_GetS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetV, time_PARMCI_GetV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetValueDouble, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetValueDouble, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetValueDouble,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetValueDouble, time_PARMCI_GetValueDouble, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetValueFloat, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetValueFloat, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetValueFloat,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetValueFloat, time_PARMCI_GetValueFloat, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetValueInt, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetValueInt, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetValueInt,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetValueInt, time_PARMCI_GetValueInt, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_GetValueLong, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_GetValueLong, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_GetValueLong,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_GetValueLong, time_PARMCI_GetValueLong, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Init, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Init, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Init,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Init, time_PARMCI_Init, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Init_args, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Init_args, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Init_args,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Init_args, time_PARMCI_Init_args, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Initialized, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Initialized, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Initialized,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Initialized, time_PARMCI_Initialized, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Lock, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Lock, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Lock,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Lock, time_PARMCI_Lock, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Malloc, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Malloc, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Malloc,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Malloc, time_PARMCI_Malloc, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Malloc_local, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Malloc_local, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Malloc_local,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Malloc_local, time_PARMCI_Malloc_local, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Memat, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Memat, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Memat,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Memat, time_PARMCI_Memat, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Memctl, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Memctl, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Memctl,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Memctl, time_PARMCI_Memctl, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Memdt, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Memdt, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Memdt,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Memdt, time_PARMCI_Memdt, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Memget, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Memget, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Memget,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Memget, time_PARMCI_Memget, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbAccS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbAccS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbAccS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbAccS, time_PARMCI_NbAccS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbAccV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbAccV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbAccV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbAccV, time_PARMCI_NbAccV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbGet, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbGet, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbGet,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbGet, time_PARMCI_NbGet, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbGetS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbGetS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbGetS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbGetS, time_PARMCI_NbGetS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbGetV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbGetV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbGetV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbGetV, time_PARMCI_NbGetV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPut, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPut, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPut,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPut, time_PARMCI_NbPut, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutS, time_PARMCI_NbPutS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutV, time_PARMCI_NbPutV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutValueDouble, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutValueDouble, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutValueDouble,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutValueDouble, time_PARMCI_NbPutValueDouble, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutValueFloat, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutValueFloat, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutValueFloat,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutValueFloat, time_PARMCI_NbPutValueFloat, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutValueInt, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutValueInt, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutValueInt,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutValueInt, time_PARMCI_NbPutValueInt, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_NbPutValueLong, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_NbPutValueLong, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_NbPutValueLong,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_NbPutValueLong, time_PARMCI_NbPutValueLong, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Put, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Put, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Put,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Put, time_PARMCI_Put, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutS, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutS, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutS,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutS, time_PARMCI_PutS, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutS_flag, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutS_flag, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutS_flag,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutS_flag, time_PARMCI_PutS_flag, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutS_flag_dir, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutS_flag_dir, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutS_flag_dir,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutS_flag_dir, time_PARMCI_PutS_flag_dir, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutV, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutV, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutV,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutV, time_PARMCI_PutV, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutValueDouble, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutValueDouble, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutValueDouble,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutValueDouble, time_PARMCI_PutValueDouble, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutValueFloat, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutValueFloat, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutValueFloat,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutValueFloat, time_PARMCI_PutValueFloat, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutValueInt, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutValueInt, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutValueInt,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutValueInt, time_PARMCI_PutValueInt, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_PutValueLong, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_PutValueLong, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_PutValueLong,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_PutValueLong, time_PARMCI_PutValueLong, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Put_flag, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Put_flag, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Put_flag,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Put_flag, time_PARMCI_Put_flag, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Rmw, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Rmw, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Rmw,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Rmw, time_PARMCI_Rmw, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Test, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Test, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Test,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Test, time_PARMCI_Test, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Unlock, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Unlock, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Unlock,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Unlock, time_PARMCI_Unlock, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_Wait, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_Wait, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_Wait,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_Wait, time_PARMCI_Wait, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_WaitAll, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_WaitAll, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_WaitAll,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_WaitAll, time_PARMCI_WaitAll, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_PARMCI_WaitProc, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_PARMCI_WaitProc, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("PARMCI_WaitProc,%ld,%lf,%ld,%lf,%lf\n",
                    count_PARMCI_WaitProc, time_PARMCI_WaitProc, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_parmci_msg_barrier, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_parmci_msg_barrier, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("parmci_msg_barrier,%ld,%lf,%ld,%lf,%lf\n",
                    count_parmci_msg_barrier, time_parmci_msg_barrier, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_parmci_msg_group_barrier, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_parmci_msg_group_barrier, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("parmci_msg_group_barrier,%ld,%lf,%ld,%lf,%lf\n",
                    count_parmci_msg_group_barrier, time_parmci_msg_group_barrier, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_parmci_notify, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_parmci_notify, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("parmci_notify,%ld,%lf,%ld,%lf,%lf\n",
                    count_parmci_notify, time_parmci_notify, total_count, total_time,
                    total_time/total_count);
        }

        MPI_Reduce(&count_parmci_notify_wait, &total_count, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&time_parmci_notify_wait, &total_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (me == 0 && total_count > 0) {
            printf("parmci_notify_wait,%ld,%lf,%ld,%lf,%lf\n",
                    count_parmci_notify_wait, time_parmci_notify_wait, total_count, total_time,
                    total_time/total_count);
        }

    }
}

