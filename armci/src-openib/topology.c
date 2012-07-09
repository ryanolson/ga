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
    assert(PARMCI_Initialized());

    return l_state.rank;
}

int armci_domain_count(armci_domain_t domain)
{
    assert(PARMCI_Initialized());

    return l_state.size;
}

int armci_domain_same_id(armci_domain_t domain, int proc)
{
    int rc = (proc == l_state.rank);
    return rc;
}

