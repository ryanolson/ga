#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <assert.h>

#include "message.h"

#include "tcgmsgP.h"

/* size of internal buffer for global ops */
#define DGOP_BUF_SIZE 65536 
#define IGOP_BUF_SIZE (sizeof(DoublePrecision)/sizeof(Integer))*DGOP_BUF_SIZE 

static DoublePrecision gop_work[DGOP_BUF_SIZE]; /**< global ops buffer */

/**
 * global operations -- integer version 
 */
void IGOP_(Integer *ptype, Integer *x, Integer *pn, char *op, Integer oplen)
{
    Integer *work  = (Integer *) gop_work;
    Integer nleft  = *pn;
    Integer buflen = TCG_MIN(nleft,IGOP_BUF_SIZE); /**< Try to get even sized buffers */
    Integer nbuf   = (nleft-1) / buflen + 1;
    Integer n;

    buflen = (nleft-1) / nbuf + 1;

    while (nleft) {
        int ndo = TCG_MIN(nleft, buflen);

        if (sizeof(Integer) == sizeof(int)) {
            armci_msg_reduce(x, ndo, op, ARMCI_INT);
        }
        else if (sizeof(Integer) == sizeof(long)) {
            armci_msg_reduce(x, ndo, op, ARMCI_LONG);
        }
        else {
            assert(0);
        }

        nleft -= ndo; x+= ndo;
    }
}



/**
 * global operations -- DoublePrecision version 
 */
void DGOP_(Integer *ptype, DoublePrecision *x, Integer *pn, char *op, Integer oplen)
{
    DoublePrecision *work=  gop_work;
    Integer nleft  = *pn;
    Integer buflen = TCG_MIN(nleft,DGOP_BUF_SIZE); /**< Try to get even sized buffers */
    Integer nbuf   = (nleft-1) / buflen + 1;
    Integer n;

    buflen = (nleft-1) / nbuf + 1;

    while (nleft) {
        int ndo = TCG_MIN(nleft, buflen);

        armci_msg_reduce(x, ndo, op, ARMCI_DOUBLE);

        nleft -= ndo; x+= ndo;
    }
}


/**
 * Synchronize processes
 */
void SYNCH_(Integer *type)
{
    ARMCI_Barrier();
}



/**
 * broadcast buffer to all other processes from process originator
 */
void BRDCST_(Integer *type, char *buf, Integer *lenbuf, Integer *originator)
{
    int count = (int)*lenbuf, root = (int)*originator;

    armci_msg_brdcst(buf, count, root);
}
