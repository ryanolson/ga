/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set sw=4 ts=8 expandtab : */

#if HAVE_CONFIG_H
#   include "config.h"
#endif

/****************************************************************************** 
* file:    clusterinfo.c
* purpose: Determine cluster info i.e., number of machines and processes
*          running on each of them.
*
*******************************************************************************/

/* C and/or system headers */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <hugetlbfs.h>

#include "armci.h"
#include "armci_impl.h"

#include "pmi.h"


#define SAMECLUSNODE(P) (armci_pe_node[(P)] == armci_clus_me)

int           armci_npes_on_smp;           /* number of local SMP ranks  */
int          *armci_pes_on_smp;            /* array of local SMP ranks   */
int           armci_smp_index = -1;        /* our index within local SMP */
int           armci_rank_order = 1;        /* Rank reorder method: smp (block cyclic) default */

int          *armci_pe_node;               /* array to convert from PE to logical node idx */
int          *armci_node_leader;           /* array to convert from node idx to leader PE rank */
int           armci_npes_per_node;         /* maximum pes per node of the job */

int           armci_clus_me = -1;          /* my node index */
int           armci_nclus = -1;            /* number of nodes that make up job */

ARMCI_Group   armci_smp_group = -1;        /* ARMCI group for local SMP ranks */

void
armci_allgather_ordered(void *in, void *out, int len)
{
        int i, rc;
        char *tmp_buf, *out_ptr;
        int job_size = 0;
        static int *armci_ivec_ptr = NULL;

        rc = PMI_Get_size(&job_size);
        assert(rc == PMI_SUCCESS);

        /* This array needs to be generated during init */
        if (armci_ivec_ptr == NULL) {
            int my_rank = -1;

            rc = PMI_Get_rank(&my_rank);
            assert(rc == PMI_SUCCESS);

            armci_ivec_ptr = (int *)malloc(sizeof(int) * job_size);
            assert(armci_ivec_ptr != NULL);

            rc = PMI_Allgather(&my_rank, armci_ivec_ptr, sizeof(int));
            assert(rc == PMI_SUCCESS);
        }

        tmp_buf = (char *)malloc(job_size * len);
        assert(tmp_buf);

        rc = PMI_Allgather(in,tmp_buf,len);
        assert(rc == PMI_SUCCESS);

        out_ptr = out;

        for(i=0;i<job_size;i++) {
            memcpy(&out_ptr[len * armci_ivec_ptr[i]], &tmp_buf[i * len], len);
        }

        free(tmp_buf);
}

/* armci_get_jobparams gets job parameters such as rank, size from PMI.
 * returns 0 upon success, -1 otherwise.
 */
static int
armci_get_jobparams(int *pe_p,
                    int *npes_p,
                    int *npes_on_smp_p,
                    int **pes_on_smp_p,
                    int *lindex_p)
{
        int i, rc = PMI_SUCCESS;
        int pe,appnum,npes,npes_on_smp,*pe_list,lindex;

        /* We still call the functions below even if we used PMI2_init just
           to keep things simple. */

        rc = PMI_Get_rank(&pe);     /* TODO get rank in app, when fixed */
        if (rc != PMI_SUCCESS) {
                fprintf(stderr, " ERROR: PMI_Get_rank failed w/ %d.\n", rc);
                goto error;
        }
        rc = PMI_Get_appnum(&appnum);
        if (rc != PMI_SUCCESS) {
                fprintf(stderr, " ERROR: PMI_Get_appnum failed w/ %d.\n", rc);
                goto error;
        }
        rc = PMI_Get_app_size(appnum, &npes);
        if (rc != PMI_SUCCESS) {
                fprintf(stderr, " ERROR: PMI_Get_app_size failed w/ %d.\n", rc);
                goto error;
        }
        rc = PMI_Get_numpes_on_smp(&npes_on_smp);
        if (rc != PMI_SUCCESS) {
                fprintf(stderr, " ERROR: PMI_Get_numpes_on_smp failed w/ %d.\n", rc);
                goto error;
        }

        pe_list = (int *)malloc(sizeof(int) * npes_on_smp);
        if (pe_list == NULL) {
                fprintf(stderr, " ERROR: malloc failed w/ %d.\n", rc);
                goto error;
        }

        rc = PMI_Get_pes_on_smp(pe_list,npes_on_smp);
        if (rc != PMI_SUCCESS) {
                fprintf(stderr, " ERROR: PMI_Get_pes_on_smp failed w/ %d.\n", rc);
                free(pe_list);
                goto error;
        }

        for (lindex=-1,i=0; i<npes_on_smp; i++) {
                if (pe_list[i] == pe) {
                        lindex = i;
                        break;
                }
        }
        assert(lindex!=-1);

        *pe_p = pe;
        *npes_p = npes;
        *npes_on_smp_p = npes_on_smp;
        *pes_on_smp_p = pe_list;
        *lindex_p = lindex;

        return 0;

error:
        return -1;
}


/* Compare fn for qsort() below */
static int
compare_ints(const void* px, const void* py)
{
    int x;
    int y;

    x = *((const int*)px);
    y = *((const int*)py);

    return x - y;
}

/* armci_get_jobptopo generates job topological using info from PMI.
 *
 * Allocates the armci_pe_node[] array that converts PE ranks to logical node indices
 * Allocates the armci_node_leader[] array that converts node idx to PE leader rank
 *
 * returns 0 upon success, -1 otherwise.
 */

static int
armci_get_jobtopo(int npes, int *pe_list, int *nodesp)
{
    int *leaders;
    int i, last_leader, num_leaders, num_nodes = 0;
    int max_npes_per_node, npes_per_node;

    char *value;

    /* Parse the environment to see if RANK_REORDER is in action */
    if ((value = getenv("MPICH_RANK_REORDER_METHOD")) != NULL) {
        int val;
        if ((val = atoi(value)) >= 0)
            armci_rank_order = val;
    }

    /* Allocate an array to hold the PE leader ranks */
    armci_pe_node = malloc(npes*sizeof(int));
    if (armci_pe_node == NULL)
        goto error_free;

    /* Now build an array with one entry per PE giving the leader rank
     * for their SMP node.
     */
    armci_allgather_ordered(&pe_list[0], armci_pe_node, sizeof(int));

    /*
     * Calculate the number of nodes in use
     * and build arrays to convert from PE to node idx and from node idx to leader
     */


    /*
     * Cope with non block job PE layouts by creating and then
     * sorting a temporary copy of the leaders array so we can
     * count the number of unique node leaders
     */
    leaders = malloc(npes*sizeof(int));
    if (leaders == NULL)
        goto error_free;

    memcpy(leaders, armci_pe_node, npes*sizeof(int));

    qsort(leaders, npes, sizeof(int), compare_ints);

    /* Count the number of leaders == number of nodes */
    for (num_leaders = 0, last_leader = -1, npes_per_node = 1, max_npes_per_node = 1, i = 0; i < npes; i++) {
        if (leaders[i] != last_leader) {
            num_leaders++;
            last_leader = leaders[i];
            npes_per_node = 1;
        }
        else {
            /* Also calculate the maximum number of pes per node */
            if (++npes_per_node > max_npes_per_node)
                max_npes_per_node = npes_per_node;
        }
    }
    assert(num_leaders);

    /* Save max to help keep things symmetric */
    armci_npes_per_node = max_npes_per_node;

    /* Allocate an array to hold each logical node leader PE rank */
    armci_node_leader = malloc(num_leaders*sizeof(int));
    if (armci_node_leader == NULL)
        goto error_free2;

    /* Fill out the logical node to leader array */
    for (num_nodes = 0, last_leader = -1, i = 0; i < npes; i++) {
        if (leaders[i] != last_leader)
            armci_node_leader[num_nodes++] = last_leader = leaders[i];
    }
    assert(num_nodes == num_leaders);

    free(leaders);

    /* Now re-write the armci_pe_node[] array to convert from PE to logical node idx */
    for (i = 0; i < npes; i++) {
        int leader = armci_pe_node[i];
        int j;

        /* Find idx of leader in armci_pe_node[] array */
        for (j = 0; j < num_nodes; j++)
            if (armci_node_leader[j] == leader) {
                /* Re-write PE table entry */
                armci_pe_node[i] = j;
                break;
            }
        assert(j < num_nodes);
    }

    *nodesp = num_nodes;

    return 0;

error_free2:
    free(leaders);

error_free:
    free(armci_pe_node);
    armci_pe_node = NULL;

    return -1;
}

void armci_init_clusinfo(void)
{
    int rc;
    int pe, npes, npes_on_smp, lindex, nodes;
    int *pe_list;

    /* Get job parameters such as rank, size etc. */
    rc = armci_get_jobparams(&pe, &npes, &npes_on_smp, &pe_list, &lindex);
    if (rc != 0) {
        armci_die("armci_get_jobparams failed", rc);
    }

    /* Determine the job topology; such as mapping local ranks to each node */
    rc = armci_get_jobtopo(npes, pe_list, &nodes);
    if (rc != 0) {
        armci_die("armci_get_jobtopo failed", rc);
    }

    armci_clus_me     = armci_pe_node[pe];
    armci_nclus       = nodes;
    armci_pes_on_smp  = pe_list;
    armci_npes_on_smp = npes_on_smp;
    armci_smp_index   = lindex;

    ARMCI_Group_create(armci_npes_on_smp, armci_pes_on_smp, &armci_smp_group);

    /* Flag that on node SMP optimisations are enabled */
    armci_uses_shm = 1;

    return;
}

/* Locality functions */

/*\ find cluster node id the specified process is on
\*/
int armci_clus_id(int p)
{
    assert(p >= 0 && p < armci_nproc);
    assert(armci_pe_node);

    return armci_pe_node[p];
}

/* return number of nodes in given domain */
int armci_domain_count(armci_domain_t domain)
{
    assert(armci_nclus >= 0);
    return armci_nclus;
}

/* return ID of domain that the calling process belongs to
 */
int armci_domain_my_id(armci_domain_t domain)
{
    assert(armci_clus_me >= 0);
    return armci_clus_me;
}

/* Check whether the process is on the same node */
int armci_domain_same_id(armci_domain_t domain, int proc)
{
    assert(armci_clus_me >= 0);

    return SAMECLUSNODE(proc);
}


