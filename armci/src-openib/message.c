#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <mpi.h>

#include "armci.h"
#include "armci_impl.h"
#include "groups.h"
#include "message.h"


/* for armci_msg_sel_scope */
static MPI_Datatype MPI_LONGLONG_INT;


/* undocumented, but used in GA to expose MPI_Comm */
MPI_Comm armci_group_comm(ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(group);
    return igroup->comm;
}


static MPI_Datatype armci_type_to_mpi_type(int type)
{
    MPI_Datatype mpi_dt;

    if (type == ARMCI_INT) {
        mpi_dt = MPI_INT;
    }
    else if (type == ARMCI_LONG) {
        mpi_dt = MPI_LONG;
    }
    else if (type == ARMCI_LONG_LONG) {
        mpi_dt = MPI_LONG_LONG;
    }
    else if (type == ARMCI_FLOAT) {
        mpi_dt = MPI_FLOAT;
    }
    else if (type == ARMCI_DOUBLE) {
        mpi_dt = MPI_DOUBLE;
    }
    else {
        assert(0);
    }

    return mpi_dt;
}


static MPI_Op armci_op_to_mpi_op(char *op)
{
    if (strncmp(op, "+", 1) == 0) {
        return MPI_SUM;
    }
    else if (strncmp(op, "max", 3) == 0) {
        return MPI_MAX;
    }
    else if (strncmp(op, "min", 3) == 0) {
        return MPI_MIN;
    }
    else if (strncmp(op, "*", 1) == 0) {
        return MPI_PROD;
    }
    else if (strncmp(op, "absmin", 6) == 0) {
        return MPI_MIN;
    }
    else if (strncmp(op, "absmax", 6) == 0) {
        return MPI_MAX;
    }
    else if (strncmp(op, "or", 2) == 0) {
        return MPI_BOR;
    }
    else {
        printf("Unsupported gop operation:%s\n",op);
        assert(0);
    }
}


static void do_abs(void *x, int n, int type)
{
#define ARMCI_ABS_INT(a)  (((a) >= 0)   ? (a) : (-(a)))
#define ARMCI_ABS_FLT(a)  (((a) >= 0.0) ? (a) : (-(a)))
#define DO_ABS(ARMCI_TYPE, C_TYPE, WHICH)       \
    if (type == ARMCI_TYPE) {                   \
        int i;                                  \
        C_TYPE *y = (C_TYPE *)x;                \
        for (i = 0; i < n; i++) {               \
            y[i] = ARMCI_ABS_##WHICH(y[i]);     \
        }                                       \
    }                                           \
    else
    DO_ABS(ARMCI_INT,       int,        INT)
    DO_ABS(ARMCI_LONG,      long,       INT)
    DO_ABS(ARMCI_LONG_LONG, long long,  INT)
    DO_ABS(ARMCI_FLOAT,     float,      FLT)
    DO_ABS(ARMCI_DOUBLE,    double,     FLT)
    {
        assert(0);
    }
#undef ARMCI_ABS_INT
#undef ARMCI_ABS_FLT
#undef DO_ABS
}


static void do_gop(void *x, int n, char* op, int type, MPI_Comm comm)
{
    int mpi_type_size = 0;
    MPI_Datatype mpi_type = MPI_DATATYPE_NULL;
    int rc = 0;
    void *result = NULL;
    MPI_Op mpi_op = MPI_OP_NULL;

    mpi_type = armci_type_to_mpi_type(type);
    MPI_Type_size(mpi_type, &mpi_type_size);
    mpi_op = armci_op_to_mpi_op(op);

    if (strncmp(op, "absmin", 6) == 0) {
        do_abs(x, n, type);
    }
    else if (strncmp(op, "absmax", 6) == 0) {
        do_abs(x, n, type);
    }
        
    result = malloc(n*mpi_type_size);
    assert(result);

    rc = MPI_Allreduce(x, result, n, mpi_type, mpi_op, comm); 
    assert(rc == MPI_SUCCESS);

    memcpy(x, result, mpi_type_size * n);
    free(result);
}


static MPI_Comm get_comm(ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(group);
    assert(igroup);
    return igroup->comm;
}


static MPI_Comm get_default_comm()
{
    ARMCI_Group group;
    ARMCI_Group_get_default(&group);
    return get_comm(&group);
}


static int get_default_rank()
{
    int rank;
    MPI_Comm_rank(get_default_comm(), &rank);
    return rank;
}


void armci_msg_bcast(void *buf, int len, int root)
{
    assert(buf != NULL);
    MPI_Bcast(buf, len, MPI_BYTE, root, get_default_comm());
}


/* the payload is a struct with a union e.g.
 *
 * typedef struct {
 *    union val_t {double dval; int ival; long lval; long long llval; float fval;}v;
 *    Integer subscr[MAXDIM];
 *    DoubleComplex extra;
 *    SingleComplex extra2;
 * } elem_info_t;
 *
 * The key piece is the first sizeof(double) bytes. The rest of the struct
 * simply tags along for the communication and can be represented as a byte
 * stream.
 *
 * The 'n' parameter is the size of the entire payload i.e.
 * sizeof(struct elem_info_t).
 *
 * We really care which process has the min/max value and then bcast the
 * entire payload using the min/max answer as the root.
 */
void armci_msg_sel_scope(int scope, void *x, int n, char* op, int type, int contribute)
{
    static int initialized = 0;
    MPI_Op mpi_op = MPI_OP_NULL;

    assert(SCOPE_ALL == scope);

    /* first time this function is called we establish the
     * long long w/ int type that MPI doesn't provide by default */
    if (!initialized) {
        int block[2];
        MPI_Aint disp[2];
        MPI_Datatype type[2];

        initialized = 1;
        type[0] = MPI_LONG_LONG;
        type[1] = MPI_INT;
        disp[0] = 0;
        disp[1] = sizeof(long long);
        block[0] = 1;
        block[1] = 1;
        MPI_Type_struct(2, block, disp, type, &MPI_LONGLONG_INT);
    }

    if (strncmp(op, "min", 3) == 0) {
        mpi_op = MPI_MINLOC;
    }
    else if (strncmp(op, "max", 3) == 0) {
        mpi_op = MPI_MAXLOC;
    }
    else {
        assert(0);
    }

#define SELECT(ARMCI_TYPE, C_TYPE, MPI_TYPE)                                \
    if (type == ARMCI_TYPE) {                                               \
        struct {                                                            \
            C_TYPE val;                                                     \
            int rank;                                                       \
        } in, out;                                                          \
        in.val = *((C_TYPE*)x);                                             \
        in.rank = get_default_rank();                                       \
        MPI_Allreduce(&in, &out, 1, MPI_TYPE, mpi_op, get_default_comm());  \
        armci_msg_bcast(x, n, out.rank);                                    \
    }                                                                       \
    else
    SELECT(ARMCI_INT,       int,        MPI_2INT)
    SELECT(ARMCI_LONG,      long,       MPI_LONG_INT)
    SELECT(ARMCI_LONG_LONG, long long,  MPI_LONGLONG_INT)
    SELECT(ARMCI_FLOAT,     float,      MPI_FLOAT_INT)
    SELECT(ARMCI_DOUBLE,    double,     MPI_DOUBLE_INT)
    {
        assert(0);
    }
#undef SELECT
}


void armci_msg_bcast_scope(int scope, void* buffer, int len, int root)
{
    assert(SCOPE_ALL == scope);
    armci_msg_bcast(buffer, len, root);
}


void armci_msg_brdcst(void* buffer, int len, int root)
{
    armci_msg_bcast(buffer, len, root);
}


void armci_msg_snd(int tag, void* buffer, int len, int to)
{
    //MPI_Send(buffer, len, MPI_CHAR, to, tag, get_default_comm());
    MPI_Send(buffer, len, MPI_CHAR, to, tag, l_state.world_comm);
}


void armci_msg_rcv(int tag, void* buffer, int buflen, int *msglen, int from)
{
    MPI_Status status;
    //MPI_Recv(buffer, buflen, MPI_CHAR, from, tag, get_default_comm(), &status);
    MPI_Recv(buffer, buflen, MPI_CHAR, from, tag, l_state.world_comm, &status);
    if(msglen) {
        MPI_Get_count(&status, MPI_CHAR, msglen);
    }
}


int armci_msg_rcvany(int tag, void* buffer, int buflen, int *msglen)
{
    int ierr;
    MPI_Status status;

    assert(0);
    ierr = MPI_Recv(buffer, buflen, MPI_BYTE, MPI_ANY_SOURCE, tag,
            get_default_comm(), &status);
    if(ierr != MPI_SUCCESS) {
        armci_msg_abort(-1);
    }

    if(msglen) {
        if(MPI_SUCCESS != MPI_Get_count(&status, MPI_CHAR, msglen)) {
            armci_msg_abort(-1);
        }
    }

    return (int)status.MPI_SOURCE;
}


void armci_msg_reduce(void *x, int n, char *op, int type)
{
    do_gop(x, n, op, type, get_default_comm());
}


void armci_msg_reduce_scope(int scope, void *x, int n, char *op, int type)
{
    assert(SCOPE_ALL == scope);
    do_gop(x, n, op, type, get_default_comm());
}


void armci_msg_gop_scope(int scope, void *x, int n, char* op, int type)
{
    do_gop(x, n, op, type, get_default_comm());
}


void armci_msg_igop(int *x, int n, char* op)
{
    do_gop(x, n, op, ARMCI_INT, get_default_comm());
}


void armci_msg_lgop(long *x, int n, char* op)
{
    do_gop(x, n, op, ARMCI_LONG, get_default_comm());
}


void armci_msg_llgop(long long *x, int n, char* op)
{
    do_gop(x, n, op, ARMCI_LONG_LONG, get_default_comm());
}


void armci_msg_fgop(float *x, int n, char* op)
{
    do_gop(x, n, op, ARMCI_FLOAT, get_default_comm());
}


void armci_msg_dgop(double *x, int n, char* op)
{
    do_gop(x, n, op, ARMCI_DOUBLE, get_default_comm());
}


void armci_exchange_address(void *ptr_ar[], int n)
{
    ARMCI_Group group;
    ARMCI_Group_get_default(&group);
    armci_exchange_address_grp(ptr_ar, n, &group);
}


void parmci_msg_barrier()
{
    MPI_Barrier(get_default_comm());
}


void armci_msg_bintree(int scope, int* Root, int *Up, int *Left, int *Right)
{
    assert(SCOPE_ALL == scope);
    assert(0);
}


int armci_msg_me()
{
    int rank;
    if (PARMCI_Initialized()) {
        assert(l_state.world_comm != MPI_COMM_NULL);
        MPI_Comm_rank(l_state.world_comm, &rank);
    }
    else {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    }
    return rank;
}


int armci_msg_nproc()
{
    int size;
    if (PARMCI_Initialized()) {
        assert(l_state.world_comm != MPI_COMM_NULL);
        MPI_Comm_size(l_state.world_comm, &size);
    }
    else {
        MPI_Comm_size(MPI_COMM_WORLD, &size);
    }
    return size;
}


void armci_msg_abort(int code)
{
    if (0 == l_state.rank) {
        fprintf(stderr, "Exiting, Error in Communication\n");
    }

    MPI_Abort(l_state.world_comm, code);
}


void armci_msg_init(int *argc, char ***argv)
{
    int flag;
    MPI_Initialized(&flag);
    if(!flag) {
        MPI_Init(argc, argv);
    }
}


void armci_msg_finalize()
{
    int flag;
    MPI_Initialized(&flag);
    assert(flag);
    MPI_Finalize();
}


double armci_timer()
{
    return MPI_Wtime();
}


void armci_msg_clus_brdcst(void *buf, int len)
{
    assert(0);
}


void armci_msg_clus_igop(int *x, int n, char* op)
{
    assert(0);
}


void armci_msg_clus_fgop(float *x, int n, char* op)
{
    assert(0);
}


void armci_msg_clus_lgop(long *x, int n, char* op)
{
    assert(0);
}


void armci_msg_clus_llgop(long long *x, int n, char* op)
{
    assert(0);
}


void armci_msg_clus_dgop(double *x, int n, char* op)
{
    assert(0);
}


void armci_msg_group_gop_scope(int scope, void *x, int n, char* op, int type, ARMCI_Group *group)
{
    assert(SCOPE_ALL == scope);
    do_gop(x, n, op, type, get_comm(group));
}


void armci_msg_group_igop(int *x, int n, char* op, ARMCI_Group *group)
{
    do_gop(x, n, op, ARMCI_INT, get_comm(group));
}


void armci_msg_group_lgop(long *x, int n, char* op, ARMCI_Group *group)
{
    do_gop(x, n, op, ARMCI_LONG, get_comm(group));
}


void armci_msg_group_llgop(long long *x, int n, char* op, ARMCI_Group *group)
{
    do_gop(x, n, op, ARMCI_LONG_LONG, get_comm(group));
}


void armci_msg_group_fgop(float *x, int n, char* op, ARMCI_Group *group)
{
    do_gop(x, n, op, ARMCI_FLOAT, get_comm(group));
}


void armci_msg_group_dgop(double *x, int n,char* op, ARMCI_Group *group)
{
    do_gop(x, n, op, ARMCI_DOUBLE, get_comm(group));
}


void armci_exchange_address_grp(void *ptr_arr[], int n, ARMCI_Group *group)
{
    assert(0);
#if 0
    MPI_Datatype mpi_datatype;

    if (sizeof(void*) == sizeof(int)) {
        mpi_datatype = MPI_INT;
    }
    else if (sizeof(void*) == sizeof(long)) {
        mpi_datatype = MPI_LONG;
    }
    else if (sizeof(void*) == sizeof(long long)) {
        mpi_datatype = MPI_LONG_LONG;
    }
    else {
        assert(0);
    }

    MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
            ptr_ar, n, mpi_datatype, get_comm(group));
#endif
}


void parmci_msg_group_barrier(ARMCI_Group *group)
{
    MPI_Barrier(get_comm(group));
}


void armci_msg_group_bcast_scope(int scope, void *buf, int len, int root, ARMCI_Group *group)
{
    assert(SCOPE_ALL == scope);
    MPI_Bcast(buf, len, MPI_BYTE, root, get_comm(group));
}


void armci_grp_clus_brdcst(void *buf, int len, int grp_master, int grp_clus_nproc,ARMCI_Group *mastergroup)
{
    assert(0);
}
