#if HAVE_CONFIG_H
#   include "config.h"
#endif

/* C and/or system headers */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

/* 3rd party headers */
#include <mpi.h>
#include <infiniband/verbs.h>

/* our headers */
#include "armci.h"
#include "armci_impl.h"
#include "groups.h"
#include "parmci.h"


/* exported state */
local_state l_state;
int armci_me=-1;
int armci_nproc=-1;
MPI_Comm ARMCI_COMM_WORLD;

/* static state */
static int initialized=0;       /* for PARMCI_Initialized(), 0=false */
static long sc_page_size=-1;    /* from sysconf, in bytes */

/* static function declarations */
static void *_PARMCI_Malloc_local(armci_size_t size, void **rinfo);


void ARMCI_Error(char *msg, int code)
{
    if (0 == l_state.rank) {
        fprintf(stderr,"Received an Error in Communication\n");
    }
    
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


int ARMCI_Malloc_group(void **ptrs, armci_size_t size, ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;
    int comm_size = -1;
    int comm_rank = -1;
    int rc = MPI_SUCCESS;
    void *src_buf = NULL;
    armci_size_t max_size = size;
    void *local_rkey_buf = NULL;
    struct ibv_mr *mr = NULL;
    int *allgather_rkey_buf = NULL;
    int i = 0;

    /* preconditions */
    assert(ptrs);
    assert(group);

    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);
    rc = MPI_Comm_rank(comm, &comm_rank);
    assert(rc == MPI_SUCCESS);
    rc = MPI_Comm_size(comm, &comm_size);
    assert(rc == MPI_SUCCESS);
   
    /* achieve consensus on the allocation size */
    rc = MPI_Allreduce(&size, &max_size, 1, MPI_LONG, MPI_MAX, comm);
    assert(rc == MPI_SUCCESS);
    size = max_size; 

    /* 0 Byte registrations are potential problems */
    if (size < MIN_REGISTRATION_SIZE) {
        size = MIN_REGISTRATION_SIZE;
    }

    /* allocate and register the user level buffer */
    ptrs[comm_rank] = _PARMCI_Malloc_local(sizeof(char)*max_size, &local_rkey_buf);

    /* exchange buffer address */
    /* @TODO: Consider usijng MPI_IN_PLACE? */
    memcpy(&src_buf, &ptrs[comm_rank], sizeof(void *));
    MPI_Allgather(&src_buf, sizeof(void *), MPI_BYTE, ptrs,
            sizeof(void *), MPI_BYTE, comm);
   
    
    /* allocate buffer for collecting remote keys */
    allgather_rkey_buf = (int *)malloc(sizeof(int) * comm_size);
    assert(allgather_rkey_buf);

    /* set the rkey of the local buffer */
    mr = (struct ibv_mr *)local_rkey_buf;
    allgather_rkey_buf[comm_rank] = mr->rkey;
   
    /* exchange rkeys */
    MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
            allgather_rkey_buf, 1, MPI_INT, comm);

    /* insert in the remote registration cache */
    for (i = 0; i < comm_size; i++) {
        int world_rank = ARMCI_Absolute_id(group, i);
        if (i == comm_rank) {
            continue;
        }
        reg_cache_insert(world_rank, ptrs[i], size, -1, allgather_rkey_buf[i], NULL);
    }

    /* free the temporary buffer */
    free(allgather_rkey_buf);

    MPI_Barrier(comm);

    return 0;
}


int ARMCI_Free_group(void *ptr, ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;
    int comm_size = -1;
    int comm_rank = -1;
    int i;
    long **allgather_ptrs = NULL;
    int rc = -1;

    /* preconditions */
    assert(NULL != ptr);
    assert(NULL != group);

    igroup = armci_get_igroup_from_group(group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);
    rc = MPI_Comm_rank(comm, &comm_rank);
    assert(rc == MPI_SUCCESS);
    rc = MPI_Comm_size(comm, &comm_size);
    assert(rc == MPI_SUCCESS);
   
    /* allocate receive buffer for exhange of pointers */
    allgather_ptrs = (long **)malloc(sizeof(void *) * comm_size); 
    assert(allgather_ptrs);

    /* exchange of pointers */
    rc = MPI_Allgather(&ptr, sizeof(void *), MPI_BYTE, 
            allgather_ptrs, sizeof(void *), MPI_BYTE, comm);
    assert(rc == MPI_SUCCESS);

    /* remove all ptrs from registration cache */
    for (i = 0; i < comm_size; i++) {
        int world_rank = ARMCI_Absolute_id(group, i);
        if (i == comm_rank) {
            continue;
        }
        reg_cache_delete(world_rank, allgather_ptrs[i]);
    }
    
    /* remove my ptr from reg cache and free ptr */
    PARMCI_Free_local(ptr); 

    /* Synchronize: required by ARMCI semantics */
    free(allgather_ptrs);
    MPI_Barrier(comm);        
    
    return 0;
}


static void *_PARMCI_Malloc_local(armci_size_t size, void **rinfo)
{
    void *ptr = NULL;
    int rc = 0;

    /* allocate the user level buffer */
    rc = posix_memalign(&ptr, sc_page_size, sizeof(char)*size);
    assert(0 == rc);
    assert(ptr);

    /* register the buffer and check the return info */
    *rinfo = ARMCID_register_memory(ptr, size);
    assert(*rinfo);

    return ptr;
}


void *PARMCI_Malloc_local(armci_size_t size)
{
    void *ptr = NULL;
    void *rinfo = NULL;
    
    ptr = _PARMCI_Malloc_local(size, &rinfo);

    return ptr;
}


int PARMCI_Free_local(void *ptr)
{
    assert(ptr != NULL);

    ARMCID_deregister_memory(ptr);

    free(ptr);
}


int PARMCI_Init()
{
    int init_flag;
    
    /* Test if initialized has been called more than once */ 
    if (initialized) {
        return 0;
    }
    initialized = 1;

    /* Assert MPI has been initialized */
    MPI_Initialized(&init_flag);
    assert(init_flag);
    
    /* Duplicate the World Communicator */
    MPI_Comm_dup(MPI_COMM_WORLD, &(l_state.world_comm));
    MPI_Comm_dup(MPI_COMM_WORLD, &ARMCI_COMM_WORLD);
   
    /* My Rank */
    MPI_Comm_rank(l_state.world_comm, &(l_state.rank));
    armci_me = l_state.rank;

    /* World Size */
    MPI_Comm_size(l_state.world_comm, &(l_state.size));
    armci_nproc = l_state.size;
  
    // init groups
    armci_group_init();

    // Initialize the number of outstanding messages
    l_state.num_outstanding = 0;

    // Get the page size for malloc
    sc_page_size = sysconf(_SC_PAGESIZE);

    // Initialize the ARMCI device 
    ARMCID_initialize(); 

    /* Synch - Sanity Check */
    MPI_Barrier(l_state.world_comm);
   
    return 0;

}

int PARMCI_Init_args(int *argc, char ***argv)
{
    int rc;
    int init_flag;
    
    MPI_Initialized(&init_flag);
    
    if(!init_flag) {
        MPI_Init(argc, argv);
    }
    
    rc = PARMCI_Init();
   
    return rc;
}

void PARMCI_Finalize()
{
    // Make sure that all outstanding operations are done
    PARMCI_WaitAll();

    // Call ARMCI device specific finalize 
    ARMCID_finalize();

    MPI_Barrier(l_state.world_comm);

    // destroys all groups and their communicators
    armci_group_finalize();

    // destroy the primary communicator
    assert(MPI_SUCCESS == MPI_Comm_free(&l_state.world_comm));
    assert(MPI_SUCCESS == MPI_Comm_free(&ARMCI_COMM_WORLD));
}

void ARMCI_Cleanup()
{
    PARMCI_Finalize();
}


/* Always return 0, since shared memory not implemented yet */
int PARMCI_Same_node(int proc)
{
    return 0;
}

int  PARMCI_Rmw(int op, void *ploc, void *prem, int extra, int proc)
{
    int status;
    if (op == ARMCI_FETCH_AND_ADD) {
        ARMCID_network_lock(proc);
        PARMCI_Get(prem, ploc, sizeof(int), proc);
        (*(int *)ploc) += extra;
        PARMCI_Put(ploc, prem, sizeof(int), proc);
        (*(int *)ploc) -= extra;
        ARMCID_network_unlock(proc);
    }
    else if (op == ARMCI_FETCH_AND_ADD_LONG) {
        ARMCID_network_lock(proc);
        PARMCI_Get(prem, ploc, sizeof(long), proc);
        (*(long *)ploc) += extra;
        PARMCI_Put(ploc, prem, sizeof(long), proc);
        (*(long *)ploc) -= extra;
        ARMCID_network_unlock(proc);

    }
    else if (op == ARMCI_SWAP) {
        int tmp;
        ARMCID_network_lock(proc);
        PARMCI_Get(prem, &tmp, sizeof(int), proc);
        PARMCI_Put(ploc, prem, sizeof(int), proc);
        ARMCID_network_unlock(proc);
        *(int*)ploc = tmp;
    }
    else if (op == ARMCI_SWAP_LONG) {
        long tmp;
        ARMCID_network_lock(proc);
        PARMCI_Get(prem, &tmp, sizeof(long), proc);
        PARMCI_Put(ploc, prem, sizeof(long), proc);
        ARMCID_network_unlock(proc);
        *(long*)ploc = tmp;
    }
    else  {
        assert(0);
    }

    
    return 0;
}


/* Shared memory not implemented */
int ARMCI_Uses_shm()
{
    return 0;
}

int ARMCI_Uses_shm_grp(ARMCI_Group *group)
{
    return 0;
}

void ARMCI_Set_shm_limit(unsigned long shmemlimit)
{
}

int ARMCI_Same_node(int proc)
{
    return 0;
}

int PARMCI_Initialized()
{
    return initialized;
}

void ARMCI_SET_AGGREGATE_HANDLE(armci_hdl_t* handle)
{
}

void ARMCI_UNSET_AGGREGATE_HANDLE(armci_hdl_t* handle)
{
}

