#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <assert.h>
#include <string.h>

#include "armci.h"
#include "armci_impl.h"
#include "parmci.h"

int PARMCI_Put(void *src, void *dst, int bytes, int proc)
{
    assert(src && dst && bytes);
    ARMCID_put_nbi(src, dst, bytes, proc);
    ARMCID_waitproc(proc);
    return 0;
}

int PARMCI_Get(void *src, void *dst, int bytes, int proc)
{
    ARMCID_get_nbi(src, dst, bytes, proc);
    ARMCID_waitproc(proc);

    return 0;
}

int   PARMCI_Acc(int datatype, void *scale,
                 void *src_ptr,
                 void *dst_ptr,
                 int bytes, int proc)
{
    double *get_buf = (double *)l_state.acc_buf;
    double *_src_buf = (double *)src_ptr;
    double calc_scale = *(double *)scale;
    int m, limit;


    assert(bytes <= l_state.acc_buf_len);
    assert(datatype == ARMCI_ACC_DBL);
    assert(get_buf);

    ARMCID_network_lock(proc);
    PARMCI_Get(dst_ptr, get_buf, bytes, proc);

    for (m=0, limit=bytes/sizeof(double); m<limit; ++m) {
        if (calc_scale == 1.0) {
            get_buf[m] += _src_buf[m];
        }
        else {
            get_buf[m] += calc_scale * _src_buf[m];
        }
    }

    PARMCI_Put(get_buf, dst_ptr, bytes, proc);
    ARMCID_network_unlock(proc);
    
    return 0;
}


int   PARMCI_Put_flag(void *src, void* dst, int size, int *flag, int value, int proc)
{
    fprintf(stderr, "ARMCI Put Flag not supported\n");
    assert(0);
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

