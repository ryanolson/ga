#if HAVE_CONFIG_H
#   include "config.h"
#endif

/* C and/or system headers */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <hugetlbfs.h>

/* 3rd party headers */
#include <mpi.h>
#include <dmapp.h>

/* our headers */
#include "armci.h"
#include "armci_impl.h"
#include "groups.h"
#include "parmci.h"
#include "reg_cache.h"

/* Cray */
#define HAVE_DMAPP_LOCK 1

#define DEBUG 0


#if HAVE_DMAPP_LOCK
// ARMCI_MAX_LOCKS mirrors the default DMAPP_MAX_LOCKS limit
// Larger values of ARMCI_MAX_LOCKS will require DMAPP_MAX_LOCKS be set at runtime.
// DMAPP_MAX_LOCKS has a maxium value of 1023
#define ARMCI_MAX_LOCKS 128  
static dmapp_lock_desc_t   lock_desc[ARMCI_MAX_LOCKS];
static dmapp_lock_handle_t lock_handle[ARMCI_MAX_LOCKS];
static int use_locks_on_get = 0;
static int use_locks_on_put = 0;
#endif


/* exported state */
local_state l_state;
int armci_me=-1;
int armci_nproc=-1;
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
static void* _PARMCI_Malloc_local(armci_size_t size, dmapp_seg_desc_t *seg);

/* needed for complex accumulate */
typedef struct {
    double real;
    double imag;
} DoubleComplex;

typedef struct {
    float real;
    float imag;
} SingleComplex;

typedef struct{
  int sent;
  int received;
  int waited;
}armci_notify_t;

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
    reg_entry_t *src_reg = NULL;

    /* Corner case */
    if (proc == l_state.rank) {
        memcpy(dst, src, bytes);
        return status;
    }

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

    /* Find the dmapp seg desc */
    dst_reg = reg_cache_find(proc, dst, bytes);
    assert(dst_reg);

    src_reg = reg_cache_find(l_state.rank, src, bytes);

    status = dmapp_put_nbi(dst, &(dst_reg->mr), proc, src, nelems, type);
    increment_total_outstanding();
    if (status != DMAPP_RC_SUCCESS) {
        failure_observed = 1;
    }

    /* Fallback */
    if (failure_observed) {
        PARMCI_WaitAll();
        assert(bytes <= l_state.put_buf_len);
        memcpy(l_state.put_buf, src, bytes);
        status = dmapp_put_nbi(dst, &(dst_reg->mr),
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
    reg_entry_t *dst_reg = NULL;

    /* Corner case */
    if (proc == l_state.rank) {
        memcpy(dst, src, bytes);
        return status;
    }

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

    /* Find the dmapp seg desc */
    dst_reg = reg_cache_find(proc, src, bytes);
    assert(dst_reg);

    status = dmapp_get_nbi(dst, src, &(dst_reg->mr),
            proc, nelems, type);
    increment_total_outstanding();
    if (status != DMAPP_RC_SUCCESS) {
        failure_observed = 1;
    }
    
    /* Fallback */
    if (failure_observed) {    
        PARMCI_WaitAll();
        assert(bytes <= l_state.get_buf_len);
        status = dmapp_get_nbi(l_state.get_buf, src, &(dst_reg->mr),
                proc, nelems, type);
        increment_total_outstanding();
        PARMCI_WaitAll();
        memcpy(dst, l_state.get_buf, bytes);

        /* Fallback must work correctly */
        assert(status == DMAPP_RC_SUCCESS);
    }

    return status;
}


static void dmapp_network_lock(int proc)
{
    int dmapp_status;

#if HAVE_DMAPP_LOCK
    dmapp_lock_acquire( &lock_desc[0], &(l_state.job.data_seg), proc, 0, &lock_handle[0]);
#else
    reg_entry_t *dst_reg= reg_cache_find(proc, 
            l_state.atomic_lock_buf[proc], sizeof(long));

    assert(dst_reg);

    do {    
        dmapp_status = dmapp_acswap_qw(l_state.local_lock_buf, 
                l_state.atomic_lock_buf[proc],
                &(dst_reg->mr),
                proc, 0, l_state.rank + 1);

        assert(dmapp_status == DMAPP_RC_SUCCESS);
    }
    while(*(l_state.local_lock_buf) != 0);
#endif
}


static void dmapp_network_unlock(int proc)
{
    int dmapp_status;

# if HAVE_DMAPP_LOCK
    dmapp_lock_release( lock_handle[0], 0 );
#else
    reg_entry_t *dst_reg= reg_cache_find(proc, 
            l_state.atomic_lock_buf[proc], sizeof(long));

    assert(dst_reg);

    do {
        dmapp_status = dmapp_acswap_qw(l_state.local_lock_buf, 
                l_state.atomic_lock_buf[proc],
                &(dst_reg->mr),
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

#if HAVE_DMAPP_LOCK
    if(use_locks_on_put) dmapp_network_unlock(proc);
#endif

    PARMCI_WaitProc(proc);

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

#if HAVE_DMAPP_LOCK
    if(use_locks_on_get) dmapp_network_unlock(proc);
#endif

    PARMCI_WaitProc(proc);
    
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


int PARMCI_AccS(int datatype, void *scale,
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
    double calc_scale = *(double *)scale;
    int sizetogetput;
    void *get_buf;
    long k = 0;

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

    if (sizetogetput <= l_state.acc_buf_len) {
        get_buf = l_state.acc_buf;
    }
    else {
        get_buf = (char *)my_malloc(sizeof(char) * sizetogetput);
    }

    assert(get_buf);

    // grab the atomics lock
    dmapp_network_lock(proc);

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
        // allocate the temporary buffer

        // Get the remote data in a temp buffer
        PARMCI_Get((char *)dst_ptr + dst_idx, get_buf, sizetogetput, proc);

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
            int m_lim = count[0]/sizeof(C_TYPE);                            \
            C_TYPE *iterator = (C_TYPE *)get_buf;                           \
            C_TYPE *value = (C_TYPE *)((char *)src_ptr + src_idx);          \
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

        // Write back
        PARMCI_Put(get_buf, (char *)dst_ptr + dst_idx, sizetogetput, proc);

        // free temp buffer
    }
    PARMCI_WaitProc(proc);

    // ungrab the lock
    dmapp_network_unlock(proc);

#if HAVE_DMAPP_LOCK
    use_locks_on_get = lock_on_get;
    use_locks_on_put = lock_on_put;
#endif

    if (sizetogetput > l_state.acc_buf_len)
        my_free(get_buf);

    return 0;
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


static void* _PARMCI_Malloc_local(armci_size_t size, dmapp_seg_desc_t *seg)
{
    void *ptr;
    int rc;
    int status;

    rc = my_memalign(&ptr, armci_page_size, sizeof(char)*size);
    assert(0 == rc);
    assert(ptr);

    status = dmapp_mem_register(ptr, size, seg);
    assert(status == DMAPP_RC_SUCCESS);
#if DEBUG
    printf("[%d] _PARMCI_Malloc_local ptr=%p size=%zu\n",
            l_state.rank, ptr, size);
    printf("[%d] _PARMCI_Malloc_local seg=%p size=%zu\n",
            l_state.rank, seg->addr, seg->len);
#endif
#if 0
    assert(seg->addr == ptr);
    assert(seg->len == size); /* @TODO this failed! */
#endif
    reg_cache_insert(l_state.rank, ptr, size, *seg);

    return ptr;
}


void *PARMCI_Malloc_local(armci_size_t size)
{
    void *ptr = NULL;
    dmapp_seg_desc_t seg;

    ptr = _PARMCI_Malloc_local(size, &seg);

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
    bzero(lock_desc, sizeof(lock_desc));
#else
    l_state.local_lock_buf = PARMCI_Malloc_local(sizeof(long));
    assert(l_state.local_lock_buf);

    l_state.atomic_lock_buf = (void **)my_malloc(l_state.size * sizeof(void *));
    assert(l_state.atomic_lock_buf);

    PARMCI_Malloc((l_state.atomic_lock_buf), sizeof(long));

    *(long *)(l_state.atomic_lock_buf[l_state.rank]) = 0;
    *(long *)(l_state.local_lock_buf) = 0;
#endif

    MPI_Barrier(l_state.world_comm);
}


static void dmapp_alloc_buf(void)
{
    // FAILURE_BUFSIZE should be some multiple of our page size?
    //l_state.acc_buf_len = FAILURE_BUFSIZE;
    l_state.acc_buf_len = armci_page_size;
    l_state.acc_buf = PARMCI_Malloc_local( l_state.acc_buf_len);
    assert(l_state.acc_buf);

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

    armci_notify_init();

    /* mutexes */
    l_state.mutexes = NULL;
    l_state.local_mutex = NULL;
    l_state.num_mutexes = NULL;

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
    return 0;
}


int   PARMCI_WaitProc(int proc)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);
}


int   PARMCI_Wait(armci_hdl_t* hdl)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);
}


int   PARMCI_Test(armci_hdl_t* hdl)
{
    int status;
    status = dmapp_gsync_wait();
    assert(status == DMAPP_RC_SUCCESS);
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

/* Always return 0, since shared memory not implemented yet */
int PARMCI_Same_node(int proc)
{
    return 0;
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
        status = dmapp_afadd_qw(&local_fadd, prem, &(rem_reg->mr), proc, extra);
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
        status = dmapp_afax_qw(&local_swap, prem, &(rem_reg->mr), proc,
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
    PARMCI_Malloc(l_state.mutexes, num*sizeof(unsigned long));
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


void PARMCI_Lock(int mutex, int proc)
{
    int dmapp_status;
    reg_entry_t *dst_reg = NULL;

    /* preconditions */
    assert(0 <= proc && proc < l_state.size);
    assert(0 <= mutex && mutex < l_state.num_mutexes[proc]);

    /* locate remote lock */
    dst_reg = reg_cache_find(proc, &(l_state.mutexes[proc][mutex]), sizeof(unsigned long));
    assert(dst_reg);

    do {    
        dmapp_status = dmapp_acswap_qw(l_state.local_mutex, 
                &(l_state.mutexes[proc][mutex]),
                &(dst_reg->mr),
                proc, 0, l_state.rank + 1);
        assert(dmapp_status == DMAPP_RC_SUCCESS);
    }
    while(*(l_state.local_mutex) != 0);
}


void PARMCI_Unlock(int mutex, int proc)
{
    int dmapp_status;
    reg_entry_t *dst_reg = NULL;

    /* preconditions */
    assert(0 <= proc && proc < l_state.size);
    assert(0 <= mutex && mutex < l_state.num_mutexes[proc]);

    dst_reg = reg_cache_find(proc, &(l_state.mutexes[proc][mutex]), sizeof(unsigned long));
    assert(dst_reg);

    do {
        dmapp_status = dmapp_acswap_qw(l_state.local_mutex, 
                &(l_state.mutexes[proc][mutex]),
                &(dst_reg->mr),
                proc, l_state.rank + 1, 0);
        assert(dmapp_status == DMAPP_RC_SUCCESS);
    }
    while (*(l_state.local_mutex) != l_state.rank + 1);
}


void ARMCI_Set_shm_limit(unsigned long shmemlimit)
{
    assert(0);
}


/* Shared memory not implemented */
int ARMCI_Uses_shm()
{
    return 0;
}


int ARMCI_Uses_shm_group()
{
    return 0;
}


/* Is it memory copy? */
void PARMCI_Copy(void *src, void *dst, int n)
{
    assert(0);
    memcpy(src, dst, sizeof(int) * n);
}


/* Group Functions */
int ARMCI_Uses_shm_grp(ARMCI_Group *group)
{
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
    dmapp_seg_desc_t heap_seg;
    dmapp_seg_desc_t *allgather_heap_seg = NULL;
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
    ptrs[comm_rank] = _PARMCI_Malloc_local(sizeof(char)*max_size, &heap_seg);
  
    /* exchange buffer address */
    /* @TODO: Consider using MPI_IN_PLACE? */
    memcpy(&src_buf, &ptrs[comm_rank], sizeof(void *));
    MPI_Allgather(&src_buf, sizeof(void *), MPI_BYTE, ptrs,
            sizeof(void *), MPI_BYTE, comm);

    /* allocate receive buffer for exchange of registration info */
    allgather_heap_seg = (dmapp_seg_desc_t *)my_malloc(
            sizeof(dmapp_seg_desc_t) * comm_size);
    assert(allgather_heap_seg);

    /* exchange registration info */
    MPI_Allgather(&heap_seg, sizeof(dmapp_seg_desc_t), MPI_BYTE,
            allgather_heap_seg, sizeof(dmapp_seg_desc_t), MPI_BYTE, comm); 

    /* insert this info into registration cache */
    for (i = 0; i < comm_size; ++i) {
        int world_rank = ARMCI_Absolute_id(group, i);
        if (i == comm_rank)
            continue;
        reg_cache_insert(world_rank, ptrs[i], size, allgather_heap_seg[i]);
    }

    // Free the temporary buffer
    my_free(allgather_heap_seg);

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


/* Locality functions */


int armci_domain_nprocs(armci_domain_t domain, int id)
{
    return 1;
}


int armci_domain_id(armci_domain_t domain, int glob_proc_id)
{
    return glob_proc_id;
}


int armci_domain_glob_proc_id(armci_domain_t domain, int id, int loc_proc_id)
{
    return id;
}


int armci_domain_my_id(armci_domain_t domain)
{
    assert(initialized);

    return l_state.rank;
}


int armci_domain_count(armci_domain_t domain)
{
    assert(initialized);
    return l_state.size;
}


int armci_domain_same_id(armci_domain_t domain, int proc)
{
    int rc = (proc == l_state.rank);
    return rc;
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


static void check_envs(void)
{
    char *value;

    /* PARMCI_GEMINI_DMAPP_ROUTING
     *
     * TODO description */
    if ((value = getenv("PARMCI_GEMINI_DMAPP_ROUTING")) != NULL){
        l_state.dmapp_routing = (atoi(value));
    }
    else {
        l_state.dmapp_routing = DMAPP_ROUTING_ADAPTIVE;
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

    /* HUGETLB_DEFAULT_PAGE_SIZE
     *
     * controls the page size that will be used for hugepages
     * we look for this value in case it is specified and we want to allocate
     * memory aligned to the same page size */
    if ((value = getenv("HUGETLB_DEFAULT_PAGE_SIZE")) != NULL){
        /* must be one of [128K|512K|2M|8M|16M|64M] */
        if (0 == strncasecmp(value, "128K", 4)) {
            hugetlb_default_page_size = 131072;
        }
        else if (0 == strncasecmp(value, "512K", 4)) {
            hugetlb_default_page_size = 524288;
        }
        else if (0 == strncasecmp(value, "2M", 2)) {
            hugetlb_default_page_size = 2097152;
        }
        else if (0 == strncasecmp(value, "8M", 2)) {
            hugetlb_default_page_size = 8388608;
        }
        else if (0 == strncasecmp(value, "16M", 3)) {
            hugetlb_default_page_size = 16777216;
        }
        else if (0 == strncasecmp(value, "64M", 3)) {
            hugetlb_default_page_size = 67108864;
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

//#if DEBUG
#if 1
    if (0 == l_state.rank) {
        printf("gethugepagesize()=%ld\n", hugepagesize);
        printf("hugetlb_default_page_size=%ld\n", hugetlb_default_page_size);
        printf("_SC_PAGESIZE=%ld\n", sc_page_size);
        printf("armci_page_size=%ld\n", armci_page_size);
        printf("armci_is_using_huge_pages=%d\n", armci_is_using_huge_pages);
        printf("malloc_is_using_huge_pages=%d\n", malloc_is_using_huge_pages);
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
    assert(MAX_NB_OUTSTANDING > DMAPP_MIN_OUTSTANDING_NB);
    assert(MAX_NB_OUTSTANDING < DMAPP_MAX_OUTSTANDING_NB);
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
    requested_attrs.offload_threshold = ARMCI_DMAPP_OFFLOAD_THRESHOLD;

    /* Specifies the type of routing to be used. Applies to RMA requests with
     * PUT semantics and all AMOs. The default is DMAPP_ROUTING_ADAPTIVE.
     * The value can be specified at any time. Note that
     * DMAPP_ROUTING_IN_ORDER guarantees the requests arrive in order and may
     * result in poor performance.  Valid settings are:
     * - DMAPP_ROUTING_IN_ORDER
     * - DMAPP_ROUTING_DETERMINISTIC
     * - DMAPP_ROUTING_ADAPTIVE */
    requested_attrs.put_relaxed_ordering = l_state.dmapp_routing;

    /* Specifies the type of routing to be used. Applies to RMA requests with
     * GET semantics. The default is DMAPP_ROUTING_ADAPTIVE. The value can be
     * specified at any time. Note that DMAPP_ROUTING_IN_ORDER may result in
     * poor performance. Valid settings are:
     * - DMAPP_ROUTING_IN_ORDER
     * - DMAPP_ROUTING_DETERMINISTIC
     * - DMAPP_ROUTING_ADAPTIVE */
    requested_attrs.get_relaxed_ordering = l_state.dmapp_routing;

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

    // Create locks
    create_dmapp_locks();

    /* Synchronize */
    MPI_Barrier(l_state.world_comm);
}


static void dmapp_terminate(void)
{
    int status;

    destroy_dmapp_locks();
   
    dmapp_free_buf();

    reg_cache_destroy(l_state.size); 
    
    status = dmapp_finalize();
    assert(status == DMAPP_RC_SUCCESS);

    MPI_Barrier(l_state.world_comm);
}


int ARMCI_Same_node(int proc)
{
    return 0;
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

