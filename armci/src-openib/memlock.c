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


