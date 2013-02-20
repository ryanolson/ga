#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <mpi.h>

extern void exit(int status);

#include "tcgmsgP.h"
#include "srftoc.h"

char     tcgmsg_err_string[ERR_STR_LEN];
int      _tcg_initialized=0;
Integer  DEBUG_;
int      SR_parallel; 
int      SR_single_cluster =1;
int      tcgi_argc=0;
char   **tcgi_argv=NULL;

static int SR_initialized=0;


Integer TCGREADY_()
{
    return (Integer)SR_initialized;
}


/**
 * number of processes
 */
Integer NNODES_()
{
    int numprocs;

    numprocs = armci_msg_nproc();
    return((Integer)numprocs);
}


/**
 * Get calling process id
 */
Integer NODEID_()
{
    int myid;

    myid = armci_msg_me();
    return((Integer)myid);
}


void Error(char *string, Integer code)
{
    fprintf(stdout, FMT_INT ": %s " FMT_INT " (%#lx).\n",
            NODEID_(), string, code, (long unsigned int)code);
    fflush(stdout);
    fprintf(stderr, FMT_INT ": %s " FMT_INT " (%#lx).\n",
            NODEID_(), string, code, (long unsigned int)code);

    finalize_nxtval(); /* clean nxtval resources */
    armci_msg_abort(code);
}


/**
 * Alternative initialization for C programs
 * used to address argv/argc manipulation in MPI
 */
void tcgi_alt_pbegin(int *argc, char **argv[])
{
    int numprocs, myid;
    int init=0;

    if(SR_initialized) {
        Error("TCGMSG initialized already???",-1);
    } else {
        SR_initialized=1;
    }

    armci_msg_init(argc, argv);
    ARMCI_Init_args(argc, argv);

    numprocs = armci_msg_nproc();
    myid = armci_msg_me();
    SR_parallel = numprocs > 1 ? 1 : 0;

    /* printf("%d:ready to go\n",NODEID_()); */
    /* wait until the last possible moment to call install_nxtval
     * it could be called by ARMCI_Init
     * or is called the first time nxtval is invoked (yuck) */
    /*install_nxtval(argc, argv);*/
}


/**
 * Initialization for C programs
 */
void tcgi_pbegin(int argc, char* argv[])
{
    tcgi_argc = argc;
    tcgi_argv = argv;
    tcgi_alt_pbegin(&argc, &argv);
}


/**
 * shut down message-passing library
 */ 
void FATR PEND_()
{
    finalize_nxtval();
    armci_msg_finalize();
    exit(0);
}


double FATR TCGTIME_()
{
    static int first_call = 1;
    static double first_time, last_time, cur_time;
    double diff;

    if (first_call) {
        /* first_time = armci_timer(); */
        first_time = MPI_Wtime();
        first_call = 0;
        last_time  = -1e-9; 
    }

    /* cur_time = armci_timer(); */
    cur_time = MPI_Wtime();
    diff = cur_time - first_time;

    /* address crappy armci_timer: consectutive calls must be at least 1ns apart  */
    if(diff - last_time < 1e-9) {
        diff +=1e-9;
    }
    last_time = diff;

    return diff;                  /* Add logic here for clock wrap */
}


Integer MTIME_()
{
    return (Integer) (TCGTIME_()*100.0); /* time in centiseconds */
}



/**
 * Integererface from Fortran to C error routine
 */
void PARERR_(Integer *code)
{
    Error("User detected error in FORTRAN", *code);
}


void SETDBG_(Integer *onoff)
{
    DEBUG_ = *onoff;
}

void FATR STATS_()
{
    printf("STATS not implemented\n");
} 
