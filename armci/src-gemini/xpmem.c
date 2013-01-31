/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set sw=4 ts=8 expandtab : */

#if HAVE_CONFIG_H
#   include "config.h"
#endif

/* C and/or system headers */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <hugetlbfs.h>
#include <errno.h>

#include "armci.h"
#include "armci_impl.h"
#include "groups.h"

#include "pmi.h"
#include "xpmem.h"

/* Info which a PE keeps on other PE's memory regions mapped into its own
   virtual address space via XPMEM. */

typedef struct armci_xpmem_info {
    int           rank;    /* global rank of remote process */
    xpmem_segid_t segid;   /* xpmem handle for data segment of remote PE */
    xpmem_apid_t  apid;    /* access permit ID for that data segment */
    long          length;  /* length of the data segment */
    void          *vaddr;  /* vaddr at which the XPMEM mapping was created */
} armci_xpmem_info_t;

armci_xpmem_info_t *armci_xpmem_info = NULL;

void
armci_setup_xpmem(dmapp_seg_desc_t *all_segs, int comm_rank, int comm_size, ARMCI_Group *group)
{
    int                rc, i;
    int                page_count;
    long               page_size;
    armci_xpmem_info_t xpmem_info;
    struct xpmem_addr  xpmem_addr;
    xpmem_apid_t       apid;
    void               *vaddr = NULL;

    ARMCI_iGroup *igroup = NULL;
    MPI_Comm comm = MPI_COMM_NULL;

    /* Share data segment and symmetric heap with PEs on same node.
       This is a three step process: 1. The source process makes a portion of
       its address space accessible to the other processes. 2. The consumer
       process gets access to the source processes address space as defined by
       the source process. 3. The consumer process attaches the source address
       space range to its own address space. */

    page_size = armci_is_using_huge_pages ? gethugepagesize() ? getpagesize();

    /* 1. Make our memory segment available to other processes. */
    xpmem_info.rank   = ARMCI_Absolute_id(group, comm_rank);
    xpmem_info.length = all_segs[comm_rank].len;
    xpmem_info.segid  = xpmem_make(all_segs[comm_rank].addr,
                                   all_segs[comm_rank].len,
                                   XPMEM_PERMIT_MODE, (void *)0666);
    if (xpmem_info.segid == -1L) {
        armci_die("xpmem_make failed", errno);
    }

    /* Allocate an array to receive all the local SMP xpmem_info */
    armci_xpmem_info = (armci_xpmem_info_t *)calloc(armci_npes_on_smp, sizeof(armci_xpmem_info_t));
    if (armci_xpmem_info == NULL) {
        armci_die("calloc failed ", errno);
    }

    igroup = armci_get_igroup_from_group(&armci_smp_group);
    comm = igroup->comm;
    assert(comm != MPI_COMM_NULL);

    /* Gather all the local process XPMEM export info */
    rc = MPI_Allgather(&xpmem_info, sizeof(xpmem_info), MPI_BYTE,
                       armci_xpmem_info, sizeof(xpmem_info), MPI_BYTE, comm);
    assert(rc == MPI_SUCCESS);

    for (i=0; i<armci_npes_on_smp; i++) {
            /* use the data_segment length from the given rank */
            xpmem_data_length = armci_xpmem_info[i].length;

            /* 2. Get permission to attach memory from other PEs virtual address spaces.
               3. Attach BSS and symmetric heap from other PEs into own address space.
                  Length of each is the same across all PEs. */

            if (armci_xpmem_info[i].rank != armci_me) {
                apid = xpmem_get(armci_xpmem_info[i].segid, XPMEM_RDWR,
                                    XPMEM_PERMIT_MODE, (void *)0666);
                if (apid == -1L) {
                    armci_die("xpmem_get failed ", errno);
                }

                armci_xpmem_info[i].apid  = apid;

                xpmem_addr.apid   = apid;
                xpmem_addr.offset = 0;
                vaddr = xpmem_attach(xpmem_addr, xpmem_data_length, NULL);
                if (vaddr == (void *)-1) {
                    armci_die("xpmem_attach failed ", errno);
                }

                armci_xpmem_info[i].vaddr = vaddr;
            }
    }

    return;
}

void
armci_shutdown_xpmem(void)
{
    int i;

    for (i=0; i<armci_npes_on_smp; i++) {
            if (armci_xpmem_info[i].ds_vaddr) {
                    xpmem_detach(armci_xpmem_info[i].ds_vaddr);
                    xpmem_release(armci_xpmem_info[i].ds_apid);
                    xpmem_remove(armci_xpmem_info[i].ds_segid);
            }
            if (armci_xpmem_info[i].sh_vaddr) {
                    xpmem_detach(armci_xpmem_info[i].sh_vaddr);
                    xpmem_release(armci_xpmem_info[i].sh_apid);
                    xpmem_remove(armci_xpmem_info[i].sh_segid);
            }

            armci_xpmem_info[i].ds_vaddr = NULL;
            armci_xpmem_info[i].sh_vaddr = NULL;
    }

    free(armci_xpmem_info);
    armci_xpmem_info = NULL;
}
