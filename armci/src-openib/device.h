#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <assert.h>
#include <string.h>

// Device specific functions
 int ARMCID_put_nbi(void *src, void *dst, int bytes, int proc);

 int ARMCID_get_nbi(void *src, void *dst, int bytes, int proc);

 void ARMCID_network_lock(int proc);

 void ARMCID_network_unlock(int proc);

void* ARMCID_register_memory(void *buf, int len);

