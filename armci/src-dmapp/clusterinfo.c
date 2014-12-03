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

int           armci_npes_on_smp;           /* number of local SMP ranks  */
int          *armci_pes_on_smp;            /* array of local SMP ranks   */
int           armci_smp_index = -1;        /* our index within local SMP */
int           armci_rank_order = 1;        /* Rank reorder method: smp (block cyclic) default */

int           armci_npes_per_node;         /* maximum pes per node of the job */

int           armci_clus_me = -1;          /* my node index */
int           armci_nclus = -1;            /* number of nodes that make up job */

ARMCI_Group   armci_smp_group = -1;        /* ARMCI group for local SMP ranks */

/* Optimised memcpy implementation */
// extern void *_cray_armci_memcpy_snb(void *dest, const void *src, size_t n);
// extern void *_cray_armci_memcpy_amd(void *dest, const void *src, size_t n);
void *(*_cray_armci_memcpy)(void *dest, const void *src, size_t n) = NULL;
int armci_use_system_memcpy = 1;

void
armci_init_memcpy(void)
{
    unsigned int family, model, stepping;
    unsigned int genuine_intel = 0;
    unsigned int authentic_amd = 0;
    unsigned int ncpus = 0;
    FILE *cpuinfo;
    char buf[80];
    char vendor_id[12]; /* vendor id is always 12 characters */

    if (armci_use_system_memcpy) {
        _cray_armci_memcpy = NULL;
        return;
    }

    family = model = stepping = 0;

    cpuinfo = fopen("/proc/cpuinfo","r");

    if (cpuinfo) {
        while (fgets(buf, sizeof(buf), cpuinfo) != NULL) {
            if (!strncmp(buf, "vendor_id", 9)) {
                ncpus++;
                if (ncpus > 1) {
                    /* No need to process info for more than 1 CPU */
		    fclose(cpuinfo);
                    break;
                }

                sscanf(buf, "vendor_id\t: %s", vendor_id);

                if (!strncmp(vendor_id, "GenuineIntel", 12)) {
                    genuine_intel = 1;
                } else if (!strncmp(vendor_id, "AuthenticAMD", 12)) {
                    authentic_amd = 1;
                } else {
                    /* Unknown vendor. */
                    fclose(cpuinfo);
                    break;
                }
                continue;
            }

            if (!strncmp(buf, "cpu family", 10)) {
                sscanf(buf, "cpu family\t: %u", &family);
                continue;
            }

            if (!strncmp(buf, "model\t\t", 7)) {
                sscanf(buf, "model\t\t: %u", &model);
                continue;
            }

            if (!strncmp(buf, "stepping", 8)) {
                sscanf(buf, "stepping\t: %u", &stepping);
                continue;
            }
        }
    } else {
        /* Couldn't open /proc/cpuinfo.  This shouldn't happen. */
        armci_use_system_memcpy = 1;
        _cray_armci_memcpy = NULL;
    }

    
    if (genuine_intel && (family == 6 && model >= 0x2d)) {
        /* Intel Sandy Bridge (0x2d) & Ivy Bridge (0x3e) */
        //_cray_armci_memcpy = _cray_armci_memcpy_snb;
        //armci_use_system_memcpy = 0;
    } else if (authentic_amd && family >= 21) {
    	/* AMD Interlagos */
        //_cray_armci_memcpy = _cray_armci_memcpy_amd;
        //armci_use_system_memcpy = 0;
    } else {
        /* Unknown vendor/family/model id? */
        armci_use_system_memcpy = 1;
        _cray_armci_memcpy = NULL;
    }
}

/* armci_get_jobparams gets job parameters such as rank, size from PMI.
 * returns 0 upon success, -1 otherwise.
 */
static int
armci_get_jobparams(int *pe_p,
                    int *npes_p,
                    int *npes_on_smp_p,
                    int **pes_on_smp_p)
{
        int i, rc = PMI_SUCCESS;
        int pe,appnum,npes,npes_on_smp,*pe_list,lindex;

        /* We still call the functions below even if we used PMI2_init just
           to keep things simple. */

        rc = PMI_Get_rank(&pe);
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

        *pe_p = pe;
        *npes_p = npes;
        *npes_on_smp_p = npes_on_smp;
        *pes_on_smp_p = pe_list;

        return 0;

error:
        return -1;
}


/* armci_get_jobptopo generates job topological using info from PMI.
 * Returns the maximum number of ranks per node and the number of nodes
 */

static int
armci_get_jobtopo(int npes, int npes_on_smp, int *pe_list, int *maxp, int *nodesp)
{
    int i, rc, num_nodes;
    int max_npes_per_node, *npes_per_node;

    /* Allocate an npes array for the PMI_Allgather */
    npes_per_node = malloc(npes*sizeof(int));

    /* Gather the npes_on_smp from every rank */
    rc = PMI_Allgather(&npes_on_smp, npes_per_node, sizeof(int));
    if (rc != PMI_SUCCESS) {
        armci_die("PMI_Allgather failed ", rc);
    }

    /* Determine the maximum number of pes per node
     * This will be the same number in all but the last SMP node which may only
     * be partially filled
     */
    max_npes_per_node = 0;
    for (i = 0; i < npes; i++)
        if (npes_per_node[i] > max_npes_per_node)
            max_npes_per_node = npes_per_node[i];

    free(npes_per_node);

    /* This should be a simple calculation (block and cyclic layouts) */
    num_nodes = npes/max_npes_per_node;

    assert(max_npes_per_node >= npes_on_smp);
    assert(num_nodes > 0);

    /* Return values to caller */
    *maxp = max_npes_per_node;
    *nodesp = num_nodes;

    return 0;
}

void armci_init_clusinfo(void)
{
    int rc;
    int pe, npes, npes_on_smp, max_npes_on_smp, lindex, nodes;
    int *pe_list;

    char *value;

    /* Parse the environment to see if RANK_REORDER is in action */
    if ((value = getenv("MPICH_RANK_REORDER_METHOD")) != NULL) {
        int val;
        if ((val = atoi(value)) >= 0)
            armci_rank_order = val;
    }

    /* SHM optimisations are only supported for block(1) and cyclic(0) rank allocations */
    if (armci_rank_order >= 2) {
        /* Disable XPMEM use */
        armci_uses_shm = 0;
        return;
    }

    /* Get job parameters such as rank, size etc. */
    rc = armci_get_jobparams(&pe, &npes, &npes_on_smp, &pe_list);
    if (rc != 0) {
        armci_die("armci_get_jobparams failed", rc);
    }

    /* Determine the job topology; such as mapping local ranks to each node */
    rc = armci_get_jobtopo(npes, npes_on_smp, pe_list, &max_npes_on_smp, &nodes);
    if (rc != 0) {
        armci_die("armci_get_jobtopo failed", rc);
    }

    armci_nclus       = nodes;
    armci_npes_per_node = max_npes_on_smp;
    armci_pes_on_smp  = pe_list;
    armci_npes_on_smp = npes_on_smp;
    armci_clus_me     = ARMCI_RANK2NODE(armci_me);
    armci_smp_index   = ARMCI_RANK2LINDEX(armci_me);

    /* Currently not used */
    ARMCI_Group_create(armci_npes_on_smp, armci_pes_on_smp, &armci_smp_group);

    armci_init_memcpy();

    return;
}

/* Locality functions */

/*
  int armci_domain_id(armci_domain_t domain, int glob_proc_id)

  PURPOSE: return number of processes/tasks in locality domain represented by id.
  ARGUMENTS:
  	domain - domain name
  	id     - identifier of a node within the locality domain, value < 0 means my node
  RETURN VALUE:
   	< 0         - error
   	other value - number of processes/tasks (0, ..., armci_domain_count(domain)-1)
*/
int armci_domain_nprocs(armci_domain_t domain, int id)
{
    /* TODO: This may not be correct for the last node in job */
    return armci_npes_on_smp;
}

/*
  int armci_domain_count(armci_domain_t domain

  PURPOSE: return number of nodes in specified locality domain.
  ARGUMENTS:
	domain - domain name
  RETURN VALUE:
 	<  0        - error
	other value - number of nodes
*/
int armci_domain_count(armci_domain_t domain)
{
    assert(armci_nclus >= 0);
    return armci_nclus;
}

/*
  int armci_domain_id(armci_domain_t domain, int glob_proc_id)

  PURPOSE: return local process id within a domain based on its global process/task id
  ARGUMENTS:
	domain       - domain name
	glob_proc_id - global process/task id
  RETURN VALUE
 	< 0         - error
 	other value - local process/task id
*/
int armci_domain_id(armci_domain_t domain, int glob_proc_id)
{
    assert(armci_nclus >= 0);
    if (glob_proc_id < 0 || glob_proc_id >= armci_nproc)
        return -1;

    return ARMCI_RANK2LINDEX(glob_proc_id);
}

/*
  int armci_domain_glob_proc_id(armci_domain_t domain, int id, int loc_proc_id)

  PURPOSE: Returns global process/task id based on its id in a given locality domain node
  ARGUMENTS:
	domain      - domain name
	id          - identifier of a node within the locality domain, value < 0 means my node
        loc_proc_id - local id within domain
  RETURN VALUE:
	< 0 - error
	other value - process/task id
*/
int armci_domain_glob_proc_id(armci_domain_t domain, int id, int loc_proc_id)
{
    assert(armci_clus_me >= 0);
    if (id >= armci_nclus || loc_proc_id < 0 || loc_proc_id >= armci_npes_per_node)
        return -1;

    if (id < 0)
        id = armci_clus_me;

    return ARMCI_RANK(id, loc_proc_id);
}

/*
  int armci_domain_my_id(armci_domain_t domain)

  PURPOSE: Returns id node in specified domain the calling process/task belongs to
  ARGUMENTS:
	domain - domain name
  RETURN VALUE: id of domain
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
    return ARMCI_SAMECLUSNODE(proc);
}

/*\ find cluster node id the specified process is on
\*/
int armci_clus_id(int p)
{
    assert(p >= 0 && p < armci_nproc);

    return ARMCI_RANK2NODE(p);
}


