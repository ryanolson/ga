#ifndef ARMCI_IMPL_H_
#define ARMCI_IMPL_H_

#include <dmapp.h>
#include <mpi.h>

#define ARMCI_DMAPP_OFFLOAD_THRESHOLD 2048
#define MAX_NB_OUTSTANDING 1024

extern int armci_nproc, armci_me;
extern int armci_uses_shm;          /* is SHM (i.e. XPMEM) in use? */

extern int armci_npes_on_smp;       /* number of local SMP ranks  */
extern int *armci_pes_on_smp;       /* array of local SMP ranks   */
extern int armci_smp_indexl;        /* our index within local SMP */
extern int armci_rank_order;        /* Rank reorder method: smp (block cyclic) default */

extern int *armci_pe_node;          /* array to convert from PE to logical node idx */
extern int *armci_node_leader;      /* array to convert from node idx to leader PE rank */
extern int armci_npes_per_node;     /* maximum pes per node of the job */

extern int armci_clus_me;           /* my node index */
extern int armci_nclus;             /* number of nodes that make up job */

extern ARMCI_Group armci_smp_group; /* ARMCI group for local SMP ranks */

typedef struct {

    MPI_Comm world_comm;
    int rank;
    int size;

    /* DMAPP Specific */
    dmapp_jobinfo_t job;

    /* buffers for locks */
    unsigned long **atomic_lock_buf; /**< internal lock, one per process */
    unsigned long *local_lock_buf; /**< holds value of remote lock locally */
    unsigned long **mutexes; /**< all mutexes */
    unsigned long *local_mutex; /**< store the remote mutex value */
    unsigned int  *num_mutexes; /**< how many mutexes on each process */

    /* fallback buffers when errors are detected */
    void *acc_buf;
    int acc_buf_len;
    void *put_buf;
    int put_buf_len;
    void *get_buf;
    int get_buf_len;

    /* envs */ 
    int dmapp_routing;
} local_state;

extern local_state l_state;

#endif /* ARMCI_IMPL_H_ */
