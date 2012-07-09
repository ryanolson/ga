#ifndef _ARMCI_OPENIB_H_
#define _ARMCI_OPENIB_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <infiniband/verbs.h>
#include <errno.h>
#include <assert.h>

#include "mpi.h"

#define MEMCPY_ITERS            (10000)
#define MAX_PORT_NUM            (2)
#define DEFAULT_PORT            (1)
#define MEMCPY_SIZE             (16 * 1024 * 1024)
#define MAX_QP_PER_PORT         (1)
#define MAX_SUBCHANNELS         (MAX_PORTS * MAX_QP_PER_PORT)
#define DEFAULT_MSG_SIZE        (8 * 1048576)
#define DEFAULT_BW_ITERS        (200)
#define DEFAULT_ITERATIONS      (10000)
#define DEFAULT_NUM_CQE         (4000)
#define DEFAULT_NUM_OUST_RECV   (2000)
#define DEFAULT_NUM_OUST_SEND   (2000)
#define DEFAULT_NUM_SGE         (1)
#define DEFAULT_WINDOW_SIZE     (32)
#define ALIGNMENT               (64)
#define STRIPING_THRESHOLD (16 * 1024)
#define MHZ 238
#define SKIP 10
#define HOSTNAME_LEN 255

#define ARMCI_OPENIB_SUCCESS 0
#define ARMCI_OPENIB_FAILURE 1

#define ARMCI_INT 0
#define ARMCI_DOUBLE 1
#define ARMCI_FLOAT 2
#define ARMCI_LONG 3
#define ARMCI_LONG_LONG 4

#define MAX_PORTS 2

#if 1 
#define D_PRINT(fmt, args...)   {fprintf(stdout, "[%d][%s:%d]", armci_info.myid, __FILE__, __LINE__);\
                         fprintf(stdout, fmt, ## args); fflush(stdout);}
#else
#define D_PRINT(fmt, args...)
#endif

struct User_Opts
{
    int     msg_size;
    int     iterations;
    int     rdma_flag;
    int     sendrecv_flag;
    char    *ib_devname;
    int     all_msgs;
    int     align;
    int     num_cqe;
    int     send_wr;
    int     recv_wr;
    int     num_sge;
    int     latency;
    int     bandwidth;
    int     bibandwidth;
    int     window;
    int     num_ports;
    int     num_qp_per_port;
    int     striping_threshold;
    int     subchannels;
    int     regcost;
    int     memperf;
    int     default_port;
    int     use_apm;
    int     apm_test;
    int     test_nft;
    int     use_srq;
};


struct HCA
{
    struct ibv_device   *ib_dev;
    struct ibv_context  *context;
    struct ibv_pd       *pd;
    struct ibv_cq       *cq;
    struct ibv_cq       *server_cq;
    struct ibv_cq       *client_cq;
    struct ibv_srq      *srq_hndl;
};

struct Local_Buf
{
    char            *buf_original;
    char            *buf;
    char            *tmp;
    struct ibv_mr   *mr;
};

struct Remote_Buf
{
    char            **buf;
    uint32_t        *server_to_client_qp_num;
    uint32_t        *client_to_server_qp_num;
    uint32_t        *qp_num;
    uint16_t        *lid;
    uint32_t        *rkey;
};

struct RC_Conn
{
    struct ibv_qp       **qp;
    uint16_t            *lid;
    uint32_t        *qp_num;
};

struct armci_info_t
{
    int myid;
    int nprocs;
    
    int nclusters;
    int my_clusid;
    
    int master;
    
    int armci_clus_first;
    int armci_clus_last;
};

struct OPENIB_param
{
    unsigned long int viadev_dreg_cache_limit;
    unsigned int      viadev_ndreg_entries;
    unsigned int      viadev_use_dreg_cache;
    int                 viadev_vbuf_pool_size;
};

typedef struct{
    int data[4]; /* tag, bufid, agg_flag, op, proc */
    double dummy[72];
} armci_hdl_t;

typedef struct {
    void **src_ptr_ar;  
    void **dst_ptr_ar;  
    int bytes;         
    int ptr_ar_len;    
} armci_giov_t;

typedef struct {
    int master;
    int nslave;
    char hostname[HOSTNAME_LEN];
} armci_clus_t;

typedef unsigned long aint_t;

extern struct RC_Conn      conn;
extern struct RC_Conn      server_to_client_conn;
extern struct RC_Conn      client_to_server_conn;

extern struct User_Opts    opts;
extern struct HCA          hca;
extern struct Local_Buf    lbuf;
extern struct Remote_Buf   rbuf;

extern struct OPENIB_param openib_param;

extern struct armci_info_t armci_info;

extern armci_clus_t *armci_clus_info;
typedef int armci_domain_t;

void async_thread_func(void *context);

/* Init Finalize Functions */
int ARMCI_Init();
int ARMCI_Finalize();
void ARMCI_Cleanup();
void ARMCI_Error();

/* ARMCI Put based data transfer functions */
int ARMCI_Put(void *src, void* dst, int bytes, int proc);
int ARMCI_NbPut(void *src, void* dst, int bytes, int proc,
                armci_hdl_t* nb_handle);
int ARMCI_PutS(void* src_ptr, int src_stride_ar[], void* dst_ptr, int dst_stride_ar[], 
                       int count[], int stride_levels, int proc);
int ARMCI_NbPutS(void* src_ptr, int src_stride_ar[], void* dst_ptr, int dst_stride_ar[], 
                         int count[], int stride_levels, int proc,armci_hdl_t* handle);
int ARMCI_NbPutV(armci_giov_t *dsrc_arr, int arr_len, int proc, armci_hdl_t*
                handle);
int ARMCI_PutV(armci_giov_t *dsrc_arr, int arr_len, int proc);

/* ARMCI Get based data transfer functions */
/* Wait and Progress Functions */
int ARMCI_Wait(armci_hdl_t *nb_handle);
int ARMCI_Test(armci_hdl_t *nb_handle);
int ARMCI_WaitAll();
int ARMCI_WaitProc(int proc);
int ARMCI_Fence(int proc);
int ARMCI_AllFence();

int ARMCI_NbPutS();

/* pt2pt data trasnfer functions */
void armci_msg_snd(int tag, void* buffer, int len, int to);
void armci_msg_rcv(int tag, void* buffer, int buflen, int *msglen, int from);

/* Domain based functions */
int armci_domain_nprocs(armci_domain_t domain, int id);
int armci_domain_count(armci_domain_t domain);
int armci_domain_id(armci_domain_t domain, int glob_proc_id);
int armci_domain_glob_proc_id(armci_domain_t domain, int id, int loc_proc_id);
int armci_domain_my_id(armci_domain_t domain);
int armci_domain_same_id (armci_domain_t domain, int proc);

/* Collective Communication Functions */
void armci_msg_brdcst(void* buffer, int len, int root);
void armci_msg_gop2(void *x, int n, int type, char* op);
void armci_msg_barrier(void);
void armci_msg_reduce(void *x, int n, char* op, int type, int root);
int ARMCI_Barrier();

/* Other Misc Functions */
void release_resources(int, int);
void armci_init_clusinfo(void);

#endif
