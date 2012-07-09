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


int   PARMCI_WaitProc(int proc)
{   
    return ARMCID_waitproc(proc);
}

int   PARMCI_Wait(armci_hdl_t* hdl)
{
    return ARMCID_waitall();
}   

int   PARMCI_Test(armci_hdl_t* hdl)
{
    return ARMCID_waitall();
}

int   PARMCI_WaitAll()
{
    return ARMCID_waitall();
}   

int parmci_notify(int proc)
{
    assert(0);
}

int parmci_notify_wait(int proc, int *pval)
{
    assert(0);
}


