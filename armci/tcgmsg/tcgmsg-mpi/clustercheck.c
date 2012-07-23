#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include "message.h"

#if HAVE_STRING_H
#   include <string.h>
#endif
#if HAVE_UNISTD_H
#   include <unistd.h>
#endif

#define HOSTNAME_LEN 128 
static char myname[HOSTNAME_LEN], rootname[HOSTNAME_LEN];

/**
 * return 1 if all processes are running on the same machine, 0 otherwise
 */
int single_cluster()
{
    int me,root=0,stat,len;

    gethostname(myname, HOSTNAME_LEN-1);
    me = armci_msg_me();
    if(me==root) {
        armci_msg_bcast(myname, HOSTNAME_LEN, root);
    } else {
        armci_msg_bcast(rootname, HOSTNAME_LEN, root);
    }

    len = strlen(myname);
    stat = (me==root) ? 0 : strncmp(rootname, myname, len);

    if(stat != 0) {
        stat = 1;
    }

    armci_msg_reduce(&stat, 1, "+", ARMCI_INT);

    if(stat) {
        return 0;
    } else {
        return 1;
    }
}
