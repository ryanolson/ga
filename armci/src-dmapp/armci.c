/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set sw=4 ts=8 expandtab : */

#if HAVE_CONFIG_H
#   include "config.h"
#endif

//#define HAVE_DMAPP_QUEUE 1

/* C and/or system headers */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <hugetlbfs.h>
#include <errno.h>
#include <sched.h>

/* 3rd party headers */
#include <mpi.h>
#include <dmapp.h>

/* our headers */
#include "armci.h"
#include "armci_impl.h"
#include "groups.h"
#include "parmci.h"
#include "reg_cache.h"
#include "message.h"

#define DEBUG 0

#define ARMCI_MAX(a,b) (((a)>(b))?(a):(b))
#define ARMCI_MIN(a,b) (((a)<(b))?(a):(b))

#define ONEK    (1024UL)
#define FOURK   (4*ONEK)
#define ONEMEG  (ONEK*ONEK)

#ifndef unlikely
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif

#if HAVE_DMAPP_LOCK
// ARMCI_MAX_LOCKS mirrors the default DMAPP_MAX_LOCKS limit
// Larger values of ARMCI_MAX_LOCKS will require DMAPP_MAX_LOCKS be set at runtime.
// DMAPP_MAX_LOCKS has a maxium value of 1023
// Declare an extra lock (ARMCI_MAX_LOCKS) for the use_locks_on_[get|put] case
#define ARMCI_MAX_LOCKS 128
dmapp_lock_desc_t *lock_desc=NULL;
__thread dmapp_lock_handle_t lock_handle[ARMCI_MAX_LOCKS+1];
static int use_locks_on_get = 0;
static int use_locks_on_put = 0;
static int use_external_locks = 0;
#endif

#if HAVE_DMAPP_QUEUE
/* Declare the DMAPP Queue API functions weak in case we run on a system without the latest library */
extern dmapp_return_t
dmapp_queue_attach(int (*queue_cb)(void *context, void *data, uint32_t len, dmapp_pe_t rank),
           void *context, uint64_t flags, dmapp_queue_handle_t *queue_hndl)  __attribute__((weak));
extern dmapp_return_t dmapp_queue_detach(dmapp_queue_handle_t queue_hndl, uint64_t flags) __attribute__((weak));
extern dmapp_return_t dmapp_queue_put(dmapp_queue_handle_t  queue_hndl, void *source, uint64_t nelems,
                                      dmapp_type_t type, dmapp_pe_t target_pe, uint64_t flags) __attribute__((weak));
#pragma weak dmapp_queue_attach
#pragma weak dmapp_queue_detach
#pragma weak dmapp_queue_put

/* ARMCI_USE_REM_ACC is disabled by default. It should only be enabled
 * when running against a CLE5.0UP03 or newer install. It also requires a helper thread
 * per rank, and so will not perform well if all cores are scheduled
 */
static int armci_use_rem_acc = 0;
static long armci_rem_acc_contig_threshold = ARMCI_REM_ACC_CONTIG_THRESHOLD;
static long armci_rem_acc_strided_threshold = ARMCI_REM_ACC_STRIDED_THRESHOLD;
dmapp_queue_handle_t  armci_queue_hndl;
static uint32_t armci_dmapp_qdepth = 128; // DMAPP_QUEUE_DEFAULT_DEPTH;
static uint32_t armci_dmapp_qnelems = DMAPP_QUEUE_MAX_NELEMS; // DMAPP_QUEUE_DEFAULT_NELEMS;
static uint64_t armci_dmapp_qflags = DMAPP_QUEUE_ASYNC_PROGRESS;
#endif

/* exported state */
local_state l_state;
int armci_me=-1;
int armci_nproc=-1;

/* XPMEM support */
int armci_uses_shm = HAVE_XPMEM;
#define XPMEM_MIN_SIZE_ALIGN (sizeof(long))

MPI_Comm ARMCI_COMM_WORLD;

/* static state */
static int  initialized=0;                  /* for PARMCI_Initialized(), 0=false */
static int  total_outstanding=0;
static int  max_outstanding_nb=MAX_NB_OUTSTANDING;
static int  malloc_is_using_huge_pages=0;   /* from env var, 0=false */
static int  armci_is_using_huge_pages=0;    /* from env var, 0=false */
static long hugetlb_default_page_size=0;    /* from env var, in bytes */
static long sc_page_size=0;                 /* from sysconf, in bytes */
static long hugepagesize=0;                 /* from libhugetlbfs, in bytes */
static long armci_page_size=0;              /* page size consensus, in bytes */

/* static function declarations */
static void  check_envs(void);
static void  create_dmapp_locks(void);
static void  destroy_dmapp_locks(void);
static void  dmapp_alloc_buf(void);
static void  dmapp_free_buf(void);
static void  dmapp_initialize(void);
static void  dmapp_network_lock(int proc);
static void  dmapp_network_unlock(int proc);
static void  dmapp_terminate(void);
static void  increment_total_outstanding(void);
static void  my_free(void *ptr);
static void* my_malloc(size_t size);
static int   my_memalign(void **memptr, size_t alignment, size_t size);
static int   PARMCI_Get_nbi(void *src, void *dst, int bytes, int proc);
static int   PARMCI_Put_nbi(void *src, void *dst, int bytes, int proc);
static void* _PARMCI_Malloc_local(armci_size_t size, armci_mr_info_t *mr);

/* needed for complex accumulate */
typedef struct {
    double real;
    double imag;
} DoubleComplex;

typedef struct {
    float real;
    float imag;
} SingleComplex;

typedef struct {
  int sent;
  int received;
  int waited;
} armci_notify_t;

armci_notify_t **_armci_notify_arr;

static void* my_malloc(size_t size)
{
    void *memptr=NULL;

#if DEBUG
    if (0 == l_state.rank) {
        printf("my_malloc(%lu)\n", (long unsigned)size);
    }
#endif

#if HAVE_LIBHUGETLBFS
    if (malloc_is_using_huge_pages) {
        memptr = malloc(size);
    }
    else if (armci_is_using_huge_pages) {
        memptr = get_hugepage_region(size, GHR_DEFAULT);
    }
    else {
        memptr = malloc(size);
    }
#else
    memptr = malloc(size);
#endif

    /* postconditions */
    assert(memptr);

    return memptr;
}


static void my_free(void *ptr)
{
#if DEBUG
    if (0 == l_state.rank) {
        printf("my_free(%p)\n", ptr);
    }
#endif

#if HAVE_LIBHUGETLBFS
    if (malloc_is_using_huge_pages) {
        free(ptr);
    }
    else if (armci_is_using_huge_pages) {
        free_hugepage_region(ptr);
    }
    else {
        free(ptr);
    }
#else
    free(ptr);
#endif
}


static int my_memalign(void **memptr, size_t alignment, size_t size)
{
    int status = 0;

#if DEBUG
    if (0 == l_state.rank) {
        printf("my_memalign(%lu)\n", (long unsigned)size);
    }
#endif

    /* preconditions */
    assert(memptr);

#if HAVE_LIBHUGETLBFS
    if (malloc_is_using_huge_pages) {
        status = posix_memalign(memptr, alignment, size);
    }
    else if (armci_is_using_huge_pages) {
        *memptr = get_hugepage_region(size, GHR_DEFAULT);
    }
    else {
        status = posix_memalign(memptr, alignment, size);
    }
#else
    status = posix_memalign(memptr, alignment, size);
#endif

    /* postconditions */
    assert(*memptr);

    return status;
}


static void increment_total_outstanding(void)
{
    ++total_outstanding;

    if (total_outstanding == max_outstanding_nb) {
    dmapp_return_t status;

        status = dmapp_gsync_wait();
        assert(status == DMAPP_RC_SUCCESS);
        total_outstanding = 0;
    }
}


void PARMCI_Copy(void *src, void *dst, int n)
{
    unsigned long x = ((unsigned long) src | (unsigned long) dst) | n;

    if (armci_use_system_memcpy || (x & (XPMEM_MIN_SIZE_ALIGN-1)))
        memcpy(dst, src, n);
    else {
        _cray_armci_memcpy(dst, src, n);
    }
}

/* The blocking implementations should use blocking DMAPP calls */
int PARMCI_Put(void *src, void *dst, int bytes, int proc)
{
    PARMCI_Put_nbi(src, dst, bytes, proc);
    PARMCI_WaitProc(proc);
    return 0;
}


int PARMCI_Get(void *src, void *dst, int bytes, int proc)
{
    PARMCI_Get_nbi(src, dst, bytes, proc);
    PARMCI_WaitProc(proc);
    return 0;
}


/* The blocking implementations should use blocking DMAPP calls */
static int PARMCI_Put_nbi(void *src, void *dst, int bytes, int proc)
{
    int status = DMAPP_RC_SUCCESS;
    int nelems = bytes;
    int type = DMAPP_BYTE;
    int failure_observed = 0;
    reg_entry_t *dst_reg = NULL;

    /* Corner case */
    if (proc == l_state.rank) {
        PARMCI_Copy(src, dst, bytes);
        return status;
    }

    /* Find the dest memory region mapping */
    dst_reg = reg_cache_find(proc, dst, bytes);
    assert(dst_reg);

#if HAVE_XPMEM
    /* XPMEM optimisation */
    if (armci_uses_shm && ARMCI_Same_node(proc)) {
        unsigned long offset = (unsigned long) dst - (unsigned long)dst_reg->mr.seg.addr;
        void *xpmem_addr = (void *) ((unsigned long)dst_reg->mr.vaddr + offset);
        PARMCI_Copy(src, xpmem_addr, bytes);
        return status;
    }
#endif

    /* If the number of bytes is even, use Double word datatype,
     * DMAPP_BYTE performance is much worse */
    if (0 == bytes%16) {
        nelems = bytes/16;
        type = DMAPP_DQW;
    }
    else if (0 == bytes%8) {
        nelems = bytes/8;
        type = DMAPP_QW;
    }
    else if (0 == bytes%4) {
        nelems = bytes/4;
        type = DMAPP_DW;
    }

    status = dmapp_put_nbi(dst, &(dst_reg->mr.seg), proc, src, nelems, type);
    increment_total_outstanding();
    if (status != DMAPP_RC_SUCCESS) {
        failure_observed = 1;
    }

    /* Fallback */
    if (failure_observed) {
        PARMCI_WaitAll();
        assert(bytes <= l_state.put_buf_len);
        PARMCI_Copy(src, l_state.put_buf, bytes);
        status = dmapp_put_nbi(dst, &(dst_reg->mr.seg),
                               proc, l_state.put_buf, nelems, type);
        increment_total_outstanding();
        PARMCI_WaitAll();

        /* Fallback must work correctly */
        assert(status == DMAPP_RC_SUCCESS);
    }

    return status;
}


static int PARMCI_Get_nbi(void *src, void *dst, int bytes, int proc)
{
    int status = DMAPP_RC_SUCCESS;
    int nelems = bytes;
    int type = DMAPP_BYTE;
    int failure_observed = 0;
    reg_entry_t *src_reg = NULL;

    /* Corner case */
    if (proc == l_state.rank) {
        PARMCI_Copy(src, dst, bytes);
        return status;
    }

    /* Find the source memory region mapping */
    src_reg = reg_cache_find(proc, src, bytes);
    assert(src_reg);

#if HAVE_XPMEM
    /* XPMEM optimisation */
    if (armci_uses_shm && ARMCI_Same_node(proc)) {
        unsigned long offset = (unsigned long) src - (unsigned long)src_reg->mr.seg.addr;
        void *xpmem_addr = (void *) ((unsigned long) src_reg->mr.vaddr + offset);
        PARMCI_Copy(xpmem_addr, dst, bytes);
        return status;
    }
#endif

    /* If the number of bytes is even, use Double word datatype,
     * DMAPP_BYTE performance is much worse */
    if (0 == bytes%16) {
        nelems = bytes/16;
        type = DMAPP_DQW;
    }
    else if (0 == bytes%8) {
        nelems = bytes/8;
        type = DMAPP_QW;
    }
    else if (0 == bytes%4) {
        nelems = bytes/4;
        type = DMAPP_DW;
    }

    status = dmapp_get_nbi(dst, src, &(src_reg->mr.seg),
                           proc, nelems, type);
    increment_total_outstanding();
    if (status != DMAPP_RC_SUCCESS) {
        failure_observed = 1;
    }

    /* Fallback */
    if (failure_observed) {
        PARMCI_WaitAll();
        assert(bytes <= l_state.get_buf_len);
        status = dmapp_get_nbi(l_state.get_buf, src, &(src_reg->mr.seg),
                               proc, nelems, type);
        increment_total_outstanding();
        PARMCI_WaitAll();
        PARMCI_Copy(l_state.get_buf, dst, bytes);

        /* Fallback must work correctly */
        assert(status == DMAPP_RC_SUCCESS);
    }

    return status;
}

void PARMCI_Lock(int mutex, int proc)
{
#if HAVE_DMAPP_LOCK
    int dmapp_status;
    if(unlikely(mutex < 0) || unlikely(mutex >= ARMCI_MAX_LOCKS))
       ARMCI_Error("Runtime Error: armci_lock mutex out of range\n",911);
    dmapp_status = dmapp_lock_acquire( &lock_desc[mutex], &(l_state.job.sheap_seg),
                                        proc, 0, &lock_handle[mutex]);
    assert(dmapp_status == DMAPP_RC_SUCCESS);
#else
#error ARMCI_Lock requires HAVE_DMAPP_LOCK
#endif
}

void PARMCI_Unlock(int mutex, int proc)
{
#if HAVE_DMAPP_LOCK
    int dmapp_status;
    if(unlikely(mutex < 0) || unlikely(mutex >= ARMCI_MAX_LOCKS))
       ARMCI_Error("Runtime Error: armci_unlock mutex out of range\n",911);
    dmapp_status = dmapp_lock_release( lock_handle[mutex], 0);
    assert(dmapp_status == DMAPP_RC_SUCCESS);
#else
#error ARMCI_Unlock requires HAVE_DMAPP_LOCK
#endif
}

static void dmapp_network_lock(int proc)
{
    int dmapp_status;
    if(use_external_locks) return;

#if HAVE_DMAPP_LOCK
    dmapp_status = dmapp_lock_acquire( &lock_desc[ARMCI_MAX_LOCKS],
                                       &(l_state.job.sheap_seg), proc, 0,
                                       &lock_handle[ARMCI_MAX_LOCKS] );
    assert(dmapp_status == DMAPP_RC_SUCCESS);
#else
    reg_entry_t *dst_reg= reg_cache_find(proc,
            l_state.atomic_lock_buf[proc], sizeof(long));

    assert(dst_reg);

    do {
        dmapp_status = dmapp_acswap_qw(l_state.local_lock_buf,
                l_state.atomic_lock_buf[proc],
                &(dst_reg->mr.seg),
                proc, 0, l_state.rank + 1);

        assert(dmapp_status == DMAPP_RC_SUCCESS);
    }
    while(*(l_state.local_lock_buf) != 0);
#endif
}


static void dmapp_network_unlock(int proc)
{
    int dmapp_status;
    if(use_external_locks) return;

# if HAVE_DMAPP_LOCK
    dmapp_status = dmapp_lock_release( lock_handle[ARMCI_MAX_LOCKS], 0 );
    assert(dmapp_status == DMAPP_RC_SUCCESS);
#else
    reg_entry_t *dst_reg= reg_cache_find(proc,
            l_state.atomic_lock_buf[proc], sizeof(long));

    assert(dst_reg);

    do {
        dmapp_status = dmapp_acswap_qw(l_state.local_lock_buf,
                                       l_state.atomic_lock_buf[proc],
                                       &(dst_reg->mr.seg),
                                       proc, l_state.rank + 1, 0);
        assert(dmapp_status == DMAPP_RC_SUCCESS);
    } while (*(l_state.local_lock_buf) != l_state.rank + 1);
#endif
}


int PARMCI_PutS(void *src_ptr, int src_stride_ar[/*stride_levels*/],
                void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                int count[/*stride_levels+1*/], int stride_levels, int proc)
{
    int i, j;
    long src_idx, dst_idx;  /* index offset of current block position to ptr */
    int n1dim;  /* number of 1 dim block */
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];
    MPI_Status status;
    int dmapp_status;
    int k = 0, rc;

    /* number of n-element of the first dimension */
    n1dim = 1;
    for(i=1; i<=stride_levels; i++) {
        n1dim *= count[i];
    }

    /* calculate the destination indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    dst_bvalue[0] = 0; dst_bvalue[1] = 0; dst_bunit[0] = 1; dst_bunit[1] = 1;

    for(i=2; i<=stride_levels; i++) {
        src_bvalue[i] = 0;
        dst_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
        dst_bunit[i] = dst_bunit[i-1] * count[i-1];
    }

#if HAVE_DMAPP_LOCK
    if(use_locks_on_put) dmapp_network_lock(proc);
#endif

    /* index mangling */
    for(i=0; i<n1dim; i++) {
        src_idx = 0;
        dst_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            src_idx += src_bvalue[j] * src_stride_ar[j-1];
            if((i+1) % src_bunit[j] == 0) {
                src_bvalue[j]++;
            }
            if(src_bvalue[j] > (count[j]-1)) {
                src_bvalue[j] = 0;
            }
        }

        for(j=1; j<=stride_levels; j++) {
            dst_idx += dst_bvalue[j] * dst_stride_ar[j-1];
            if((i+1) % dst_bunit[j] == 0) {
                dst_bvalue[j]++;
            }
            if(dst_bvalue[j] > (count[j]-1)) {
                dst_bvalue[j] = 0;
            }
        }

        PARMCI_Put_nbi((char *)src_ptr + src_idx,
                (char *)dst_ptr + dst_idx, count[0], proc);
    }

    PARMCI_WaitProc(proc);

#if HAVE_DMAPP_LOCK
    if(use_locks_on_put) dmapp_network_unlock(proc);
#endif

    return 0;
}


int PARMCI_GetS(void *src_ptr, int src_stride_ar[/*stride_levels*/],
                void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                int count[/*stride_levels+1*/], int stride_levels, int proc)
{
    int i, j;
    long src_idx, dst_idx;  /* index offset of current block position to ptr */
    int n1dim;  /* number of 1 dim block */
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];
    MPI_Status status;
    int dmapp_status;
    int k = 0;

    /* number of n-element of the first dimension */
    n1dim = 1;
    for(i=1; i<=stride_levels; i++) {
        n1dim *= count[i];
    }

    /* calculate the destination indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    dst_bvalue[0] = 0; dst_bvalue[1] = 0; dst_bunit[0] = 1; dst_bunit[1] = 1;

    for(i=2; i<=stride_levels; i++) {
        src_bvalue[i] = 0;
        dst_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
        dst_bunit[i] = dst_bunit[i-1] * count[i-1];
    }

#if HAVE_DMAPP_LOCK
    if(use_locks_on_get) dmapp_network_lock(proc);
#endif

    for(i=0; i<n1dim; i++) {

        src_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            src_idx += src_bvalue[j] * src_stride_ar[j-1];
            if((i+1) % src_bunit[j] == 0) {
                src_bvalue[j]++;
            }
            if(src_bvalue[j] > (count[j]-1)) {
                src_bvalue[j] = 0;
            }
        }

        dst_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            dst_idx += dst_bvalue[j] * dst_stride_ar[j-1];
            if((i+1) % dst_bunit[j] == 0) {
                dst_bvalue[j]++;
            }
            if(dst_bvalue[j] > (count[j]-1)) {
                dst_bvalue[j] = 0;
            }
        }

        PARMCI_Get_nbi((char *)src_ptr + src_idx,
                (char *)dst_ptr + dst_idx, count[0], proc);
    }

    PARMCI_WaitProc(proc);

#if HAVE_DMAPP_LOCK
    if(use_locks_on_get) dmapp_network_unlock(proc);
#endif

    return 0;
}


int PARMCI_Acc(int datatype, void *scale,
               void *src_ptr,
               void *dst_ptr,
               int bytes, int proc)
{

    PARMCI_AccS(datatype, scale, src_ptr, NULL, dst_ptr,
            NULL, &bytes, 0, proc);
    return 0;
}

/* Perform the scaled Accumulate operation on src/dst array leaving result in dst array */
static inline void
do_acc(void *src, void *dst, int datatype, void *scale, int count)
{
#if 0
    printf("%d : do_acc src %p dst %p datatype %d count %d\n",
           l_state.rank, src, dst, datatype, count);
#endif
#define EQ_ONE_REG(A) ((A) == 1.0)
#define EQ_ONE_CPL(A) ((A).real == 1.0 && (A).imag == 0.0)
#define IADD_REG(A,B) (A) += (B)
#define IADD_CPL(A,B) (A).real += (B).real; (A).imag += (B).imag
#define IADD_SCALE_REG(A,B,C) (A) += (B) * (C)
#define IADD_SCALE_CPL(A,B,C) (A).real += ((B).real*(C).real) - ((B).imag*(C).imag);\
                              (A).imag += ((B).real*(C).imag) + ((B).imag*(C).real);
#define ACC(WHICH, ARMCI_TYPE, C_TYPE)                                      \
        if (datatype == ARMCI_TYPE) {                                       \
            int m;                                                          \
            int m_lim = count/sizeof(C_TYPE);                               \
            C_TYPE *iterator = (C_TYPE *)dst;                               \
            C_TYPE *value = (C_TYPE *)src;                                  \
            C_TYPE calc_scale = *(C_TYPE *)scale;                           \
            if (EQ_ONE_##WHICH(calc_scale)) {                               \
                for (m = 0 ; m < m_lim; ++m) {                              \
                    IADD_##WHICH(iterator[m], value[m]);                    \
                }                                                           \
            }                                                               \
            else {                                                          \
                for (m = 0 ; m < m_lim; ++m) {                              \
                    IADD_SCALE_##WHICH(iterator[m], value[m], calc_scale);  \
                }                                                           \
            }                                                               \
        } else
        ACC(REG, ARMCI_ACC_DBL, double)
        ACC(REG, ARMCI_ACC_FLT, float)
        ACC(REG, ARMCI_ACC_INT, int)
        ACC(REG, ARMCI_ACC_LNG, long)
        ACC(CPL, ARMCI_ACC_DCP, DoubleComplex)
        ACC(CPL, ARMCI_ACC_CPL, SingleComplex)
        {
            assert(0);
        }
#undef ACC
#undef EQ_ONE_REG
#undef EQ_ONE_CPL
#undef IADD_REG
#undef IADD_CPL
#undef IADD_SCALE_REG
#undef IADD_SCALE_CPL

        return;
}

typedef struct acc_buffer {
    char * get_buf;
    long src_idx;
    long dst_idx;
    long bytes;
} acc_buffer_t;

#define PIPEDEPTH     128
#define CHUNK_SIZE_1D (64*ONEK)

static int do_AccS(int datatype, void *scale,
                   void *src_ptr, int src_stride_ar[/*stride_levels*/],
                   void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                   int count[/*stride_levels+1*/], int stride_levels, int proc)
{
    int i, j;
    long src_idx, dst_idx;  /* index offset of current block position to ptr */
    int n1dim;  /* number of 1 dim block */
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];
    MPI_Status status;
    int dmapp_status = DMAPP_RC_SUCCESS;
    int sizetogetput;
    char *get_buf = NULL;
    long k = 0;
    int buffered_1d = 0;
    acc_buffer_t buffers[3];
    acc_buffer_t * get_buffer = NULL;
    acc_buffer_t * acc_buffer = NULL;
    acc_buffer_t * put_buffer = NULL;

#if HAVE_DMAPP_LOCK
    int lock_on_get = use_locks_on_get;
    int lock_on_put = use_locks_on_put;
    use_locks_on_get = 0;
    use_locks_on_put = 0;
#endif

    /* number of n-element of the first dimension */
    n1dim = 1;
    for(i=1; i<=stride_levels; i++)
        n1dim *= count[i];

    /* calculate the destination indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    dst_bvalue[0] = 0; dst_bvalue[1] = 0; dst_bunit[0] = 1; dst_bunit[1] = 1;

    for(i=2; i<=stride_levels; i++)
    {
        src_bvalue[i] = 0;
        dst_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
        dst_bunit[i] = dst_bunit[i-1] * count[i-1];
    }

    sizetogetput = count[0];

    if (sizetogetput*3 <= l_state.acc_buf_len) {
        get_buf = l_state.acc_buf;
        buffers[0].get_buf = get_buf;
        buffers[1].get_buf = &get_buf[sizetogetput];
        buffers[2].get_buf = &get_buf[sizetogetput*2];
    }
    else {
#if HAVE_XPMEM
        /* XPMEM optimisation */
        if (!armci_uses_shm || !ARMCI_Same_node(proc))
#endif
        {
            // allocate the temporary buffer
            get_buf = (char *)my_malloc(sizeof(char) * sizetogetput * 3);
            buffers[0].get_buf = get_buf;
            buffers[1].get_buf = &get_buf[sizetogetput];
            buffers[2].get_buf = &get_buf[sizetogetput*2];
            assert(get_buf);
        }
    }

    if(n1dim == 1 && sizetogetput > CHUNK_SIZE_1D) {
#if HAVE_XPMEM
       /* XPMEM optimisation */
       if (!armci_uses_shm || !ARMCI_Same_node(proc))
#endif
       {
           n1dim = sizetogetput / CHUNK_SIZE_1D;
           if(sizetogetput % CHUNK_SIZE_1D) n1dim++;
           sizetogetput = CHUNK_SIZE_1D;
           buffered_1d = 1;
       }
    }

    // grab the atomic lock
    dmapp_network_lock(proc);

    for(i=0; i<n1dim; i++) {

        if(acc_buffer) put_buffer = acc_buffer;
        if(get_buffer) acc_buffer = get_buffer;

        src_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            src_idx += src_bvalue[j] * src_stride_ar[j-1];
            if((i+1) % src_bunit[j] == 0) {
                src_bvalue[j]++;
            }
            if(src_bvalue[j] > (count[j]-1)) {
                src_bvalue[j] = 0;
            }
        }

        dst_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            dst_idx += dst_bvalue[j] * dst_stride_ar[j-1];
            if((i+1) % dst_bunit[j] == 0) {
                dst_bvalue[j]++;
            }
            if(dst_bvalue[j] > (count[j]-1)) {
                dst_bvalue[j] = 0;
            }
        }

        if(buffered_1d == 1) {
           dst_idx = src_idx = i*CHUNK_SIZE_1D;
           // on last iteration resize sizetogetput
           if(i+1 == n1dim) sizetogetput = count[0] - i*CHUNK_SIZE_1D;
        }

        get_buffer = &buffers[i % 3];
        get_buffer->src_idx = src_idx;
        get_buffer->dst_idx = dst_idx;
        get_buffer->bytes = sizetogetput;

#if HAVE_XPMEM
        /* XPMEM optimisation */
        if (armci_uses_shm && ARMCI_Same_node(proc)) {
            reg_entry_t *dst_reg;
            unsigned long offset;

            get_buffer->get_buf = (char *)dst_ptr + get_buffer->dst_idx;

            if (l_state.rank != proc) {
                /* Find the dest memory region mapping */
                dst_reg = reg_cache_find(proc, get_buffer->get_buf, get_buffer->bytes);
                assert(dst_reg);
                offset = (unsigned long) get_buffer->get_buf - (unsigned long)dst_reg->mr.seg.addr;
                get_buffer->get_buf = (void *) ((unsigned long)dst_reg->mr.vaddr + offset);
            }
        }
        else
#endif
        {
            // Get the remote data in a temp buffer
            PARMCI_Get_nbi((char *)dst_ptr + get_buffer->dst_idx, get_buffer->get_buf, get_buffer->bytes, proc);
        }

        if(put_buffer) {
#if HAVE_XPMEM
           /* XPMEM optimisation */
           if (!armci_uses_shm || !ARMCI_Same_node(proc))
#endif
              // Write back to remote source
              PARMCI_Put_nbi(put_buffer->get_buf, (char *)dst_ptr + put_buffer->dst_idx, put_buffer->bytes, proc);
        }

        if (acc_buffer)
            /* Now perform the Accumulate operation leaving the result in acc_buffer */
            do_acc((char *)src_ptr + acc_buffer->src_idx, acc_buffer->get_buf, datatype, scale, acc_buffer->bytes);

        PARMCI_WaitProc(proc);
    }

    // Clean up remaining get and acc buffers
    while(get_buffer || acc_buffer) {
       if(acc_buffer) put_buffer = acc_buffer;  acc_buffer = NULL;
       if(get_buffer) acc_buffer = get_buffer;  get_buffer = NULL;
       if(put_buffer) {
#if HAVE_XPMEM
           /* XPMEM optimisation */
           if (!armci_uses_shm || !ARMCI_Same_node(proc))
#endif
              // Write back to remote source
              PARMCI_Put_nbi(put_buffer->get_buf, (char *)dst_ptr + put_buffer->dst_idx, put_buffer->bytes, proc);
       }

       if (acc_buffer)
           /* Now perform the Accumulate operation leaving the result in acc_buffer */
           do_acc((char *)src_ptr + acc_buffer->src_idx, acc_buffer->get_buf, datatype, scale, acc_buffer->bytes);

       PARMCI_WaitProc(proc);
    }

    // ungrab the lock
    dmapp_network_unlock(proc);

#if HAVE_DMAPP_LOCK
    use_locks_on_get = lock_on_get;
    use_locks_on_put = lock_on_put;
#endif

#if HAVE_XPMEM
    /* XPMEM optimisation */
    if (!armci_uses_shm || !ARMCI_Same_node(proc))
#endif
        // free temp buffer
        if (get_buf != l_state.acc_buf)
            my_free(get_buf);

    return 0;
}

static int do_remote_AccS(int datatype, void *scale,
                         void *src_ptr, /* src_ptr is contiguous */
                            void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                         int count[/*stride_levels+1*/], int stride_levels)
{
    int i, j;
    long src_idx, dst_idx;  /* index offset of current block position to ptr */
    int n1dim;  /* number of 1 dim block */
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];

    /* number of n-element of the first dimension */
    n1dim = 1;
    for(i=1; i<=stride_levels; i++) n1dim *= count[i];

    // printf("%d: [msgq thread] n1dim=%d\n", l_state.rank, n1dim);
    // printf("%d: [msgq thread] stride1=%d\n", l_state.rank, count[0]);

    /* calculate the destination indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    dst_bvalue[0] = 0; dst_bvalue[1] = 0; dst_bunit[0] = 1; dst_bunit[1] = 1;

    for(i=2; i<=stride_levels; i++)
    {
        src_bvalue[i] = 0;
        dst_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
        dst_bunit[i] = dst_bunit[i-1] * count[i-1];
    }

    // grab the atomic lock
    // printf("%d: [msgq thread] acquire lock\n", l_state.rank);
    dmapp_network_lock(l_state.rank);
    // printf("%d: [msgq thread] lock acquired\n", l_state.rank);

    // accumulate to each contiguous segment
    for(i=0; i<n1dim; i++) {

        // calculate src_idx
        src_idx = count[0]*i;

        // calculate dst_idx
        dst_idx = 0;
        for(j=1; j<=stride_levels; j++) {

            dst_idx += dst_bvalue[j] * dst_stride_ar[j-1];
            if((i+1) % dst_bunit[j] == 0) {
                dst_bvalue[j]++;
            }
            if(dst_bvalue[j] > (count[j]-1)) {
                dst_bvalue[j] = 0;
            }
        }

        // contiguous accumulate
        do_acc((char *)src_ptr + src_idx, (char *)dst_ptr + dst_idx, datatype,
               scale, count[0]);
    }

    // release the lock
    // printf("%d: [msgq thread] release lock\n", l_state.rank);
    dmapp_network_unlock(l_state.rank);
    // printf("%d: [msgq thread] lock released\n", l_state.rank);

    return 0;
}


static uint64_t rem_acc_header_size(int datatype, int stride_levels) {
    uint64_t msg=0, slen;

    // src_ptr
    msg += sizeof(void*);

    // src_reg
    msg += sizeof(dmapp_seg_desc_t);

    // dst_ptr
    msg += sizeof(void*);

    // stride_levels
    msg += sizeof(int);

    // dst_stride_ar
    msg += sizeof(int)*stride_levels;

    // count
    msg += sizeof(int)*(stride_levels+1);

    // datatype
    msg += sizeof(int);

    // scale
    msg += 2*sizeof(double);

    return msg;
}

static void rem_acc_pack_header(void *header, int datatype, void *scale,
                        void *src_ptr, reg_entry_t *src_reg,
                        void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                        int count[/*stride_levels+1*/], int stride_levels)
{
    char *msg, *buf;
    int i, slen;

    msg = buf = (char *)header;

    *(void **)msg = src_ptr;
    msg += sizeof(void*);

    if(src_reg) { *(dmapp_seg_desc_t *)msg = src_reg->mr.seg; }
    msg += sizeof(dmapp_seg_desc_t);

    *(void **)msg = dst_ptr;
    msg += sizeof(void*);

    *(int*)msg = stride_levels;
    msg += sizeof(int);

    for (i=0; i < stride_levels; i++) {
        ((int *)msg)[i] = dst_stride_ar[i];
    }
    msg += sizeof(int)*stride_levels;

    for (i=0; i < stride_levels+1; i++) {
        ((int*)msg)[i] = count[i];
    }
    msg += sizeof(int)*(stride_levels+1);

    *(int *)msg = datatype;
    msg += sizeof(int);

    /* pack scale */
    switch(datatype){
    case ARMCI_ACC_INT:
        *(int*)msg = *(int*)scale; slen=sizeof(int); break;
    case ARMCI_ACC_DCP:
        ((double*)msg)[0] = ((double*)scale)[0];
        ((double*)msg)[1] = ((double*)scale)[1];
        slen=2*sizeof(double); break;
    case ARMCI_ACC_DBL:
        *(double*)msg = *(double*)scale; slen=sizeof(double); break;
    case ARMCI_ACC_CPL:
        ((float*)msg)[0] = ((float*)scale)[0];
        ((float*)msg)[1] = ((float*)scale)[1];
        slen=2*sizeof(float); break;
    case ARMCI_ACC_FLT:
        *(float*)msg = *(float*)scale; slen=sizeof(float); break;
    default: slen=0;
    }
    msg += slen;
}

/*
   Copies the strided data from src_ptr to a contiguous buffer.
*/
static void rem_acc_pack_data(void *buffer,
                       void *src_ptr, int src_stride_ar[/*stride_levels*/],
                       int count[/*stride_levels+1*/], int stride_levels)
{
    int i, j, n1dim = 1;
    size_t stride1_size = count[0];
    char *src = (char *) src_ptr;
    char *buf = (char *) buffer;
    uint64_t src_idx;
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];

    /* number of n-element of the first dimension */
    for(i=1; i<=stride_levels; i++) { n1dim *= count[i]; }

    /* calculate the offset indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    for(i=2; i<=stride_levels; i++) {
        src_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
    }

    /* pack the source data contiguously in the destination buffer */
    for(i=0; i<n1dim; i++) {
        src_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            src_idx += src_bvalue[j] * src_stride_ar[j-1];
            if((i+1) % src_bunit[j] == 0) {
                src_bvalue[j]++;
            }
            if(src_bvalue[j] > (count[j]-1)) {
                src_bvalue[j] = 0;
            }
        }
        memcpy(buf, src_ptr + src_idx, stride1_size);
        buf += stride1_size;
    }

    // printf("%d: packed strided src_ptr => contiguous buffer @ %x", l_state.rank, buffer);
}


#if HAVE_DMAPP_QUEUE
typedef struct rem_acc_desc {
    long src_idx;
    long dst_idx;
    long sizetoget;
    dmapp_syncid_handle_t syncid;
} rem_acc_desc_t;

static int do_remote_AccS_old(int datatype, void *scale,
                          void *src_ptr, int src_stride_ar[/*stride_levels*/],
                          dmapp_seg_desc_t *src_seg,
                          void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                          int count[/*stride_levels+1*/], int stride_levels, int proc)
{
    int i, j;
    long src_idx, dst_idx;  /* index offset of current block position to ptr */
    int n1dim;  /* number of 1 dim block */
    int src_bvalue[7], src_bunit[7];
    int dst_bvalue[7], dst_bunit[7];
    int dmapp_status = DMAPP_RC_SUCCESS;
    int sizetoget;
    char *get_buf;
    long k = 0;
    dmapp_return_t status;
    rem_acc_desc_t *rem_acc_desc;
    int pipedepth;
    int buffered_1d = 0;

#if HAVE_DMAPP_LOCK
    int lock_on_get = use_locks_on_get;
    int lock_on_put = use_locks_on_put;
    use_locks_on_get = 0;
    use_locks_on_put = 0;
#endif

    /* number of n-element of the first dimension */
    n1dim = 1;
    for(i=1; i<=stride_levels; i++)
        n1dim *= count[i];

    sizetoget = count[0];

    /* Convert 1d operations into strided ones to gain from buffering */
    if(n1dim == 1 && sizetoget > CHUNK_SIZE_1D) {
        n1dim = sizetoget / CHUNK_SIZE_1D;
        if(sizetoget % CHUNK_SIZE_1D) n1dim++;
        sizetoget = CHUNK_SIZE_1D;
        buffered_1d = 1;
    }

    pipedepth = ARMCI_MIN(n1dim, PIPEDEPTH);

    if((sizetoget*pipedepth <= l_state.rem_acc_buf_len)) {
        // use pre-allocated buffer
        get_buf = l_state.rem_acc_buf;
    }
    else {
        // allocate the temporary buffers
        get_buf = (char *)my_malloc(sizetoget * pipedepth);
        assert(get_buf);
    }

    /* allocate an array which stores the calculated src_idx/dst_idx so
     * we can pre-fetch the data and overlap communications with the accumulate
     */
    rem_acc_desc = malloc(n1dim*sizeof(rem_acc_desc_t));
    assert(rem_acc_desc);

    /* calculate the destination indices */
    src_bvalue[0] = 0; src_bvalue[1] = 0; src_bunit[0] = 1; src_bunit[1] = 1;
    dst_bvalue[0] = 0; dst_bvalue[1] = 0; dst_bunit[0] = 1; dst_bunit[1] = 1;

    for(i=2; i<=stride_levels; i++)
    {
        src_bvalue[i] = 0;
        dst_bvalue[i] = 0;
        src_bunit[i] = src_bunit[i-1] * count[i-1];
        dst_bunit[i] = dst_bunit[i-1] * count[i-1];
    }

    for(i=0; i<n1dim; i++) {
        src_idx = 0;
        for(j=1; j<=stride_levels; j++) {
            src_idx += src_bvalue[j] * src_stride_ar[j-1];
            if((i+1) % src_bunit[j] == 0) {
                src_bvalue[j]++;
            }
            if(src_bvalue[j] > (count[j]-1)) {
                src_bvalue[j] = 0;
            }
        }

        dst_idx = 0;

        for(j=1; j<=stride_levels; j++) {
            dst_idx += dst_bvalue[j] * dst_stride_ar[j-1];
            if((i+1) % dst_bunit[j] == 0) {
                dst_bvalue[j]++;
            }
            if(dst_bvalue[j] > (count[j]-1)) {
                dst_bvalue[j] = 0;
            }
        }

        if(buffered_1d) {
            rem_acc_desc[i].src_idx = rem_acc_desc[i].dst_idx = i*CHUNK_SIZE_1D;
            if(i+1 == n1dim)
                // on last iteration resize sizetoget
                rem_acc_desc[i].sizetoget = count[0] - (i*CHUNK_SIZE_1D);
            else
                rem_acc_desc[i].sizetoget = sizetoget;
        }
        else {
            rem_acc_desc[i].src_idx = src_idx;
            rem_acc_desc[i].dst_idx = dst_idx;
            rem_acc_desc[i].sizetoget = sizetoget;
        }
    }

    // grab the atomic lock (in our rank)
    dmapp_network_lock(l_state.rank);

#if 0
    printf("%d.%d: REM_ACC: n1dim %d count[0] %d sizetoget %d stride_levels %d pipedepth %d buffered_1d %d\n",
           l_state.rank, proc, n1dim, count[0], sizetoget, stride_levels, pipedepth, buffered_1d);
#endif

    /* Fill the async Get pipe */
    for(i = 0; i < pipedepth; i++) {
        rem_acc_desc_t *curr = &rem_acc_desc[i];

        /* Issue async Get in to a temp buffer */
        status = dmapp_get_nb(get_buf + (i*sizetoget),
                              (char *)src_ptr + curr->src_idx, src_seg, proc,
                              curr->sizetoget/4, DMAPP_DW, &curr->syncid);
        assert(status == DMAPP_RC_SUCCESS);
    }

    for(i = 0; i < n1dim; i++) {
        rem_acc_desc_t *curr = &rem_acc_desc[i];
        char *buf = get_buf + ((i % pipedepth) * sizetoget);

        /* Wait for the previous async Get */
        status = dmapp_syncid_wait(&curr->syncid);
        assert(status == DMAPP_RC_SUCCESS);

        /* Now perform the Accumulate operation leaving the result in dst buf */
        do_acc(buf, (char *)dst_ptr + curr->dst_idx, datatype, scale, curr->sizetoget);

        if (i+pipedepth < n1dim) {
            rem_acc_desc_t *next = &rem_acc_desc[i+pipedepth];

            /* Issue the next async Get in to a temp buffer */
            status = dmapp_get_nb(buf, (char *)src_ptr + next->src_idx, src_seg, proc,
                                  next->sizetoget/4, DMAPP_DW, &next->syncid);
            assert(status == DMAPP_RC_SUCCESS);
        }
    }

    // ungrab the lock
    dmapp_network_unlock(l_state.rank);

    // Notify source that request has completed
    parmci_notify(proc);

    // free temp buffer
    if (get_buf != l_state.rem_acc_buf)
        my_free(get_buf);

    free(rem_acc_desc);

#if HAVE_DMAPP_LOCK
    use_locks_on_get = lock_on_get;
    use_locks_on_put = lock_on_put;
#endif

    return 0;
}

int process_remote_AccS(char *msg, uint32_t len, dmapp_pe_t proc)
{
    int i;
    void *rem_ptr, *src_ptr;
    void *dst_ptr; int *dst_stride_ar;
    dmapp_seg_desc_t rem_desc;
    int *count;
    int datatype; void *scale;
    int stride_levels;
    int slen;
    uint64_t bytes;
    int status = DMAPP_RC_SUCCESS;

    // printf("%d: [msgq thread] processing remote AccS from %d\n", l_state.rank, proc);


    /* Unpack the remote AccS request */
    rem_ptr = *(void **)msg;
    msg += sizeof(void*);

    rem_desc = *(dmapp_seg_desc_t *)msg;
    msg += sizeof(dmapp_seg_desc_t);

    dst_ptr = *(void **)msg;
    msg += sizeof(void*);

    stride_levels = *(int*)msg;
    msg += sizeof(int);

    dst_stride_ar = (int *)msg;
    msg += sizeof(int)*stride_levels;

    count = (int*)msg;
    msg += sizeof(int)*(stride_levels+1);

    datatype = *(int *)msg;
    msg += sizeof(int);

    scale = msg;
    msg += 2*sizeof(double);
    /* End Unpack remote AccS request */

    // printf("%d: [msgq thread] unpacked request\n", l_state.rank);

    if (rem_ptr == NULL) {
        // eager protocol
        // printf("%d: [msgq thread] eager - data is here\n", l_state.rank);
        src_ptr = msg;
    } else {
        // rendezvous protocol
        src_ptr = l_state.rem_acc_buf;
        for(i=0, bytes=1; i<=stride_levels; i++) bytes *= count[i];
        // printf("%d: [msgq thread] rendezvous - pulling data\n", l_state.rank);
        status = dmapp_get(src_ptr, rem_ptr, &rem_desc, proc, bytes/4, DMAPP_DW);
        assert(status == DMAPP_RC_SUCCESS);
        // printf("%d: [msgq thread] rendezvous - data is here\n", l_state.rank);
    }

    // perform a completely local AccS where only the dst is actually strided
    // printf("%d: [msgq thead] calling do_remote_AccS(%d, %x, %x, %x %x, %x, %d)\n",
    //    l_state.rank, datatype, scale, src_ptr, dst_ptr, dst_stride_ar, count, stride_levels);

    do_remote_AccS(datatype, scale,
                   src_ptr,
                   dst_ptr, dst_stride_ar,
                   count, stride_levels);

    // Notify source that request has completed
    parmci_notify(proc);

    return 0;
/*
    return do_remote_AccS_old(datatype, scale,
                          rem_ptr, src_stride_ar,
                          &rem_desc,
                          dst_ptr, dst_stride_ar,
                          count, stride_levels, proc);
*/
}

static int armci_queue_cb(void *context, void *data, uint32_t len, dmapp_pe_t rank)
{
    static int first_call = 1;
    dmapp_return_t status;
    dmapp_rma_attrs_ext_t attrs;

    /* Set the DMAPP attributes on first invocation of the helper thread */
    if (first_call == 1) {
        /* Read the current attributes */
        status = dmapp_get_rma_attrs_ext(&attrs);
        assert(status == DMAPP_RC_SUCCESS);

        /* Update the per thread attributes we want to change */
        /* Note we use the GET threshold here as the accumulate thread only does GETs */
        attrs.offload_threshold = l_state.dmapp_get_threshold;
        attrs.put_relaxed_ordering = l_state.dmapp_put_routing;
        attrs.get_relaxed_ordering = l_state.dmapp_get_routing;

        /* Change the attributes in this thread */
        status = dmapp_set_rma_attrs_ext(&attrs, &attrs);
        assert(status == DMAPP_RC_SUCCESS);

        first_call=0;
    }

    return process_remote_AccS(data, len, rank);
}

/* Attempt to perform the Remote ACC using a helper thread
 * Returns -1 if this is not possible or inefficient
 */
static int send_remote_AccS(int datatype, void *scale,
                            void *src_ptr, int src_stride_ar[/*stride_levels*/],
                            void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                            int count[/*stride_levels+1*/], int stride_levels, int proc)
{
    int i, nelems, wait;
    dmapp_return_t status;
    reg_entry_t *rem_data_reg = NULL;

    char *header;
    uint64_t data_size;
    uint64_t header_size = rem_acc_header_size(datatype, stride_levels);
    uint64_t max_put_size = (armci_dmapp_qnelems - 1) * sizeof(long);

    void *rem_data_ptr;
    dmapp_seg_desc_t *rem_data_desc;
    void *local_data_ptr;

    // printf("%d: entered send_remote_AccS\n", l_state.rank);

    // determine the size of the data to be transfered
    for(i=0, data_size=1; i<=stride_levels; i++) data_size *= count[i];

    /* Test whether we should use the Remote ACC thread or not */
    // default back the direct method if:
    // * large contiguous data
    // * data too large to repack pack into a single contiguous buffer
//  if ((stride_levels == 0 && data_size < armci_rem_acc_contig_threshold) ||
//      l_state.acc_buf_len < data_size) {
     if(l_state.acc_buf_len < data_size) {
        // printf("%d: WARNING: not using dmapp queue for AccS - %ld bytes\n", l_state.rank, data_size);
        return -1;
    }

    // Allocate header and calculate pointers
    if (header_size + data_size <= max_put_size) {
        // this is an eager message
        // printf("%d: eager remote AccS\n", l_state.rank);
        header = malloc(header_size + data_size);
        rem_data_ptr = NULL;
        rem_data_desc = NULL;
        local_data_ptr = header + header_size;
        header_size += data_size;
    } else {
        // this is a rendez-vous message
        // printf("%d: rendezvous remote AccS\n", l_state.rank);
        return -1; // turning off rendezvous protocol
        header = malloc(header_size);
        rem_data_reg = reg_cache_find(l_state.rank, l_state.acc_buf, data_size);
        assert(rem_data_reg);
        rem_data_ptr = l_state.acc_buf;
        local_data_ptr = l_state.acc_buf;
    }

    /* Pack message queue header and data blocks */
    // printf("%d: send_remote_AccS: pack header\n", l_state.rank);
    rem_acc_pack_header(header, datatype, scale,
        rem_data_ptr, rem_data_reg,
        dst_ptr, dst_stride_ar,
        count, stride_levels);
    // printf("%d: send_remote_AccS: pack data\n", l_state.rank);
    rem_acc_pack_data(local_data_ptr,
        src_ptr, src_stride_ar, count, stride_levels);

    /* Calculate message length in whole QWs */
    nelems = (header_size + sizeof(long) - 1)/sizeof(long);
    assert(nelems <= armci_dmapp_qnelems-1);

    /* Initiate the active message */
    // printf("%d: send_remote_AccS: queue active message\n", l_state.rank);
    status = dmapp_queue_put(armci_queue_hndl, header, nelems, DMAPP_QW, proc, 0);
    if (status != DMAPP_RC_SUCCESS) {
        fprintf(stderr,"\n dmapp_queue_put FAILED: %d\n", status);
        assert(0);
    }

    /* free header */
    free(header);

    /* Wait for completion of Remote accumulate */
    // printf("%d: send_remote_AccS: block on operation\n", l_state.rank);
    parmci_notify_wait(proc, &wait);
    // printf("%d: send_remote_AccS: AccS completed\n", l_state.rank);

    return 0;
}

#endif /* HAVE_DMAPP_QUEUE */

int PARMCI_AccS(int datatype, void *scale,
                void *src_ptr, int src_stride_ar[/*stride_levels*/],
                void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                int count[/*stride_levels+1*/], int stride_levels, int proc)
{
#if HAVE_XPMEM
        /* XPMEM optimisation */
        if (armci_uses_shm && ARMCI_Same_node(proc))
            return do_AccS(datatype, scale, src_ptr, src_stride_ar,
                           dst_ptr, dst_stride_ar, count , stride_levels, proc);
        else
#endif
#if HAVE_DMAPP_QUEUE
            /* DMAPP Queue based Remote accumulate */
            if (armci_use_rem_acc && send_remote_AccS(datatype, scale, src_ptr, src_stride_ar,
                                                      dst_ptr, dst_stride_ar, count, stride_levels, proc) == 0)
                ;
            else
#endif
                return do_AccS(datatype, scale, src_ptr, src_stride_ar,
                               dst_ptr, dst_stride_ar, count , stride_levels, proc);
}


int   PARMCI_Put_flag(void *src, void* dst, int size, int *flag, int value, int proc)
{
    assert(0);
}


int   PARMCI_PutS_flag(void *src_ptr, int src_stride_ar[/*stride_levels*/],
                 void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                 int count[/*stride_levels+1*/], int stride_levels,
                 int *flag, int value, int proc)
{
    assert(0);
}


/* The Value operations */
int PARMCI_PutValueInt(int src, void *dst, int proc)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(int), proc);
    return rc;
}


int PARMCI_PutValueLong(long src, void *dst, int proc)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(long), proc);
    return rc;
}


int PARMCI_PutValueFloat(float src, void *dst, int proc)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(float), proc);
    return rc;
}


int PARMCI_PutValueDouble(double src, void *dst, int proc)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(double), proc);
    return rc;
}


int PARMCI_GetValueInt(void *src, int proc)
{
    int val;
    PARMCI_Get(src, &val, sizeof(int), proc);
    return val;
}


long PARMCI_GetValueLong(void *src, int proc)
{
    long val;
    PARMCI_Get(src, &val, sizeof(long), proc);
    return val;
}


float PARMCI_GetValueFloat(void *src, int proc)
{
    float val;
    PARMCI_Get(src, &val, sizeof(float), proc);
    return val;
}


double PARMCI_GetValueDouble(void *src, int proc)
{
    double val;
    PARMCI_Get(src, &val, sizeof(double), proc);
    return val;
}


/* Non-blocking implementations of the above operations */

int PARMCI_NbPutValueInt(int src, void *dst, int proc, armci_hdl_t *handle)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(int), proc);
    return rc;
}


int PARMCI_NbPutValueLong(long src, void *dst, int proc, armci_hdl_t * handle)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(long), proc);
    return rc;
}


int PARMCI_NbPutValueFloat(float src, void *dst, int proc, armci_hdl_t *handle)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(float), proc);
    return rc;
}


int PARMCI_NbPutValueDouble(double src, void *dst, int proc, armci_hdl_t *handle)
{
    int rc;
    rc = PARMCI_Put(&src, dst, sizeof(double), proc);
    return rc;
}


void PARMCI_AllFence()
{
    PARMCI_WaitAll();
    /* noop for DMAPP */
}


void PARMCI_Fence(int proc)
{
    PARMCI_WaitAll();
    /* noop for DMAPP */
}


void PARMCI_Barrier()
{
    /* PARMCI_Barrier = PARMCI_Allfence + MPI_Barrier */
    PARMCI_AllFence();

    MPI_Barrier(l_state.world_comm);
}


void ARMCI_Error(char *msg, int code)
{
    if (0 == l_state.rank)
        fprintf(stderr,"Received an Error in Communication\n");

    MPI_Abort(l_state.world_comm, code);
}


int PARMCI_Malloc(void **ptrs, armci_size_t size)
{
    ARMCI_Group group;
    ARMCI_Group_get_world(&group);
#if 1
    {
        ARMCI_iGroup *igroup = armci_get_igroup_from_group(&group);
        int group_size;
        int world_size;
        MPI_Comm_size(l_state.world_comm, &world_size);
        assert(igroup->comm != MPI_COMM_NULL);
        MPI_Comm_size(igroup->comm, &group_size);
        assert(world_size == group_size);
    }
#endif
    return ARMCI_Malloc_group(ptrs, size, &group);
}


int PARMCI_Free(void *ptr)
{
    ARMCI_Group group;
    ARMCI_Group_get_world(&group);
    return ARMCI_Free_group(ptr, &group);
}


static void* _PARMCI_Malloc_local(armci_size_t size, armci_mr_info_t *mr)
{
    void *ptr;
    int rc;
    int status;

    rc = my_memalign(&ptr, armci_page_size, sizeof(char)*size);
    assert(0 == rc);
    assert(ptr);

    status = dmapp_mem_register(ptr, size, &mr->seg);
    assert(status == DMAPP_RC_SUCCESS);
#if DEBUG
    printf("[%d] _PARMCI_Malloc_local ptr=%p size=%zu\n",
            l_state.rank, ptr, size);
    printf("[%d] _PARMCI_Malloc_local seg=%p size=%zu\n",
            l_state.rank, mr->seg.addr, mr->seg.len);
#endif
#if 0
    assert(seg->addr == ptr);
    assert(seg->len == size); /* @TODO this failed! */
#endif
    reg_cache_insert(l_state.rank, ptr, size, mr);

    return ptr;
}


void *PARMCI_Malloc_local(armci_size_t size)
{
    void *ptr = NULL;
    armci_mr_info_t mr = {0};

    ptr = _PARMCI_Malloc_local(size, &mr);

    return ptr;
}


int PARMCI_Free_local(void *ptr)
{
    reg_return_t status = RR_FAILURE;

    /* preconditions */
    assert(NULL != ptr);

    /* remove from reg cache */
    status = reg_cache_delete(l_state.rank, ptr);
    assert(RR_SUCCESS == status);

    /* free the memory */
    my_free(ptr);
}


static void destroy_dmapp_locks(void)
{
#if HAVE_DMAPP_LOCK
#else
    if (l_state.local_lock_buf)
            PARMCI_Free_local(l_state.local_lock_buf);

    if (l_state.atomic_lock_buf)
            PARMCI_Free(l_state.atomic_lock_buf[l_state.rank]);
#endif
}


static void create_dmapp_locks(void)
{
#if HAVE_DMAPP_LOCK
    lock_desc = dmapp_sheap_malloc(sizeof(dmapp_lock_desc_t *)*(ARMCI_MAX_LOCKS+1));
    if(lock_desc == NULL) {
       fprintf(stderr,"dmapp_sheap_malloc cannot allocate %d bytes from sym heap memory\n",
                         sizeof(dmapp_lock_desc_t *)*(ARMCI_MAX_LOCKS+1));
       fprintf(stderr,"Please consider setting a larger symmetric heap size value, via the ");
       fprintf(stderr,"env flag: XT_SYMMETRIC_HEAP_SIZE\n");
       assert(0);
    }
    bzero(lock_desc, sizeof(dmapp_lock_desc_t *)*(ARMCI_MAX_LOCKS+1));
#else
    l_state.local_lock_buf = PARMCI_Malloc_local(sizeof(long));
    assert(l_state.local_lock_buf);

    l_state.atomic_lock_buf = (unsigned long **)my_malloc(l_state.size * sizeof(void *));
    assert(l_state.atomic_lock_buf);

    PARMCI_Malloc((void **)(l_state.atomic_lock_buf), sizeof(long));

    *(long *)(l_state.atomic_lock_buf[l_state.rank]) = 0;
    *(long *)(l_state.local_lock_buf) = 0;
#endif

    MPI_Barrier(l_state.world_comm);
}

#if HAVE_DMAPP_QUEUE
static void create_dmapp_queue(void)
{
    dmapp_return_t status;

    status = dmapp_queue_attach(armci_queue_cb, 0, 0, &armci_queue_hndl);
    if (status != DMAPP_RC_SUCCESS) {
        fprintf(stderr,"\n dmapp_queue_attach FAILED: %d\n", status);
        assert(0);
    }
}

static void destroy_dmapp_queue(void)
{
    dmapp_return_t status;

    status = dmapp_queue_detach(armci_queue_hndl, 0);
    if (status != DMAPP_RC_SUCCESS) {
        fprintf(stderr,"\n dmapp_queue_detach FAILED: %d\n", status);
        assert(0);
    }
}
#endif

static void dmapp_alloc_buf(void)
{
    // FAILURE_BUFSIZE should be some multiple of our page size?
    //l_state.acc_buf_len = FAILURE_BUFSIZE;
    l_state.acc_buf_len = ARMCI_MAX((armci_page_size), (24*ONEMEG));
    l_state.acc_buf = PARMCI_Malloc_local(l_state.acc_buf_len);
    assert(l_state.acc_buf);
    l_state.rem_acc_buf_len = ARMCI_MAX((armci_page_size), PIPEDEPTH*CHUNK_SIZE_1D);
    l_state.rem_acc_buf_len = ARMCI_MAX((armci_page_size), (24*ONEMEG));
    l_state.rem_acc_buf = PARMCI_Malloc_local(l_state.rem_acc_buf_len);
    assert(l_state.rem_acc_buf);

    //l_state.put_buf_len = FAILURE_BUFSIZE;
    l_state.put_buf_len = armci_page_size;
    l_state.put_buf = PARMCI_Malloc_local(l_state.put_buf_len);
    assert(l_state.put_buf);

    //l_state.get_buf_len = FAILURE_BUFSIZE;
    l_state.get_buf_len = armci_page_size;
    l_state.get_buf = PARMCI_Malloc_local(l_state.get_buf_len);
    assert(l_state.get_buf);
}


static void dmapp_free_buf(void)
{
    PARMCI_Free_local(l_state.acc_buf);
    PARMCI_Free_local(l_state.put_buf);
    PARMCI_Free_local(l_state.get_buf);
}

void armci_notify_init()
{
  int rc,bytes=sizeof(armci_notify_t)*armci_nproc;

  _armci_notify_arr=
        (armci_notify_t**)malloc(armci_nproc*sizeof(armci_notify_t*));
  if(!_armci_notify_arr)armci_die("armci_notify_ini:malloc failed",armci_nproc);

  if((rc=PARMCI_Malloc((void **)_armci_notify_arr, bytes)))
        armci_die(" armci_notify_init: armci_malloc failed",bytes);
  bzero(_armci_notify_arr[armci_me], bytes);
}

void cpu_yield()
{
#if defined(SYSV) || defined(MMAP) || defined(WIN32)
#ifdef SOLARIS
               yield();
#elif defined(WIN32)
               Sleep(1);
#elif defined(_POSIX_PRIORITY_SCHEDULING)
               sched_yield();
#else
               usleep(1);
#endif
#endif
}

/*\ busy wait
 *  n represents number of time delay units
 *  notused is useful to fool compiler by passing address of sensitive variable
\*/
#define DUMMY_INIT 1.0001
double _armci_dummy_work=DUMMY_INIT;
void armci_util_spin(int n, void *notused)
{
int i;
    for(i=0; i<n; i++)
        if(armci_msg_me()>-1)  _armci_dummy_work *=DUMMY_INIT;
    if(_armci_dummy_work>(double)armci_msg_nproc())_armci_dummy_work=DUMMY_INIT;
}


int PARMCI_Init()
{
    int status;

    if (initialized) {
        return 0;
    }
    initialized = 1;

    /* Assert MPI has been initialized */
    int init_flag;
    status = MPI_Initialized(&init_flag);
    assert(MPI_SUCCESS == status);
    assert(init_flag);

    /* Duplicate the World Communicator */
    status = MPI_Comm_dup(MPI_COMM_WORLD, &(l_state.world_comm));
    assert(MPI_SUCCESS == status);
    assert(l_state.world_comm);

    /* Duplicate the World Communicator, again */
    status = MPI_Comm_dup(MPI_COMM_WORLD, &ARMCI_COMM_WORLD);
    assert(MPI_SUCCESS == status);
    assert(ARMCI_COMM_WORLD);

    /* My Rank */
    status = MPI_Comm_rank(l_state.world_comm, &(l_state.rank));
    assert(MPI_SUCCESS == status);
    armci_me = l_state.rank;

    /* World Size */
    status = MPI_Comm_size(l_state.world_comm, &(l_state.size));
    assert(MPI_SUCCESS == status);
    armci_nproc = l_state.size;

    /* compile-time sanity check */
#if !(_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600)
#   error posix_memalign *NOT* available
#endif

    /* groups */
    armci_group_init();

    /* Initialize */
    dmapp_initialize();

#if HAVE_XPMEM
    /* XPMEM support: Determine SMP/Cluster node info */
    armci_init_clusinfo();
#endif

    // Create locks
    create_dmapp_locks();

#if HAVE_DMAPP_QUEUE
    if (armci_use_rem_acc)
        // Create DMAPP queue
        create_dmapp_queue();
#endif

    armci_notify_init();

    /* mutexes */
    l_state.mutexes = NULL;
    l_state.local_mutex = NULL;
    l_state.num_mutexes = NULL;

//#if DEBUG
#if 1
    if (0 == l_state.rank) {
        printf("gethugepagesize()=%ld\n", hugepagesize);
        printf("hugetlb_default_page_size=%ld\n", hugetlb_default_page_size);
        printf("_SC_PAGESIZE=%ld\n", sc_page_size);
        printf("armci_page_size=%ld\n", armci_page_size);
        printf("armci_is_using_huge_pages=%d\n", armci_is_using_huge_pages);
        printf("malloc_is_using_huge_pages=%d\n", malloc_is_using_huge_pages);
        printf("DMAPP put threshold=%d\n", l_state.dmapp_put_threshold);
        printf("DMAPP get threshold=%d\n", l_state.dmapp_get_threshold);
        printf("XPMEM use is %s\n", (armci_uses_shm) ? "ENABLED" : "DISABLED");
        printf("Optimized CPU memcpy is %s\n", (!armci_use_system_memcpy) ? "ENABLED" : "DISABLED");
#if HAVE_DMAPP_QUEUE
        printf("ARMCI_USE_REM_ACC is %s\n", (armci_use_rem_acc) ? "ENABLED" : "DISABLED");
        if (armci_use_rem_acc) {
            printf("contig remote acc threshold=%d\n", armci_rem_acc_contig_threshold);
            printf("strided remote acc threshold=%d\n", armci_rem_acc_strided_threshold);
        }
#endif
    }
#endif

    /* Synch - Sanity Check */
    MPI_Barrier(l_state.world_comm);

    return 0;
}


int PARMCI_Init_args(int *argc, char ***argv)
{
    int rc;
    int init_flag;

    MPI_Initialized(&init_flag);

    if(!init_flag)
        MPI_Init(argc, argv);

    rc = PARMCI_Init();
    return rc;
}


void PARMCI_Finalize()
{
    /* it's okay to call multiple times -- extra calls are no-ops */
    if (!initialized) {
        return;
    }

    initialized = 0;

    /* Make sure that all outstanding operations are done */
    PARMCI_WaitAll();

    /* groups */
    armci_group_finalize();

    if(lock_desc != NULL) {
        dmapp_sheap_free(lock_desc);
    }

    dmapp_terminate();

    MPI_Barrier(l_state.world_comm);

    // destroy the communicators
    MPI_Comm_free(&l_state.world_comm);
    MPI_Comm_free(&ARMCI_COMM_WORLD);
}


void ARMCI_Cleanup(void)
{
    PARMCI_Finalize();
}


/* JAD technically not an error to have empty impl of aggregate methods */
void ARMCI_SET_AGGREGATE_HANDLE(armci_hdl_t* handle)
{
}


void ARMCI_UNSET_AGGREGATE_HANDLE(armci_hdl_t* handle)
{
}


int   PARMCI_NbPut(void *src, void *dst, int bytes, int proc, armci_hdl_t *hdl)
{
    int rc;
    rc = PARMCI_Put(src, dst, bytes, proc);
    return rc;
}


int   PARMCI_NbGet(void *src, void *dst, int bytes, int proc, armci_hdl_t *hdl)
{
    int rc;
    rc = PARMCI_Get(src, dst, bytes, proc);
    return rc;
}


int   PARMCI_WaitProc(int proc)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);

    return 0;
}


int   PARMCI_Wait(armci_hdl_t* hdl)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);

    return 0;
}


int   PARMCI_Test(armci_hdl_t* hdl)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);

    return 0;
}


int   PARMCI_WaitAll()
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);
}


int   PARMCI_NbPutS(void *src_ptr, int src_stride_ar[/*stride_levels*/],
        void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
        int count[/*stride_levels+1*/], int stride_levels,
        int proc, armci_hdl_t *hdl)
{
    int rc;
    rc = PARMCI_PutS(src_ptr, src_stride_ar, dst_ptr,
            dst_stride_ar, count,stride_levels,proc);
    return rc;

}


int   PARMCI_NbGetS(void *src_ptr, int src_stride_ar[/*stride_levels*/],
                   void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                   int count[/*stride_levels+1*/], int stride_levels,
                   int proc, armci_hdl_t *hdl)
{
    int rc;
    rc = PARMCI_GetS(src_ptr, src_stride_ar, dst_ptr,
            dst_stride_ar, count,stride_levels, proc);
    return rc;
}


int   PARMCI_NbAccS(int datatype, void *scale,
                   void *src_ptr, int src_stride_ar[/*stride_levels*/],
                   void *dst_ptr, int dst_stride_ar[/*stride_levels*/],
                   int count[/*stride_levels+1*/],
                   int stride_levels, int proc, armci_hdl_t *hdl)
{
    int rc;
    rc = PARMCI_AccS(datatype, scale, src_ptr, src_stride_ar,
            dst_ptr, dst_stride_ar, count, stride_levels, proc);
    return rc;
}


/* internal to armci, and yet GA's ghost.c uses it... */
void armci_write_strided(void *ptr, int stride_levels,
        int stride_arr[], int count[], char *buf)
{
    assert(0);
}


/* internal to armci, and yet GA's ghost.c uses it... */
void armci_read_strided(void *ptr, int stride_levels,
        int stride_arr[], int count[], char *buf)
{
    assert(0);
}


/* Vector Calls */


int PARMCI_PutV(armci_giov_t *iov, int iov_len, int proc)
{
    int i;
    for (i=0; i<iov_len; ++i) {
        int j;
        void **src = iov[i].src_ptr_array;
        void **dst = iov[i].dst_ptr_array;
        int bytes = iov[i].bytes;
        int limit = iov[i].ptr_array_len;
        for (j=0; j<limit; ++j) {
            PARMCI_Put(src[j], dst[j], bytes, proc);
        }
    }
    return 0;
}


int PARMCI_GetV(armci_giov_t *iov, int iov_len, int proc)
{
    int i;
    for (i=0; i<iov_len; ++i) {
        int j;
        void **src = iov[i].src_ptr_array;
        void **dst = iov[i].dst_ptr_array;
        int bytes = iov[i].bytes;
        int limit = iov[i].ptr_array_len;
        for (j=0; j<limit; ++j) {
            PARMCI_Get(src[j], dst[j], bytes, proc);
        }
    }
    return 0;
}


int PARMCI_AccV(int datatype, void *scale, armci_giov_t *iov,
        int iov_len, int proc)
{
    int i;
    for (i=0; i<iov_len; ++i) {
        int j;
        void **src = iov[i].src_ptr_array;
        void **dst = iov[i].dst_ptr_array;
        int bytes = iov[i].bytes;
        int limit = iov[i].ptr_array_len;
        for (j=0; j<limit; ++j) {
            PARMCI_Acc(datatype, scale, src[j], dst[j], bytes, proc);
        }
    }
    return 0;
}


int PARMCI_NbPutV(armci_giov_t *iov, int iov_len, int proc, armci_hdl_t* handle)
{
    int rc;
    rc = PARMCI_PutV(iov, iov_len, proc);
    return rc;
}


int PARMCI_NbGetV(armci_giov_t *iov, int iov_len, int proc, armci_hdl_t* handle)
{
    int rc;
    rc = PARMCI_GetV(iov, iov_len, proc);
    return rc;
}


int PARMCI_NbAccV(int datatype, void *scale, armci_giov_t *iov,
        int iov_len, int proc, armci_hdl_t* handle)
{
    int rc;
    rc = PARMCI_AccV(datatype, scale, iov, iov_len, proc);
    return rc;
}


int parmci_notify(int proc)
{
   armci_notify_t *pnotify = _armci_notify_arr[armci_me]+proc;
   pnotify->sent++;
# ifdef MEM_FENCE
   if(SAMECLUSNODE(proc)) MEM_FENCE;
# endif
   PARMCI_Put(&pnotify->sent,&(_armci_notify_arr[proc]+armci_me)->received,
             sizeof(pnotify->sent),proc);
   return(pnotify->sent);
}


/*\ blocks until received count becomes >= waited count
 *  return received count and store waited count in *pval
\*/
int parmci_notify_wait(int proc,int *pval)
{
  int retval;
  {
     long loop=0;
     armci_notify_t *pnotify = _armci_notify_arr[armci_me]+proc;
     pnotify->waited++;
     while( pnotify->waited > pnotify->received) {
         if(++loop == 1000) { loop=0;cpu_yield(); }
         armci_util_spin(loop, pnotify);
     }
     *pval = pnotify->waited;
     retval=pnotify->received;
  }

  return retval;
}

/* static variables are automatically mapped in by DMAPP
 * so using them as the target of AMOs can avoid a local
 * register/deregister sequence
 */
static long local_fadd;
static long local_swap;

int  PARMCI_Rmw(int op, void *ploc, void *prem, int extra, int proc)
{
    int status;
    if (op == ARMCI_FETCH_AND_ADD) {
        /* Gemini dmapp doesn't have atomic fadd for int */
        int tmp;
        dmapp_network_lock(proc);
        PARMCI_Get(prem, ploc, sizeof(int), proc);
        tmp = *(int*)ploc + extra;
        PARMCI_Put(&tmp, prem, sizeof(int), proc);
        dmapp_network_unlock(proc);
    }
    else if (op == ARMCI_FETCH_AND_ADD_LONG) {
        reg_entry_t *rem_reg = reg_cache_find(proc, prem, sizeof(long));
        assert(rem_reg);
        status = dmapp_afadd_qw(&local_fadd, prem, &(rem_reg->mr.seg), proc, extra);
        if(status != DMAPP_RC_SUCCESS) {
           printf("dmapp_afadd_qw failed with %d\n",status);
           assert(status == DMAPP_RC_SUCCESS);
        }
        *(long*)ploc = local_fadd;
    }
    else if (op == ARMCI_SWAP) {
        /* Gemini dmapp doesn't have atomic swap for int */
        int tmp;
        dmapp_network_lock(proc);
        PARMCI_Get(prem, &tmp, sizeof(int), proc);
        PARMCI_Put(ploc, prem, sizeof(int), proc);
        dmapp_network_unlock(proc);
        *(int*)ploc = tmp;
    }
    else if (op == ARMCI_SWAP_LONG) {
        reg_entry_t *rem_reg = reg_cache_find(proc, prem, sizeof(long));
        assert(rem_reg);
        /* Gemini does not support SWAP, so emulate it with the AFAX operation */
        status = dmapp_afax_qw(&local_swap, prem, &(rem_reg->mr.seg), proc,
                               0 /* AND mask */, *(long *)ploc /* XOR mask */);
        *(long*)ploc = local_swap;
    }
    else  {
        assert(0);
    }

    return 0;
}


/* Mutex Operations */
int PARMCI_Create_mutexes(int num)
{
    unsigned int i=0;

    assert(NULL == l_state.mutexes);
    assert(NULL == l_state.local_mutex);
    assert(NULL == l_state.num_mutexes);

    /* every process knows how many mutexes created on every process */
    l_state.num_mutexes = (unsigned int*)my_malloc(l_state.size * sizeof(unsigned int));
    assert(l_state.num_mutexes);
    /* gather the counts */
    MPI_Allgather(&num, 1, MPI_INT,
            l_state.num_mutexes, 1, MPI_UNSIGNED, l_state.world_comm);

    /* create the 1 element buffer to hold a remote mutex */
    l_state.local_mutex = PARMCI_Malloc_local(sizeof(unsigned long));
    assert(l_state.local_mutex);
    /* init the local mutex holder to rank+1, indicating no mutex is held */
    *(unsigned long *)(l_state.local_mutex) = l_state.rank+1;
    MPI_Barrier(l_state.world_comm);

    /* create all of the mutexes */
    l_state.mutexes = (unsigned long**)my_malloc(l_state.size * sizeof(unsigned long*));
    assert(l_state.mutexes);
    PARMCI_Malloc((void **)l_state.mutexes, num*sizeof(unsigned long));
    /* init all of my mutexes to 0 */
    for (i=0; i<num; ++i) {
        l_state.mutexes[l_state.rank][i] = 0;
    }

    MPI_Barrier(l_state.world_comm);
}


int PARMCI_Destroy_mutexes()
{
    MPI_Barrier(l_state.world_comm);

    /* you cannot free mutexes if one is in use */
    assert(*((unsigned long *)l_state.local_mutex) == l_state.rank+1);
#ifndef NDEBUG
    {
        int i;
        for (i=0; i<l_state.num_mutexes[l_state.rank]; ++i) {
            unsigned long *mutexes = l_state.mutexes[l_state.rank];
            assert(mutexes[i] == 0);
        }
    }
#endif

    /* destroy mutex counts */
    my_free(l_state.num_mutexes);
    l_state.num_mutexes = NULL;

    /* destroy the 1 element buffer holding a remote mutex */
    PARMCI_Free_local(l_state.local_mutex);
    l_state.local_mutex = NULL;

    /* destroy the mutexes */
    PARMCI_Free(l_state.mutexes[l_state.rank]);
    my_free(l_state.mutexes);
    l_state.mutexes = NULL;

    MPI_Barrier(l_state.world_comm);
}


void ARMCI_Set_shm_limit(unsigned long shmemlimit)
{
    /* Not relevant for XPMEM support */
//    assert(0);
}


/* Is Shared memory enabled? */
int ARMCI_Uses_shm()
{
    /* Returns 1 when XPMEM support is enabled */
    return armci_uses_shm;
}


/* Group Functions */
int ARMCI_Uses_shm_grp(ARMCI_Group *group)
{
    /* Not sure what we do with this and XPMEM support */
    assert(0);
}


int ARMCI_Malloc_group(void *ptrs[], armci_size_t size, ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;
    int comm_rank = -1;
    int comm_size = -1;
    int rc = MPI_SUCCESS;
    void *src_buf = NULL;
    armci_size_t max_size = size;
    armci_mr_info_t mr;
    armci_mr_info_t *allgather_mr_info;
    int i = 0;
    reg_entry_t *reg = NULL;

    /* preconditions */
    assert(ptrs);
    assert(group);

    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);

    /* achieve consensus on the allocation size */
    rc = MPI_Allreduce(&size, &max_size, 1, MPI_LONG, MPI_MAX, comm);
    assert(rc == MPI_SUCCESS);
    size = max_size;
    assert(size > 0);

    /* allocate and register segment */
    ptrs[comm_rank] = _PARMCI_Malloc_local(sizeof(char)*max_size, &mr);

    /* exchange buffer address */
    /* @TODO: Consider using MPI_IN_PLACE? */
    memcpy(&src_buf, &ptrs[comm_rank], sizeof(void *));
    MPI_Allgather(&src_buf, sizeof(void *), MPI_BYTE, ptrs,
            sizeof(void *), MPI_BYTE, comm);

#if HAVE_XPMEM
    /* XPMEM support */
    if (armci_uses_shm) {
        /* 1. Make our memory segment available to other processes. */
        mr.segid  = xpmem_make(mr.seg.addr,
                               mr.seg.len,
                               XPMEM_PERMIT_MODE, (void *)0600);
        if (mr.segid == -1L) {
            armci_die("xpmem_make failed", errno);
        }
    }
#endif

    /* allocate receive buffer for exchange of registration info */
    allgather_mr_info = (armci_mr_info_t *)my_malloc(sizeof(armci_mr_info_t) * comm_size);
    assert(allgather_mr_info);

    /* exchange registration info */
    MPI_Allgather(&mr, sizeof(armci_mr_info_t), MPI_BYTE,
                  allgather_mr_info, sizeof(armci_mr_info_t), MPI_BYTE, comm);

    /* insert this mr info into the registration cache */
    for (i = 0; i < comm_size; ++i) {
        int world_rank = ARMCI_Absolute_id(group, i);
        if (i == comm_rank)
            continue;

#if HAVE_XPMEM
        /* XPMEM optimisation */
        if (armci_uses_shm && ARMCI_SAMECLUSNODE(world_rank)) {
            xpmem_apid_t apid;
            struct xpmem_addr xpmem_addr;
            void *vaddr;

            apid = xpmem_get(allgather_mr_info[i].segid, XPMEM_RDWR,
                             XPMEM_PERMIT_MODE, (void *)0600);
            if (apid == -1L) {
                    armci_die("xpmem_get failed ", errno);
            }

            xpmem_addr.apid   = apid;
            xpmem_addr.offset = 0;
            vaddr = xpmem_attach(xpmem_addr, allgather_mr_info[i].seg.len, NULL);
            if (vaddr == (void *)-1) {
                armci_die("xpmem_attach failed ", errno);
            }

            allgather_mr_info[i].apid  = apid;
            allgather_mr_info[i].vaddr = vaddr;
        }
#endif

        reg_cache_insert(world_rank, ptrs[i], size, &allgather_mr_info[i]);
    }

    // Free the temporary buffer
    my_free(allgather_mr_info);

    MPI_Barrier(comm);

    return 0;
}


int ARMCI_Free_group(void *ptr, ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;
    int comm_rank;
    int comm_size;
    int i;
    long **allgather_ptrs = NULL;

    /* preconditions */
    assert(NULL != ptr);
    assert(NULL != group);

    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);

    /* allocate receive buffer for exchange of pointers */
    allgather_ptrs = (long **)my_malloc(sizeof(void *) * comm_size);
    assert(allgather_ptrs);

    /* exchange of pointers */
    MPI_Allgather(&ptr, sizeof(void *), MPI_BYTE,
            allgather_ptrs, sizeof(void *), MPI_BYTE, comm);

    /* remove all ptrs from registration cache */
    for (i = 0; i < comm_size; i++) {
        int world_rank = ARMCI_Absolute_id(group, i);
        if (i == comm_rank)
            continue;
        reg_cache_delete(world_rank, allgather_ptrs[i]);
    }

    /* remove my ptr from reg cache and free ptr */
    PARMCI_Free_local(ptr);

    // Synchronize: required by ARMCI semantics

    my_free(allgather_ptrs);
    MPI_Barrier(comm);

    return 0;
}


/* Non-Collective Memory functions */


void ARMCI_Memget(size_t bytes, armci_meminfo_t *meminfo, int memflg)
{
    assert(0);
}


void* ARMCI_Memat(armci_meminfo_t *meminfo, long offset)
{
    assert(0);
    return NULL;
}


void ARMCI_Memdt(armci_meminfo_t *meminfo, long offset)
{
    assert(0);
}



void ARMCI_Memctl(armci_meminfo_t *meminfo)
{
    assert(0);
}


/* DMAPP Functions */

static dmapp_routing_type_t convert_routing_code(int rc)
{
    dmapp_routing_type_t routing_mode = DMAPP_ROUTING_ADAPTIVE;

        /* Remap cmd line HW RC codes to DMAPP software ones
           RC   DMAPP name/value
           0  = DETERMINISTIC (non-min hashed)
           1  = DETERMINISTIC_1 (min hashed)
           2  = reserved
           3  = IN_ORDER (min non-hashed)
           4  = ADAPTIVE
           5  = ADAPTIVE_1
           6  = ADAPTIVE_2
           7  = ADAPTIVE_3
        */
        switch(rc) {
        case 0: routing_mode=DMAPP_ROUTING_DETERMINISTIC; break;
        case 1: routing_mode=DMAPP_ROUTING_DETERMINISTIC_1; break;
        case 3: routing_mode=DMAPP_ROUTING_IN_ORDER; break;
        case 4: routing_mode=DMAPP_ROUTING_ADAPTIVE; break;
        case 5: routing_mode=DMAPP_ROUTING_ADAPTIVE_1; break;
        case 6: routing_mode=DMAPP_ROUTING_ADAPTIVE_2; break;
        case 7: routing_mode=DMAPP_ROUTING_ADAPTIVE_3; break;
        default:
            fprintf(stderr, "Invalid routing control mode: rc=%d\n", rc);
        }

        return routing_mode;
}

static void check_envs(void)
{
    char *value;

    /* ARMCI_DMAPP_[PUT|GET]_ROUTING=[0 .. 7]
     *
     * Allow the user to overload the default DMAPP routing modes
     * for both Put and Get operations.
     * Default is DMAPP_ROUTING_ADAPTIVE (4)
     *
     */
    if ((value = getenv("ARMCI_DMAPP_PUT_ROUTING")) != NULL){
        l_state.dmapp_put_routing = convert_routing_code(atoi(value));
    }
    else {
        l_state.dmapp_put_routing = DMAPP_ROUTING_ADAPTIVE;
    }

    if ((value = getenv("ARMCI_DMAPP_GET_ROUTING")) != NULL){
        l_state.dmapp_get_routing = convert_routing_code(atoi(value));
    }
    else {
        l_state.dmapp_get_routing = DMAPP_ROUTING_ADAPTIVE;
    }

    /* ARMCI_DMAPP_[PUT|GET]_THRESHOLD
     *
     * Vary the point at which DMAPP switches to using RDMA to
     * launch Put and Get operations. Value is in bytes.
     * DMAPP only allows one configuration setting which applies
     * to both Puts and Gets. But as the parameter is maintained on a
     * per thread basis, so we can adjust the Get threshold in the
     * Remote Accumulate helper thread.
     */
    if ((value = getenv("ARMCI_DMAPP_PUT_THRESHOLD")) != NULL){
        l_state.dmapp_put_threshold = (atoi(value));
    }
    else {
        l_state.dmapp_put_threshold = ARMCI_DMAPP_PUT_OFFLOAD_THRESHOLD;
    }

    /* This threshold is only used in the Remote Acc thread */
    if ((value = getenv("ARMCI_DMAPP_GET_THRESHOLD")) != NULL){
        l_state.dmapp_get_threshold = (atoi(value));
    }
    else {
        l_state.dmapp_get_threshold = ARMCI_DMAPP_GET_OFFLOAD_THRESHOLD;
    }

#if HAVE_DMAPP_LOCK
    if(getenv("ARMCI_DMAPP_LOCK_ON_GET")) {
       use_locks_on_get = atoi(getenv("ARMCI_DMAPP_LOCK_ON_GET"));
       if(0 == l_state.rank) {
          fprintf(stdout,"ARMCI_DMAPP_LOCK_ON_GET = %d\n",use_locks_on_get);
          fflush(stdout);
       }
    }
    if(getenv("ARMCI_DMAPP_LOCK_ON_PUT")) {
       use_locks_on_put = atoi(getenv("ARMCI_DMAPP_LOCK_ON_PUT"));
       if(0 == l_state.rank) {
          fprintf(stdout,"ARMCI_DMAPP_LOCK_ON_PUT = %d\n",use_locks_on_put);
          fflush(stdout);
       }
    }
    if(getenv("ARMCI_EXTERNAL_LOCKING")) {
       use_external_locks = atoi(getenv("ARMCI_EXTERNAL_LOCKING"));
       if(0 == l_state.rank) {
          fprintf(stdout,"ARMCI_EXTERNAL_LOCKING = %d\n",use_external_locks);
          fflush(stdout);
       }
    }
#endif

#if HAVE_LIBHUGETLBFS

    /* hugepagesize
     *
     * set the static variable hugepagesize */
    hugepagesize = gethugepagesize();

    /* HUGETLB_MORECORE
     *
     * this variable controls whether malloc() will use hugepage memory which
     * means when we use malloc() and when the user application calls malloc
     * we will be competing for hugepage memory! */
    if ((value = getenv("HUGETLB_MORECORE")) != NULL) {
        if (0 == strncasecmp(value, "y", 1)) {
            malloc_is_using_huge_pages = 1;
        }
        else if (0 == strncasecmp(value, "n", 1)) {
            malloc_is_using_huge_pages = 0;
        }
    }

    /* ARMCI_USE_HUGEPAGES
     *
     * ARMCI can be built with hugepages and still allow the user to disable
     * their use. We assume that if libhugetlbfs is linked in, the user wants
     * to use it. This env var is then for the user to disable it, for some
     * reason. */
    armci_is_using_huge_pages = 1; /* the default if libhugetlbfs is linked */
    if ((value = getenv("ARMCI_USE_HUGEPAGES")) != NULL) {
        if (0 == strncasecmp(value, "y", 1)) {
            armci_is_using_huge_pages = 1;
        }
        else if (0 == strncasecmp(value, "n", 1)) {
            armci_is_using_huge_pages = 0;
        }
    }
    /* CRAY WORKAROUND: get_hugepage_region() currently fails on Cascade HW.
     * This is related to the sysconf(_SC_LEVEL2_CACHE_LINESIZE) call
     * incorrectly returning 0 */
    if (sysconf(_SC_LEVEL2_CACHE_LINESIZE) == 0)
        armci_is_using_huge_pages = 0;

    /* HUGETLB_DEFAULT_PAGE_SIZE
     *
     * controls the page size that will be used for hugepages
     * we look for this value in case it is specified and we want to allocate
     * memory aligned to the same page size */
    if ((value = getenv("HUGETLB_DEFAULT_PAGE_SIZE")) != NULL){
        /* must be one of [128K|512K|2M|4M|8M|16M|32M|64M|128M|256M] */
        if (0 == strncasecmp(value, "128K", 4)) {
            hugetlb_default_page_size = 128*ONEK;
        }
        else if (0 == strncasecmp(value, "512K", 4)) {
            hugetlb_default_page_size = 512*ONEK;
        }
        else if (0 == strncasecmp(value, "2M", 2)) {
            hugetlb_default_page_size = 2*ONEMEG;
        }
        else if (0 == strncasecmp(value, "4M", 2)) {
            hugetlb_default_page_size = 4*ONEMEG;
        }
        else if (0 == strncasecmp(value, "8M", 2)) {
            hugetlb_default_page_size = 8*ONEMEG;
        }
        else if (0 == strncasecmp(value, "16M", 3)) {
            hugetlb_default_page_size = 16*ONEMEG;
        }
        else if (0 == strncasecmp(value, "32M", 3)) {
            hugetlb_default_page_size = 32*ONEMEG;
        }
        else if (0 == strncasecmp(value, "64M", 3)) {
            hugetlb_default_page_size = 64*ONEMEG;
        }
        else if (0 == strncasecmp(value, "128M", 4)) {
            hugetlb_default_page_size = 128*ONEMEG;
        }
        else if (0 == strncasecmp(value, "256M", 4)) {
            hugetlb_default_page_size = 256*ONEMEG;
        }
        else {
            assert(0);
        }
    }

    if (malloc_is_using_huge_pages || armci_is_using_huge_pages) {
        armci_page_size = hugepagesize;
    }

#endif /* HAVE_LIBHUGETLBFS */

    /* get page size for memory allocation */
    sc_page_size = sysconf(_SC_PAGESIZE);
    assert(sc_page_size >= 1);
    if (0 == armci_page_size) {
        armci_page_size = sc_page_size;
    }

#if HAVE_XPMEM
    /* XPMEM support */
    if ((value = getenv("ARMCI_USE_XPMEM")) != NULL) {
        if (0 == strncasecmp(value, "y", 1)) {
            armci_uses_shm = 1;
        }
        else if (0 == strncasecmp(value, "n", 1)) {
            armci_uses_shm = 0;
        }
    }

    if ((value = getenv("ARMCI_USE_SYSTEM_MEMCPY")) != NULL) {
        if (0 == strncasecmp(value, "y", 1)) {
            armci_use_system_memcpy = 1;
        }
        else if (0 == strncasecmp(value, "n", 1)) {
            armci_use_system_memcpy = 1;
        }
    }
#endif

#if HAVE_DMAPP_QUEUE
    /* DMAPP Queue API support */

    /* Enable/disable the Remote Accumulate feature
     *
     * ARMCI_USE_REM_ACC=[y|n]
     */
    if ((value = getenv("ARMCI_USE_REM_ACC")) != NULL) {
        if (0 == strncasecmp(value, "y", 1)) {
            armci_use_rem_acc = 1;
        }
        else if (0 == strncasecmp(value, "n", 1)) {
            armci_use_rem_acc = 0;
        }
    }
    /* Check for availability of DMAPP Queue API at compile/runtime */
    if (dmapp_queue_attach == NULL)
            armci_use_rem_acc = 0;

    /* Set the threshold for using the Remote accumulate feature
     *
     * ARMCI_REM_ACC_[CONTIG|STRIDED]_THRESHOLD=<bytes>
     *
     * Separate byte thresholds are used for the Contiguous/1D and
     * Strided calls.
     */
    if ((value = getenv("ARMCI_REM_ACC_CONTIG_THRESHOLD")) != NULL) {
        armci_rem_acc_contig_threshold = atoi(value);
    }
    if ((value = getenv("ARMCI_REM_ACC_STRIDED_THRESHOLD")) != NULL) {
        armci_rem_acc_strided_threshold = atoi(value);
    }
#endif

}


static void dmapp_initialize(void)
{
    dmapp_return_t status;
    dmapp_rma_attrs_ext_t requested_attrs;
    dmapp_rma_attrs_ext_t actual_attrs;
    dmapp_jobinfo_t job;

    memset(&requested_attrs, 0, sizeof(requested_attrs));
    memset(&actual_attrs, 0, sizeof(actual_attrs));

    status = dmapp_get_rma_attrs_ext(&requested_attrs);
    assert(status == DMAPP_RC_SUCCESS);

    // Check envs
    check_envs();

    /* The maximum number of outstanding non-blocking requests supported. You
     * can only specify this flag during initialization. The following is the
     * range of valid values to be supplied: [DMAPP_MIN_OUTSTANDING_NB, ..,
     * DMAPP_MAX_OUTSTANDING_NB] Setting the value to one of the extremes may
     * lead to a slowdown. The recommended value is DMAPP_DEF_OUTSTANDING_NB.
     * Users can experiment with the value to find the optimal setting for
     * their application. */
    requested_attrs.max_outstanding_nb = MAX_NB_OUTSTANDING;
    assert(MAX_NB_OUTSTANDING >= DMAPP_MIN_OUTSTANDING_NB);
    assert(MAX_NB_OUTSTANDING <= DMAPP_MAX_OUTSTANDING_NB);
    if (0 == l_state.rank) {
        if (MAX_NB_OUTSTANDING != DMAPP_DEF_OUTSTANDING_NB) {
            printf("MAX_NB_OUTSTANDING=%u != DMAPP_DEF_OUTSTANDING_NB=%u\n",
                    MAX_NB_OUTSTANDING, DMAPP_DEF_OUTSTANDING_NB);
        }
    }

    /* The threshold, in bytes, for switching between CPU-based
     * mechanisms and CPU offload mechanisms. This value can be
     * specified at any time and can use any value. The default setting is
     * DMAPP_OFFLOAD_THRESHOLD. Very small or very large settings
     * may lead to suboptimal performance. The default value is 4k bytes.
     * Consider how to best set this threshold. While a threshold increase
     * may increase CPU availability, it may also increase transfer latency
     * due to BTE involvement. */
    requested_attrs.offload_threshold = l_state.dmapp_put_threshold;

    /* Specifies the type of routing to be used. Applies to RMA requests with
     * PUT semantics and all AMOs. The default is DMAPP_ROUTING_ADAPTIVE.
     * The value can be specified at any time. Note that
     * DMAPP_ROUTING_IN_ORDER guarantees the requests arrive in order and may
     * result in poor performance.  Valid settings are:
     * - DMAPP_ROUTING_IN_ORDER
     * - DMAPP_ROUTING_DETERMINISTIC
     * - DMAPP_ROUTING_ADAPTIVE */
    requested_attrs.put_relaxed_ordering = l_state.dmapp_put_routing;

    /* Specifies the type of routing to be used. Applies to RMA requests with
     * GET semantics. The default is DMAPP_ROUTING_ADAPTIVE. The value can be
     * specified at any time. Note that DMAPP_ROUTING_IN_ORDER may result in
     * poor performance. Valid settings are:
     * - DMAPP_ROUTING_IN_ORDER
     * - DMAPP_ROUTING_DETERMINISTIC
     * - DMAPP_ROUTING_ADAPTIVE */
    requested_attrs.get_relaxed_ordering = l_state.dmapp_get_routing;

    /* The maximum number of threads that can access DMAPP. You can only use
     * this when thread-safety is enabled. The default is 1. You can only
     * specify this during initialization and it must be >= 1. */
    requested_attrs.max_concurrency = 1;

    /* Defines the PI ordering registration flags used by DMAPP when
     * registering all memory regions with GNI. Applies to the data, symmetric
     * heap, and user or dynamically mapped regions. The default is
     * DMAPP_PI_RELAXED_ORDERING.
     *
     * The dmapp_pi_reg_type_t enumeration defines the modes of PI access
     * ordering to be used by DMAPP during memory registration with uGNI;
     * therefore, these modes apply to the data and symmetric heap and any
     * user or dynamically mapped regions.
     *
     * These modes do not affect GET operations.
     *
     * Strict ordering ensures that posted and non-posted writes arrive at the
     * target in strict order. Default and relaxed ordering impose no ordering
     * constraints, therefore if an application requires the global visibility
     * of data (for example, after a blocking put or gsync/fence), it must
     * perform extra synchronization in the form of a remote GET from the
     * target node in order to ensure that written data is globally visible.
     * - DMAPP_PI_ORDERING_STRICT   Strict PI (P_PASS_PW=0, NP_PASS_PW=0)
     * - DMAPP_PI_ORDERING_DEFAULT  Default GNI PI (P_PASS_PW=0, NP_PASS_PW=1)
     * - DMAPP_PI_ORDERING_RELAXED  Relaxed PI ordering (P_PASS_PW=1, NP_PASS_PW=1) */
    requested_attrs.PI_ordering = DMAPP_PI_ORDERING_RELAXED;

#if HAVE_DMAPP_QUEUE
    /* Modify RMA attributes to match our needs */
    requested_attrs.queue_depth = armci_dmapp_qdepth;
    requested_attrs.queue_nelems = armci_dmapp_qnelems;
    requested_attrs.queue_flags = armci_dmapp_qflags;
#endif

    // initialize
    status = dmapp_init_ext(&requested_attrs, &actual_attrs);
    assert(status == DMAPP_RC_SUCCESS);
#define sanity(field) assert(actual_attrs.field == requested_attrs.field)
    sanity(max_outstanding_nb);
    sanity(offload_threshold);
    sanity(put_relaxed_ordering);
    sanity(get_relaxed_ordering);
    sanity(max_concurrency);
    sanity(PI_ordering);
#undef sanity

    // TODO is this the correct place to set this?
    max_outstanding_nb = actual_attrs.max_outstanding_nb;

    status = dmapp_get_jobinfo (&(l_state.job));
    assert(status == DMAPP_RC_SUCCESS);

    // Initialize the reg cache
    reg_cache_init(l_state.size);

    // Allocate buffers
    dmapp_alloc_buf();

}


static void dmapp_terminate(void)
{
    int status;

#if HAVE_DMAPP_QUEUE
    if (armci_use_rem_acc)
        // Destroy DMAPP queue
        destroy_dmapp_queue();
#endif

    destroy_dmapp_locks();

    dmapp_free_buf();

    reg_cache_destroy(l_state.size);

    status = dmapp_finalize();
    assert(status == DMAPP_RC_SUCCESS);

    MPI_Barrier(l_state.world_comm);
}


int ARMCI_Same_node(int proc)
{
    return armci_domain_same_id(ARMCI_DOMAIN_SMP, proc);
}


int PARMCI_Initialized()
{
    return initialized;
}


int PARMCI_PutS_flag_dir(void *src_ptr, int *src_stride_arr, void *dst_ptr, int *dst_stride_arr, int *count, int stride_levels, int *flag, int val, int proc)
{
    assert(0);
}


/* JAD multiple tests needed these two symbols */
void derr_printf(const char *format, ...) {
    assert(0);
}


int dassertp_fail(const char *cond_string, const char *file,
                  const char *func, unsigned int line, int code) {
    assert(0);
}


/* JAD test_memlock needed these */
void armci_lockmem(void *pstart, void* pend, int proc) {
    assert(0);
}


void armci_unlockmem(int proc) {
    assert(0);
}
