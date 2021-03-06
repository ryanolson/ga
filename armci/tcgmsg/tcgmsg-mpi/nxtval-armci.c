#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <mpi.h>

#if HAVE_MALLOC_H
#   include <malloc.h>
#endif

#include "armci.h"
#include "tcgmsgP.h"

#define LEN 2
static Integer pnxtval_counter_val;
static Integer *pnxtval_counter=&pnxtval_counter_val;
static int nxtval_installed=0;
extern int     *tcgi_argc;
extern char  ***tcgi_argv;
#define INCR 1   /**< increment for NXTVAL */
#define BUSY -1L /**< indicates somebody else updating counter*/
#define NXTV_SERVER ((int)NNODES_() -1)


/**
 *  Get next value of shared counter.
 *  mproc > 0 ... returns requested value
 *  mproc < 0 ... server blocks until abs(mproc) processes are queued
 *                and returns junk
 *  mproc = 0 ... indicates to server that I am about to terminate
 */
Integer NXTVAL_(Integer *mproc)
{
    Integer local;
    int server = NXTV_SERVER;         /* id of server process */

    install_nxtval(tcgi_argc, tcgi_argv);

    if (SR_parallel) {
        if (DEBUG_) {
            (void) printf(FMT_INT ": nxtval: mproc=" FMT_INT "\n",
                          NODEID_(), *mproc);
            (void) fflush(stdout);
        }

        if (*mproc < 0) {
            ARMCI_Barrier();

            /* reset the counter value to zero */
            if( NODEID_() == server) {
                *pnxtval_counter = 0;
            }

            ARMCI_Barrier();
        }
        if (*mproc > 0) {
#if   SIZEOF_F77_INTEGER == SIZEOF_INT
            int op = ARMCI_FETCH_AND_ADD;
#elif SIZEOF_F77_INTEGER == SIZEOF_LONG
            int op = ARMCI_FETCH_AND_ADD_LONG;
#else
#   error
#endif
            ARMCI_Rmw(op,(void*)&local,(void*)pnxtval_counter,1,server);
        }
    } else {
        /* Not running in parallel ... just do a simulation */
        static int count = 0;
        if (*mproc == 1) {
            local = count++;
        } else if (*mproc == -1) {
            count = 0;
            local = 0;
        } else {
            Error("nxtval: sequential version with silly mproc ", (Integer) *mproc);
        }
    }

    return local;
}


/**
 * initialization for nxtval
 */
void install_nxtval(int *argc, char **argv[])
{
    int rc;
    int me = (int)NODEID_(), bytes, server;
    void **ptr_ar;

    if (nxtval_installed) {
        return;
    }
    nxtval_installed = 1;

    if (!ARMCI_Initialized()) {
        ARMCI_Init_args(argc, argv);
    }

    ptr_ar = (void **)malloc(sizeof(void *)*(int)NNODES_());
    if(!ptr_ar) {
        Error("malloc failed in install_nxtval", (Integer)NNODES_());  
    }

    server = NXTV_SERVER;

    if(me== server) {
        bytes = sizeof(Integer);
    } else {
        bytes =0;
    }

    rc = ARMCI_Malloc(ptr_ar,bytes);
    if(rc) {
        Error("nxtv: armci_malloc failed",rc);
    }

    pnxtval_counter = (Integer*) ptr_ar[server];

    if(me==server) {
        *pnxtval_counter = (Integer)0;
    }

    free(ptr_ar);
    ARMCI_Barrier();
}


void finalize_nxtval()
{
/*
 * Cannot call ARMCI functions here as ARMCI might have been terminated
 * by now. NOTE: finalize_nxtval is called in pend(), which is called after
 * GA_Terminate/ARMCI_Finalize.
 */    
#if 0
    if(NODEID_() == NXTV_SERVER)ARMCI_Free(pnxtval_counter);
#endif
    ARMCI_Finalize();
}
