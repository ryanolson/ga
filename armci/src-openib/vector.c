#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <assert.h>
#include <string.h>

#include "armci.h"
#include "armci_impl.h"
#include "parmci.h"

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
            assert(src[j] && dst[j] && bytes && limit);
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
