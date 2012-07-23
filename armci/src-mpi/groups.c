#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDLIB_H
#   include <stdlib.h>
#endif
#if HAVE_ASSERT_H
#   include <assert.h>
#endif

#include "armci.h"
#include "groups.h"
#include "message.h"

/* ARMCI has the notion of a default group and a world group. */
ARMCI_Group ARMCI_Default_Proc_Group = 0;
ARMCI_Group ARMCI_World_Proc_Group = 0;

/* the HEAD of the group linked list */
ARMCI_iGroup *group_list = NULL;

/**
 * Return the ARMCI_iGroup instance given the ARMCI_Group.
 *
 * The group linked list is searched sequentially until the given group
 * is found. It is an error if this function is called before
 * armci_group_init(). An error occurs if the given group is not found.
 */
ARMCI_iGroup* armci_get_igroup_from_group(ARMCI_Group *id)
{
    ARMCI_iGroup *current_group_list_item = group_list;

    assert(group_list != NULL);
    while (current_group_list_item != NULL) {
        if (current_group_list_item->id == *id) {
            return current_group_list_item;
        }
        current_group_list_item = current_group_list_item->next;
    }
    armci_die("ARMCI_Group lookup failed", -1);

    return NULL;
}

/**
 * Creates and associates an ARMCI_Group with an ARMCI_iGroup.
 *
 * This does *not* initialize the members of the ARMCI_iGroup.
 */
static void armci_create_group_and_igroup(
        ARMCI_Group *id, ARMCI_iGroup **igroup)
{
    ARMCI_iGroup *new_group_list_item = NULL;
    ARMCI_iGroup *last_group_list_item = NULL;

    /* find the last group in the group linked list */
    last_group_list_item = group_list;
    while (last_group_list_item->next != NULL) {
        last_group_list_item = last_group_list_item->next;
    }

    /* create, init, and insert the new node for the linked list */
    new_group_list_item = malloc(sizeof(ARMCI_iGroup));
    new_group_list_item->id = last_group_list_item->id + 1;
    new_group_list_item->next = NULL;
    last_group_list_item->next = new_group_list_item;

    /* return the group id and ARMCI_iGroup */
    *igroup = new_group_list_item;
    *id = new_group_list_item->id;
}

/**
 * Returns the rank of this process within the given group.
 */
int ARMCI_Group_rank(ARMCI_Group *id, int *rank)
{
    int status;

    ARMCI_iGroup *igroup = armci_get_igroup_from_group(id);
    status = MPI_Group_rank(igroup->group, rank);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Group_rank: Failed ", status);
    }

    return 0; /* TODO what should this return? an error code? */
}

/**
 * Returns the size of a group.
 */
void ARMCI_Group_size(ARMCI_Group *id, int *size)
{
    int status;

    ARMCI_iGroup *igroup = armci_get_igroup_from_group(id);
    status = MPI_Group_size(igroup->group, size);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Group_size: Failed ", status);
    }
}

/**
 * Translates the given rank from the given group into that of the world group.
 */
int ARMCI_Absolute_id(ARMCI_Group *id, int group_rank)
{
    int world_rank;
    int status;
    MPI_Group world_group;
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(id);

    status = MPI_Comm_group(ARMCI_COMM_WORLD, &world_group);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Comm_group: Failed ", status);
    }
    status = MPI_Group_translate_ranks(
            igroup->group, 1, &group_rank, world_group, &world_rank);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Group_translate_ranks: Failed ", status);
    }

    return world_rank;
}

/**
 * Sets the default ARMCI_Group.
 */
void ARMCI_Group_set_default(ARMCI_Group *id) 
{
    /* sanity check that the group is valid */
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(id);
    assert(NULL != igroup);
    ARMCI_Default_Proc_Group = *id;
}

/**
 * Gets the default ARMCI_Group.
 */
void ARMCI_Group_get_default(ARMCI_Group *group_out)
{
    *group_out = ARMCI_Default_Proc_Group;
}

/**
 * Gets the world ARMCI_Group.
 */
void ARMCI_Group_get_world(ARMCI_Group *group_out)
{
    *group_out = ARMCI_World_Proc_Group;
}

/**
 * Destroys the given ARMCI_iGroup.
 */
static void armci_igroup_finalize(ARMCI_iGroup *igroup)
{
    int status;

    assert(igroup);

    status = MPI_Group_free(&igroup->group);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Group_free: Failed ", status);
    }
    
    if (igroup->comm != MPI_COMM_NULL) {
      status = MPI_Comm_free(&igroup->comm);
      if (status != MPI_SUCCESS) {
          armci_die("MPI_Comm_free: Failed ", status);
      }
    }
}

/**
 * Removes and destroys the given ARMCI_Group from the group linked list.
 */
void ARMCI_Group_free(ARMCI_Group *id)
{
    ARMCI_iGroup *current_group_list_item = group_list;
    ARMCI_iGroup *previous_group_list_item = NULL;

    /* find the group to free */
    while (current_group_list_item != NULL) {
        if (current_group_list_item->id == *id) {
            break;
        }
        previous_group_list_item = current_group_list_item;
        current_group_list_item = current_group_list_item->next;
    }
    /* make sure we found a group */
    assert(current_group_list_item != NULL);
    /* remove the group from the linked list */
    if (previous_group_list_item != NULL) {
        previous_group_list_item->next = current_group_list_item->next;
    }
    /* free the group */
    armci_igroup_finalize(current_group_list_item);
    free(current_group_list_item);
}

/**
 * Create a child group for to the given group.
 *
 * @param[in] n #procs in this group (<= that in group_parent)
 * @param[in] pid_list The list of proc ids (w.r.t. group_parent)
 * @param[out] id_child Handle to store the created group
 * @param[in] id_parent Parent group 
 */
void ARMCI_Group_create_child(
        int n, int *pid_list, ARMCI_Group *id_child, ARMCI_Group *id_parent)
{
    int status;
    int grp_me;
    ARMCI_iGroup *igroup_child = NULL;
    MPI_Group    *group_child = NULL;
    MPI_Comm     *comm_child = NULL;
    ARMCI_iGroup *igroup_parent = NULL;
    MPI_Group    *group_parent = NULL;
    MPI_Comm     *comm_parent = NULL;

    /* create the node in the linked list of groups and */
    /* get the child's MPI_Group and MPI_Comm, to be populated shortly */
    armci_create_group_and_igroup(id_child, &igroup_child);
    group_child = &(igroup_child->group);
    comm_child  = &(igroup_child->comm);

    /* get the parent's MPI_Group and MPI_Comm */
    igroup_parent = armci_get_igroup_from_group(id_parent);
    group_parent = &(igroup_parent->group);
    comm_parent  = &(igroup_parent->comm);

    status = MPI_Group_incl(*group_parent, n, pid_list, group_child);
    if (status != MPI_SUCCESS) {
        armci_die("MPI_Group_incl: Failed ", status);
    }

    {
        MPI_Comm comm, comm1, comm2;
        int lvl=1, local_ldr_pos;
        MPI_Group_rank(*group_child, &grp_me);
        if (grp_me == MPI_UNDEFINED) {
            *comm_child = MPI_COMM_NULL;
            /* FIXME: keeping the group around for now */
            return;
        }
        /* SK: sanity check for the following bitwise operations */
        assert(grp_me>=0);
        MPI_Comm_dup(MPI_COMM_SELF, &comm); /* FIXME: can be optimized away */
        local_ldr_pos = grp_me;
        while(n>lvl) {
            int tag=0;
            int remote_ldr_pos = local_ldr_pos^lvl;
            if (remote_ldr_pos < n) {
                int remote_leader = pid_list[remote_ldr_pos];
                MPI_Comm peer_comm = *comm_parent;
                int high = (local_ldr_pos<remote_ldr_pos)?0:1;
                MPI_Intercomm_create(
                        comm, 0, peer_comm, remote_leader, tag, &comm1);
                MPI_Comm_free(&comm);
                MPI_Intercomm_merge(comm1, high, &comm2);
                MPI_Comm_free(&comm1);
                comm = comm2;
            }
            local_ldr_pos &= ((~0)^lvl);
            lvl<<=1;
        }
        *comm_child = comm;
        /* cleanup temporary group (from MPI_Group_incl above) */
        MPI_Group_free(group_child);
        /* get the actual group associated with comm */
        MPI_Comm_group(*comm_child, group_child);
    }
}

/**
 * Convenience for creating child group from default group.
 */
void ARMCI_Group_create(int n, int *pid_list, ARMCI_Group *group_out)
{
    ARMCI_Group_create_child(n, pid_list, group_out, &ARMCI_Default_Proc_Group);
}

/**
 * Initialize group linked list. Prepopulate with world group.
 */
void armci_group_init() 
{
    int grp_me;

    /* Initially, World group is the default group */
    ARMCI_World_Proc_Group = 0;
    ARMCI_Default_Proc_Group = 0;

    /* create the head of the group linked list */
    assert(group_list == NULL);
    group_list = malloc(sizeof(ARMCI_iGroup));
    group_list->id = ARMCI_World_Proc_Group;
    group_list->next = NULL;

    /* save MPI world group and communicatior in ARMCI_World_Proc_Group */
    group_list->comm = ARMCI_COMM_WORLD;
    MPI_Comm_group(ARMCI_COMM_WORLD, &(group_list->group));
}

void armci_group_finalize()
{
    ARMCI_iGroup *current_group_list_item = group_list;
    ARMCI_iGroup *previous_group_list_item = NULL;

    /* don't free the world group (the list head) */
    current_group_list_item = current_group_list_item->next;

    while (current_group_list_item != NULL) {
        previous_group_list_item = current_group_list_item;
        current_group_list_item = current_group_list_item->next;
        armci_igroup_finalize(previous_group_list_item);
        free(previous_group_list_item);
    }

    /* ok, now free the world group */
    MPI_Group_free(&(group_list->group));
    free(group_list);
    group_list = NULL;
}
