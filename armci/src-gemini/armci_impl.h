#ifndef ARMCI_IMPL_H_
#define ARMCI_IMPL_H_

#include <dmapp.h>
#include <mpi.h>
#if HAVE_XPMEM
#include <xpmem.h>
#endif

#define ARMCI_DMAPP_OFFLOAD_THRESHOLD 2048
#define MAX_NB_OUTSTANDING 1024

extern void armci_init_clusinfo(void);

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

/* Optimised memcpy implementation */
extern void *(*_cray_armci_memcpy)(void *dest, const void *src, size_t n);
extern int armci_use_system_memcpy;

/* Convert rank to node index. Supports Block(1) and Cyclic(0) layouts only */
#define ARMCI_RANK2NODE(P) ((armci_rank_order == 1) ? (P)/armci_npes_per_node : (P)%armci_nclus)

/* Convert rank to local SMP index. Supports Block(1) and Cyclic(0) layouts only */
#define ARMCI_RANK2LINDEX(P) ((armci_rank_order == 1) ? (P)%armci_npes_per_node : (P)/armci_nclus)

/* Convert Node & Local index back into a PE rank */
#define ARMCI_RANK(N,L) ((armci_rank_order == 1) ? ((N)*armci_npes_per_node)+(L) : ((L)*armci_nclus)+(N))

#define ARMCI_SAMECLUSNODE(P) (ARMCI_RANK2NODE(P) == armci_clus_me)

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

typedef struct armci_mr_info {
    dmapp_seg_desc_t seg;     /* DMAPP memory registration seg */
    long             length;  /* length of the data segment */
#if HAVE_XPMEM
    xpmem_segid_t    segid;   /* xpmem handle for data segment of remote PE */
    xpmem_apid_t     apid;    /* access permit ID for that data segment */
    void             *vaddr;  /* vaddr at which the XPMEM mapping was created */
#endif
} armci_mr_info_t;

#endif /* ARMCI_IMPL_H_ */
