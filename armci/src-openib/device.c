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
#include "openib.h"

// Device specific implementation
//
void* ARMCID_register_memory(void *buf, int len)
{
    return openib_register_memory(buf, len);
}

int ARMCID_deregister_memory(void *buf)
{
    return openib_deregister_memory(buf);
}

int ARMCID_put_nbi(void *src, void *dst, int bytes, int proc)
{
    if (proc == l_state.rank) 
        return memcpy(dst, src, bytes);

    return openib_put_nbi(src, dst, bytes, proc);
}

int ARMCID_get_nbi(void *src, void *dst, int bytes, int proc)
{
    if (proc == l_state.rank)
        return memcpy(dst, src, bytes);

    return openib_get_nbi(src, dst, bytes, proc);
}

void ARMCID_network_lock(int proc)
{
    openib_network_lock(proc);
}

void ARMCID_network_unlock(int proc)
{
    openib_network_unlock(proc);
}

int ARMCID_waitproc(int proc)
{
    return openib_waitproc(proc);    
}

int ARMCID_waitall()
{
    openib_waitall();    
}

int ARMCID_initialize()
{
    return openib_initialize();
}

int ARMCID_finalize()
{
    return openib_finalize();
}
