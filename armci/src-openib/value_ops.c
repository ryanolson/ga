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

