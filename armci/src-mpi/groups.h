/**
 * Private header file for ARMCI groups backed by MPI_comm.
 *
 * The rest of the ARMCI_Group functions are defined in the public armci.h.
 *
 * @author Jeff Daily
 */
#ifndef _ARMCI_GROUPS_H_
#define _ARMCI_GROUPS_H_

#include <mpi.h>

/* dup of MPI_COMM_WORLD for internal MPI communication */
extern MPI_Comm ARMCI_COMM_WORLD;

typedef struct group_link {
    struct group_link *next;
    ARMCI_Group id;
    MPI_Comm comm;
    MPI_Group group;
} ARMCI_iGroup;

extern void armci_group_init();
extern void armci_group_finalize();
extern ARMCI_iGroup* armci_get_igroup_from_group(ARMCI_Group *group);

#endif /* _ARMCI_GROUPS_H_ */
