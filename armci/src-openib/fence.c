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

void PARMCI_AllFence()
{
    openib_waitall();
}

void PARMCI_Fence(int proc)
{
    openib_waitall();
}

void PARMCI_Barrier()
{
    assert(l_state.world_comm);

    PARMCI_AllFence();
    
    MPI_Barrier(l_state.world_comm);
}

