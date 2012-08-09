/* -*- c-basic-offset: 4; indent-tabs-mode: t; -*- */
/*  vim: set sw=4 ts=8 noexpandtab : */

/*
 * Copyright (c) 2012 Cray Inc. All Rights Reserved.
 *
 * The contents of this file is proprietary information of Cray Inc.
 * and may not be disclosed without prior written consent.
 *
 * DMAPP scalable lock implementation
 *
 * $HeadURL$
 * $LastChangedRevision$
 */

#ifndef _DMAPP_LOCK_H
#define _DMAPP_LOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <dmapp.h>

extern dmapp_jobinfo_t  _dmapp_jobinfo;

#ifdef DMAPP_DEBUG_LOCK
#define PRINTF(fmt,...)  do { printf(fmt, ## __VA_ARGS__); } while (0)
#else
#define PRINTF(fmt,...)
#endif

/*
 * MCS lock implementation
 */

#if defined(__GNUC__)

#define rep_nop() asm volatile("rep;nop":::"memory")
#define mb()      asm volatile("mfence":::"memory")
#define wmb()     asm volatile("sfence":::"memory")

#define PAUSE()                 rep_nop()
#define MEMBAR_STORESTORE()     wmb()
#define MEMBAR_LOADLOAD()       mb()

#else

#define PAUSE()
#define MEMBAR_STORESTORE()
#define MEMBAR_LOADLOAD()

#endif

#define _LOCK_FREE  -1
#define _LOCK_RESET  0L
#define _LOCK_SET    1

typedef struct _dmapp_node {
        union {
                struct {
                        uint32_t locked;
                        int32_t  next;
                } s;
                uint64_t qword;
        } u;
} _dmapp_node_t;

typedef struct _dmapp_locknode {
        _dmapp_node_t node;
        _dmapp_node_t lock;
} _dmapp_locknode_t;

#define _LOCKED u.s.locked
#define _NEXT   u.s.next
#define _QWORD  u.qword

typedef struct _dmapp_locknode *dmapp_lock_handle_t;

/* Simple HASH to map lock addresses onto owner ranks */
#define DMAPPI_LOCK_OWNER(LOCK, NPES) ((((uint64_t)(LOCK)) >> 3) % (NPES))

static inline
dmapp_return_t
_dmappi_do_lock(_dmapp_locknode_t *locknode, dmapp_pe_t owner, uint64_t flags, int try)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;
        long locked, prev_pe;

        _dmapp_node_t tmp;
        _dmapp_node_t *lock = &locknode->lock;
        _dmapp_node_t *node = &locknode->node;

        node->_NEXT = _LOCK_FREE;
        /* This flag gets cleared (remotely) once the lock is dropped */
        node->_LOCKED = _LOCK_SET;

        MEMBAR_STORESTORE();

        PRINTF("[%02d]: lock %p node %p owner %d\n", _dmapp_jobinfo.pe, node, lock, owner);

        /* Form our lock request */
        tmp._LOCKED = _LOCK_SET;
        tmp._NEXT = _dmapp_jobinfo.pe;

	if (try) {
	    /* Swap pe into the global lock owner, returning previous value, atomically
	     * For 'try' lock we only do this if the current lock is RESET (i.e. not locked)
	     */
	    status = dmapp_acswap_qw(&tmp._QWORD, &lock->_QWORD, &(_dmapp_jobinfo.sheap_seg), owner,
				     _LOCK_RESET /* compare */, tmp._QWORD /* swap */);
	    if (status != DMAPP_RC_SUCCESS)
                return status;
	}
	else {
	    /* Swap pe into the global lock owner, returning previous value, atomically */
	    /* Gemini does not support SWAP, so emulate it with the AFAX operation */
	    status = dmapp_afax_qw(&tmp._QWORD, &lock->_QWORD, &(_dmapp_jobinfo.sheap_seg), owner,
				   0 /* AND mask */, tmp._QWORD /* XOR mask */);
	    if (status != DMAPP_RC_SUCCESS)
                return status;
	}

        /* Extract the global lock (tail) state */
        prev_pe = tmp._NEXT;
        locked = tmp._LOCKED;

        PRINTF("[%02d]: Lock tail prev_pe %ld locked %ld\n", _dmapp_jobinfo.pe, prev_pe, locked);

        /* Is the lock held by someone else ? */
        if (locked == _LOCK_SET)
        {
                PRINTF("[%02d]: LOCKED: update next %p in %ld\n", _dmapp_jobinfo.pe, &node->_NEXT, prev_pe);

		/* For 'try' lock this means we didn't acquire the lock and so can exit here */
		if (try)
		    return DMAPP_RC_NOT_DONE;

                /* I'm now next in global linked list, update node next in the prev_pe process with our pe */
                status = dmapp_put(&node->_NEXT, &(_dmapp_jobinfo.sheap_seg), prev_pe, &_dmapp_jobinfo.pe, 1, DMAPP_DW);
                if (status != DMAPP_RC_SUCCESS)
                        return status;

                /* Wait for flag to be released */
                do { PAUSE(); } while (node->_LOCKED);

                PRINTF("[%02d]: LOCK %p obtained\n", _dmapp_jobinfo.pe, lock);
        }

        MEMBAR_LOADLOAD();

        return status;
}

static inline
dmapp_return_t
_dmappi_do_unlock(_dmapp_locknode_t *locknode, dmapp_pe_t owner, uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;

        _dmapp_node_t *lock = &locknode->lock;
        _dmapp_node_t *node = &locknode->node;

        static int reset = _LOCK_RESET;

        PRINTF("[%02d]: dropping lock %p\n", _dmapp_jobinfo.pe, locknode);

        MEMBAR_STORESTORE();

        /* Is there someone on the linked list ? */
        if (node->_NEXT == _LOCK_FREE)
        {
                /* No one is currently on the linked list */
                _dmapp_node_t tmp;

                /* Form the remote atomic compare value */
                tmp._LOCKED = _LOCK_SET;
                tmp._NEXT = _dmapp_jobinfo.pe;

                /* If global lock owner value still equals pe, load RESET into it & return prev value */
                status = dmapp_acswap_qw(&tmp._QWORD, &lock->_QWORD, &(_dmapp_jobinfo.sheap_seg), owner,
                                         tmp._QWORD, _LOCK_RESET);
                if (status != DMAPP_RC_SUCCESS)
                        return status;

                PRINTF("[%02d]: cswap returned next %d\n", _dmapp_jobinfo.pe, tmp._NEXT);

                if (tmp._NEXT == _dmapp_jobinfo.pe)
                        /* We were the only/final requestor, all done */
                        return status;

                /* Somebody is about to chain themself off us, wait for them to do it */
                do { PAUSE(); } while ((node->_NEXT == _LOCK_FREE));
        }

        MEMBAR_LOADLOAD();

        /*
         * Release any waiters on the linked list
         */

        PRINTF("[%02d]: releasing %ld @ %p\n", _dmapp_jobinfo.pe, node->_NEXT, &node->_LOCKED);

        /* Write 0 into the node locked flag on PE<_NEXT> */
        status = dmapp_put(&node->_LOCKED, &(_dmapp_jobinfo.sheap_seg), node->_NEXT, &reset, 1, DMAPP_DW);

        return status;
}

dmapp_return_t
dmapp_lock_alloc(IN  dmapp_lock_handle_t *locks,
		 IN  uint32_t nlocks,
		 IN  uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;
        _dmapp_locknode_t *locknodes = NULL;
	int i;

        if (locks == NULL | nlocks == 0)
                return DMAPP_RC_INVALID_PARAM;

        /* Allocate the DMAPP lock structure from symmetric memory for all locks */
        locknodes = (_dmapp_locknode_t *)dmapp_sheap_malloc(sizeof(_dmapp_locknode_t)*nlocks);
        if (locknodes == NULL)
                return DMAPP_RC_RESOURCE_ERROR;

	PRINTF("[%02d] Allocated %d locks @ %p\n", _dmapp_jobinfo.pe, nlocks, locknodes);

        memset(locknodes, 0, sizeof(_dmapp_locknode_t)*nlocks);

        /* Return all lock structure handles to caller */
	for (i = 0; i < nlocks; i++) {
	    locks[i] = &locknodes[i];
	}

        return status;
}

dmapp_return_t
dmapp_lock_free(IN  dmapp_lock_handle_t *locks,
		IN  uint32_t nlocks,
		IN  uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;

        if (locks == NULL || nlocks == 0)
                return DMAPP_RC_INVALID_PARAM;

	PRINTF("[%02d] Freeing %d locks @ %p\n", _dmapp_jobinfo.pe, nlocks, locks[0]);

        dmapp_sheap_free(locks[0]);

        return status;
}

dmapp_return_t
dmapp_lock_acquire(IN  dmapp_lock_handle_t lock,
		   IN  uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;
        dmapp_pe_t owner;

        if (lock == NULL)
                return DMAPP_RC_INVALID_PARAM;

        owner = DMAPPI_LOCK_OWNER(lock, _dmapp_jobinfo.npes);
        status = _dmappi_do_lock(lock, owner, flags, 0);

        return status;
}

dmapp_return_t
dmapp_lock_try(IN  dmapp_lock_handle_t lock,
	       IN  uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;
        dmapp_pe_t owner;

        if (lock == NULL)
                return DMAPP_RC_INVALID_PARAM;

        owner = DMAPPI_LOCK_OWNER(lock, _dmapp_jobinfo.npes);
        status = _dmappi_do_lock(lock, owner, flags, 1);

        return status;
}

dmapp_return_t
dmapp_lock_release(IN  dmapp_lock_handle_t lock,
		   IN  uint64_t flags)
{
        dmapp_return_t status = DMAPP_RC_SUCCESS;
        dmapp_pe_t owner;

        if (lock == NULL)
                return DMAPP_RC_INVALID_PARAM;

        owner = DMAPPI_LOCK_OWNER(lock, _dmapp_jobinfo.npes);
        status = _dmappi_do_unlock(lock, owner, flags);

        return status;
}

#endif /* _DMAPP_LOCK_H */
