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

/* 3rd party headers */
#include <mpi.h>

/* our headers */
#include "armci.h"
#include "armci_impl.h"
#include "groups.h"
#include "parmci.h"

#define DEBUG 1
#define DEBUG_TO_FILE 1
#if DEBUG_TO_FILE
#   define printf(...) fprintf(my_file, __VA_ARGS__); fflush(my_file);
#endif


/* exported state */
local_state l_state;
int armci_me=-1;
int armci_nproc=-1;
MPI_Comm ARMCI_COMM_WORLD;

/* static state */
static int initialized=0; /* for PARMCI_Initialized(), 0=false */
static size_t total_outstanding=0;
static MPI_Request mpi_requests[MAX_NB_OUTSTANDING];
static MPI_Status mpi_statuses[MAX_NB_OUTSTANDING];
static void *messages[MAX_NB_OUTSTANDING];
static int get_response[MAX_NB_OUTSTANDING];
#if DEBUG_TO_FILE
static FILE *my_file=NULL;
#endif

/* static function declarations */
static void  _increment_total_outstanding(void);
static void  _my_free(void *ptr);
static void* _my_malloc(size_t size);
static void* _my_memcpy(void *dest, const void *src, size_t n);
static int   _my_memalign(void **memptr, size_t alignment, size_t size);
static int   _get_nbi(void *src, void *dst, int bytes, int proc);
static int   _put_nbi(void *src, void *dst, int bytes, int proc);
static void  _make_progress(void);
static void  _put_handler(header_t *header, char *payload);
static void  _get_request_handler(header_t *header, int proc);
static void  _get_response_handler(header_t *header, char *payload);

/* needed for complex accumulate */
typedef struct {
    double real;
    double imag;
} DoubleComplex;

/* needed for complex accumulate */
typedef struct {
    float real;
    float imag;
} SingleComplex;


static void* _my_memcpy(void *dest, const void *src, size_t n)
{
#if DEBUG
    //if (0 == l_state.rank) {
        printf("[%d] _my_memcpy(dest=%p, src=%p, n=%zu)\n",
                l_state.rank, dest, src, n);
    //}
#endif
    return memcpy(dest, src, n);
}


static void* _my_malloc(size_t size)
{
    void *memptr=NULL;

    memptr = malloc(size);

#if DEBUG
    //if (0 == l_state.rank) {
        printf("[%d] _my_malloc(%zu) -> %p\n", l_state.rank, size, memptr);
    //}
#endif

    /* postconditions */
    assert(memptr);

    return memptr;
}


static void _my_free(void *ptr)
{
#if DEBUG
    //if (0 == l_state.rank) {
        printf("[%d] _my_free(%p)\n", l_state.rank, ptr);
    //}
#endif

    free(ptr);
}


static int _my_memalign(void **memptr, size_t alignment, size_t size)
{
    int status = 0;

#if DEBUG
    //if (0 == l_state.rank) {
        printf("[%d] _my_memalign(%lu)\n", l_state.rank, (long unsigned)size);
    //}
#endif

    /* preconditions */
    assert(memptr);

    status = posix_memalign(memptr, alignment, size);

    /* postconditions */
    assert(*memptr);

    return status;
}


static void _increment_total_outstanding(void)
{
    ++total_outstanding;

    if (total_outstanding == MAX_NB_OUTSTANDING) {
        _make_progress();
        total_outstanding = 0;
    }
}


static void _make_progress(void)
{
    int iprobe_flag=0;
    MPI_Status iprobe_status;
    int get_flag=0;
    size_t i=0;

#if DEBUG
    printf("[%d] _make_progress()\n", l_state.rank);
#endif

    do {
        iprobe_flag = 0;
        get_flag = 0;

        /* test for outgoing message completion only if we have outgoing messages */
        if (total_outstanding) {
            size_t i=0;
            int testall_flag=0;

            MPI_Testall(total_outstanding, mpi_requests,
                    &testall_flag, mpi_statuses);
            if (testall_flag) {
                /* outgoing messages have completed, reset outgoing message queues */
                for (i=0; i<total_outstanding; ++i) {
                    mpi_requests[i] = MPI_REQUEST_NULL;
                    _my_free(messages[i]);
                }
                total_outstanding = 0;
            }
        }

        /* test for incoming get responses */
        for (i=0; i<MAX_NB_OUTSTANDING; ++i) {
            if (get_response[i]) {
                get_flag = 1;
                break;
            }
        }

        /* test for incoming messages */
        MPI_Iprobe(MPI_ANY_SOURCE, ARMCI_TAG, l_state.world_comm,
                &iprobe_flag, &iprobe_status);
        if (iprobe_flag) {
            int length;
            char *message;
            char *payload;
            header_t *header;
            MPI_Status recv_status;

            /* allocate message buffer and get message */
            MPI_Get_count(&iprobe_status, MPI_CHAR, &length);
#if DEBUG
            printf("[%d] iprobe source=%d length=%d\n",
                    l_state.rank, iprobe_status.MPI_SOURCE, length);
#endif

            message = _my_malloc(length);
            MPI_Recv(message, length, MPI_CHAR, iprobe_status.MPI_SOURCE, ARMCI_TAG,
                    l_state.world_comm, &recv_status);
            header = (header_t*)message;
            payload = message + sizeof(header_t);
            /* dispatch message handler */
            switch (header->operation) {
                case OP_PUT:
                    _put_handler(header, payload);
                    break;
                case OP_GET_REQUEST:
                    _get_request_handler(header, iprobe_status.MPI_SOURCE);
                    break;
                case OP_GET_RESPONSE:
                    _get_response_handler(header, payload);
                    break;
                case OP_ACC:
                    assert(0);
                    break;
                case OP_FENCE_REQUEST:
                    assert(0);
                    break;
                case OP_FENCE_RESPONSE:
                    assert(0);
                    break;
                case OP_FETCH_AND_ADD:
                    assert(0);
                    break;
                case OP_SWAP:
                    assert(0);
                    break;
                default:
                    printf("[%d] header operation not recognized: %d\n",
                            l_state.rank, header->operation);
                    assert(0);
            }

            /* free the message buffer */
            _my_free(message);
        }
        /* loop until we run out of incoming and outgoing messages */
    } while (iprobe_flag || get_flag || total_outstanding);
}


static void _put_handler(header_t *header, char *payload)
{
#if DEBUG
    printf("[%d] _put_handler rem=%p loc=%p len=%d not=%p\n",
            l_state.rank,
            header->remote_address,
            header->local_address,
            header->length,
            header->notify_address);
#endif

    assert(OP_PUT == header->operation);
    _my_memcpy(header->remote_address, payload, header->length);
}


static void _get_request_handler(header_t *request_header, int proc)
{
    char *message;
    int rc;

#if DEBUG
    printf("[%d] _get_request_handler proc=%d\n", l_state.rank, proc);
#endif
#if DEBUG
    printf("[%d] request_header rem=%p loc=%p len=%d not=%p\n",
            l_state.rank,
            request_header->remote_address,
            request_header->local_address,
            request_header->length,
            request_header->notify_address);
#endif

    assert(OP_GET_REQUEST == request_header->operation);
    
    /* reuse the header, just change the OP */
    /* NOTE: it gets deleted anyway when we return from this handler */
    request_header->operation = OP_GET_RESPONSE;

    message = _my_malloc(sizeof(header_t) + request_header->length);
    _my_memcpy(message, request_header, sizeof(header_t));
    _my_memcpy(message+sizeof(header_t), request_header->remote_address,
            request_header->length);

    messages[total_outstanding] = message;
    rc = MPI_Isend(messages[total_outstanding],
            sizeof(header_t) + request_header->length, MPI_CHAR,
            proc, ARMCI_TAG, l_state.world_comm, &mpi_requests[total_outstanding]);
    assert(MPI_SUCCESS == rc);
    _increment_total_outstanding();
}


static void  _get_response_handler(header_t *header, char *payload)
{
#if DEBUG
    printf("[%d] _get_response_handler rem=%p loc=%p len=%d not=%p\n",
            l_state.rank,
            header->remote_address,
            header->local_address,
            header->length,
            header->notify_address);
#endif

    assert(OP_GET_RESPONSE == header->operation);
    _my_memcpy(header->local_address, payload, header->length);
    *(header->notify_address) = 0;
}


int PARMCI_Put(void *src, void *dst, int bytes, int proc)
{
#if DEBUG
    printf("[%d] PARMCI_Put(src=%p, dst=%p, bytes=%d, proc=%d)\n",
            l_state.rank, src, dst, bytes, proc);
#endif
    _put_nbi(src, dst, bytes, proc);
    PARMCI_WaitProc(proc);
    return 0;
}


int PARMCI_Get(void *src, void *dst, int bytes, int proc)
{
#if DEBUG
    printf("[%d] PARMCI_Get(src=%p, dst=%p, bytes=%d, proc=%d)\n",
            l_state.rank, src, dst, bytes, proc);
#endif
    _get_nbi(src, dst, bytes, proc);
    PARMCI_WaitProc(proc);
    return 0;
}


static int _put_nbi(void *src, void *dst, int bytes, int proc)
{
    int rc = 0;
    header_t header;
    char *message;

#if DEBUG
    printf("[%d] _put_nbi(src=%p, dst=%p, bytes=%d, proc=%d)\n",
            l_state.rank, src, dst, bytes, proc);
#endif

    /* Corner case */
    if (proc == l_state.rank) {
        _my_memcpy(dst, src, bytes);
        return 0;
    }

    header.operation = OP_PUT;
    header.remote_address = dst;
    header.local_address = src;
    header.length = bytes;
    header.notify_address = NULL;

    message = _my_malloc(sizeof(header_t) + bytes);
    _my_memcpy(message, &header, sizeof(header_t));
    _my_memcpy(message+sizeof(header_t), src, bytes);

    messages[total_outstanding] = message;
    rc = MPI_Isend(messages[total_outstanding], sizeof(header_t) + bytes, MPI_CHAR,
            proc, ARMCI_TAG, l_state.world_comm, &mpi_requests[total_outstanding]);
    assert(MPI_SUCCESS == rc);
    _increment_total_outstanding();

    return 0;
}


static int _get_nbi(void *src, void *dst, int bytes, int proc)
{
    int rc = 0;
    header_t header;
    char *message;

#if DEBUG
    printf("[%d] _get_nbi(src=%p, dst=%p, bytes=%d, proc=%d)\n",
            l_state.rank, src, dst, bytes, proc);
#endif

    /* Corner case */
    if (proc == l_state.rank) {
        _my_memcpy(dst, src, bytes);
        return 0;
    }

    header.operation = OP_GET_REQUEST;
    header.remote_address = dst;
    header.local_address = src;
    header.length = bytes;
    header.notify_address = &get_response[total_outstanding];

    /* set the value to wait on for a get response */
    get_response[total_outstanding] = 1;

    /* get request is a header-only message */
    message = _my_malloc(sizeof(header_t));
    _my_memcpy(message, &header, sizeof(header_t));

    messages[total_outstanding] = message;
    rc = MPI_Isend(messages[total_outstanding], sizeof(header_t), MPI_CHAR,
            proc, ARMCI_TAG, l_state.world_comm, &mpi_requests[total_outstanding]);
    assert(MPI_SUCCESS == rc);
    _increment_total_outstanding();

    return 0;
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
    int k = 0, rc;

#if DEBUG
    if (stride_levels) {
        printf("[%d] PARMCI_PutS(src_ptr=%p, src_stride_ar[0]=%d, dst_ptr=%p, dst_stride_ar[0]=%d, count[0]=%d, stride_levels=%d, proc=%d)\n",
                l_state.rank, src_ptr, src_stride_ar[0], dst_ptr, dst_stride_ar[0], count[0], stride_levels, proc);
    }
    else {
        printf("[%d] PARMCI_PutS(src_ptr=%p, src_stride_ar=NULL, dst_ptr=%p, dst_stride_ar=NULL, count=NULL, stride_levels=%d, proc=%d)\n",
                l_state.rank, src_ptr, dst_ptr, stride_levels, proc);
    }
#endif

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
        
        _put_nbi((char *)src_ptr + src_idx, 
                (char *)dst_ptr + dst_idx, count[0], proc);
    }

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
    int k = 0;

#if DEBUG
    if (stride_levels) {
        printf("[%d] PARMCI_GetS(src_ptr=%p, src_stride_ar[0]=%d, dst_ptr=%p, dst_stride_ar[0]=%d, count[0]=%d, stride_levels=%d, proc=%d)\n",
                l_state.rank, src_ptr, src_stride_ar[0], dst_ptr, dst_stride_ar[0], count[0], stride_levels, proc);
    }
    else {
        printf("[%d] PARMCI_GetS(src_ptr=%p, src_stride_ar=NULL, dst_ptr=%p, dst_stride_ar=NULL, count=NULL, stride_levels=%d, proc=%d)\n",
                l_state.rank, src_ptr, dst_ptr, stride_levels, proc);
    }
#endif

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
        
        _get_nbi((char *)src_ptr + src_idx, 
                (char *)dst_ptr + dst_idx, count[0], proc);
    }
    
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
    double calc_scale = *(double *)scale;
    int sizetogetput;
    void *get_buf;
    long k = 0;

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
        get_buf = (char *)_my_malloc(sizeof(char) * sizetogetput);
    }

    assert(get_buf);

    // grab the atomics lock
    assert(0);

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
    assert(0);

    if (sizetogetput > l_state.acc_buf_len)
        _my_free(get_buf);

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
}


void PARMCI_Fence(int proc)
{
    PARMCI_WaitAll();
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


void *PARMCI_Malloc_local(armci_size_t size)
{
    void *ptr = NULL;

    ptr = _my_malloc(size);

    return ptr;
}


int PARMCI_Free_local(void *ptr)
{
    /* preconditions */
    assert(NULL != ptr);

    /* free the memory */
    _my_free(ptr);
}


static void _alloc_buf(void)
{
    l_state.acc_buf_len = FAILURE_BUFSIZE;
    l_state.acc_buf = PARMCI_Malloc_local( l_state.acc_buf_len);
    assert(l_state.acc_buf);

    l_state.put_buf_len = FAILURE_BUFSIZE;
    l_state.put_buf = PARMCI_Malloc_local(l_state.put_buf_len);
    assert(l_state.put_buf);

    l_state.get_buf_len = FAILURE_BUFSIZE;
    l_state.get_buf = PARMCI_Malloc_local(l_state.get_buf_len);
    assert(l_state.get_buf);
}


static void _free_buf(void)
{
    PARMCI_Free_local(l_state.acc_buf);
    PARMCI_Free_local(l_state.put_buf);
    PARMCI_Free_local(l_state.get_buf);
}


int PARMCI_Init()
{
    int status;
    int init_flag;
    size_t i;
    
    if (initialized) {
        return 0;
    }
    initialized = 1;

    /* Assert MPI has been initialized */
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
    
    /* groups */
    armci_group_init();

    /* static state */
    for (i=0; i<MAX_NB_OUTSTANDING; ++i) {
        mpi_requests[i] = MPI_REQUEST_NULL;
        memset(&mpi_statuses[i], 0, sizeof(MPI_Status)); /* is this correct? */
        messages[i] = NULL;
        get_response[i] = 0;
    }
#if DEBUG_TO_FILE
    {
        char pathname[80];
        sprintf(pathname, "trace.%d.log", l_state.rank);
        my_file = fopen(pathname, "w");
    }
#endif

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
    PARMCI_WaitAll();
    return 0;
}


int   PARMCI_Wait(armci_hdl_t* hdl)
{
    PARMCI_WaitAll();
    return 0;
}


int   PARMCI_Test(armci_hdl_t* hdl)
{
    PARMCI_WaitAll();
    return 0;
}


int   PARMCI_WaitAll()
{
    _make_progress();
    return 0;
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


/* Notify wait functions, not implemented yet */
int parmci_notify(int proc)
{
    assert(0);
}


/* Always return 0, since shared memory not implemented yet */
int PARMCI_Same_node(int proc)
{
    return 0;
}


int  PARMCI_Rmw(int op, void *ploc, void *prem, int extra, int proc)
{
    assert(0);
    int status;
    if (op == ARMCI_FETCH_AND_ADD) {
        int tmp;
        _network_lock(proc);
        PARMCI_Get(prem, ploc, sizeof(int), proc);
        tmp = *(int*)ploc + extra;
        PARMCI_Put(&tmp, prem, sizeof(int), proc);
        _network_unlock(proc);
    }
    else if (op == ARMCI_FETCH_AND_ADD_LONG) {
        long tmp;
        _network_lock(proc);
        PARMCI_Get(prem, ploc, sizeof(long), proc);
        tmp = *(long*)ploc + extra;
        PARMCI_Put(&tmp, prem, sizeof(long), proc);
        _network_unlock(proc);
    }
    else if (op == ARMCI_SWAP) {
        int tmp;
        _network_lock(proc);
        PARMCI_Get(prem, &tmp, sizeof(int), proc);
        PARMCI_Put(ploc, prem, sizeof(int), proc);
        _network_unlock(proc);
        *(int*)ploc = tmp;
    }
    else if (op == ARMCI_SWAP_LONG) {
        long tmp;
        _network_lock(proc);
        PARMCI_Get(prem, &tmp, sizeof(long), proc);
        PARMCI_Put(ploc, prem, sizeof(long), proc);
        _network_unlock(proc);
        *(long*)ploc = tmp;
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
    l_state.num_mutexes = (unsigned int*)_my_malloc(l_state.size * sizeof(unsigned int));
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
    l_state.mutexes = (unsigned long**)_my_malloc(l_state.size * sizeof(unsigned long*));
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
    _my_free(l_state.num_mutexes);
    l_state.num_mutexes = NULL;

    /* destroy the 1 element buffer holding a remote mutex */
    PARMCI_Free_local(l_state.local_mutex);
    l_state.local_mutex = NULL;

    /* destroy the mutexes */
    PARMCI_Free(l_state.mutexes[l_state.rank]);
    _my_free(l_state.mutexes);
    l_state.mutexes = NULL;

    MPI_Barrier(l_state.world_comm);
}


void PARMCI_Lock(int mutex, int proc)
{
    assert(0);
}


void PARMCI_Unlock(int mutex, int proc)
{
    assert(0);
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
    _my_memcpy(src, dst, sizeof(int) * n);
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
    int rc = MPI_SUCCESS; 

#if DEBUG
    printf("[%d] ARMCI_Malloc_group(ptrs=%p, size=%ld, ...)\n",
            l_state.rank, ptrs, size);
#endif

    /* preconditions */
    assert(ptrs);
    assert(group);
   
    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);
    MPI_Comm_rank(comm, &comm_rank);

    /* allocate and register segment */
    ptrs[comm_rank] = PARMCI_Malloc_local(sizeof(char)*size);
  
    /* exchange buffer address */
    rc = MPI_Allgather(MPI_IN_PLACE, 0, MPI_LONG, ptrs, 1, MPI_LONG, comm);
    assert(MPI_SUCCESS == rc);
#if DEBUG
    {
        int i;
        int size;
        MPI_Comm_size(comm, &size);
        for (i=0; i<size; ++i) {
            printf("[%d] ptrs[%d]=%p\n", l_state.rank, i, ptrs[i]);
        }
    }
#endif

    MPI_Barrier(comm);

    return 0;
}


int ARMCI_Free_group(void *ptr, ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;

    /* preconditions */
    assert(NULL != ptr);
    assert(NULL != group);

    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;

    /* remove my ptr from reg cache and free ptr */
    PARMCI_Free_local(ptr);

    /* Synchronize: required by ARMCI semantics */
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


int ARMCI_Same_node(int proc)
{
    return 0;
}


int PARMCI_Initialized()
{
    return initialized;
}


int parmci_notify_wait(int proc, int *pval)
{
        assert(0);
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

