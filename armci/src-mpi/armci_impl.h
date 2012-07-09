#ifndef ARMCI_IMPL_H_
#define ARMCI_IMPL_H_

#include <mpi.h>

#define MAX_NB_OUTSTANDING 1024
#define FAILURE_BUFSIZE 1048576
#define ARMCI_TAG 27624

typedef enum {
    OP_PUT = 0,
    OP_GET_REQUEST,
    OP_GET_RESPONSE,
    OP_ACC,
    OP_FENCE_REQUEST,
    OP_FENCE_RESPONSE,
    OP_FETCH_AND_ADD,
    OP_SWAP,
} op_t;

typedef struct {
    op_t  operation;
    void *remote_address;
    void *local_address;
    int   length;           /**< length of message/payload not including header */
    int  *notify_address;
} header_t;

typedef struct {

    MPI_Comm world_comm;
    int rank;
    int size;

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

} local_state;

extern local_state l_state;

#endif /* ARMCI_IMPL_H_ */
