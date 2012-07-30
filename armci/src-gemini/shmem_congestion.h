
/*
 * Copyright (c) 2010 Cray Inc.
 *
 * The contents of this file is proprietary information of Cray Inc. 
 * and may not be disclosed without prior written consent.
 *
 * $HeadURL$
 * $LastChangedRevision$
 */

/*
 * SHMEM internal header file containing macros and declarations for
 * Gemini network congestion avoidance.  Include this in files where
 * DMAPP calls may contribute to congestion or may need to avoid
 * congestion and place the appropriate macros around the DMAPP calls.
 */

#ifndef _SHMEM_CONGESTION_H
#define _SHMEM_CONGESTION_H


/****************************************************************************
 *          
 *      Include files
 *         
 ***************************************************************************/

/* Assumes that file including this file has already included
 * shmem_internal.h */

#include <stdint.h>
#include <string.h>

/****************************************************************************
 *            
 *      Constants
 *           
 ***************************************************************************/

/* libpgas only tests for congestion if the size for the data being
   transferred is less than CACHELINE_LENGTH.  This is probably not
   an appropriate limitation for SHMEM.  However, it is uses for one
   of the size tests in _smai_gnc_test_for_congestion(). */

#define CACHELINE_LENGTH        64


/*************************************************************************
 *
 *      Typedefs, Structures
 *
 ************************************************************************/

/* shmem_gnc_stats_t is an emun for Gemini network congestion avoidance
   statistics.  Each enum is the offset in the _sma_gnc_stats array for
   the specified data.  Placing it in a single array allows getting the
   collective data to PE 0 in a single PMI_Allgather() call in
   shmem_finalize().  The collection and printing is controlled by an
   environment variable.  */

typedef enum {
  _SHMEM_GNC_STATS_MAX_LATENCY_1,
  _SHMEM_GNC_STATS_MAX_LATENCY_2,
  _SHMEM_GNC_STATS_MIN_LATENCY,
  _SHMEM_GNC_STATS_MAX_WAIT,
  _SHMEM_GNC_STATS_MIN_WAIT,
  _SHMEM_GNC_STATS_N_CALLS,
  _SHMEM_GNC_STATS_TOTAL_LATENCY,
  _SHMEM_GNC_STATS_N_WAITS,
  _SHMEM_GNC_STATS_TOTAL_WAIT,
  _SHMEM_GNC_STATS_MAX_DATASIZE,
  _SHMEM_GNC_STATS_MIN_DATASIZE,
  _SHMEM_GNC_STATS_SIZE,             /* size of stats array */
} shmem_gnc_stats_t;


/****************************************************************************
 *            
 *      Environment Variables
 *           
 ***************************************************************************/

/*
    These are the environment variables that control congestion avoidance
    code in libsma.  These are not meant for the users and we don't
    plan to document them.  The comments here can serve as description
    for any special situations that require someone to do some tuning.
    libpgas has a similar set of environment variables (beginning with
    PGAS_CONGESTION) for controlling congestion avoidance but the meanings
    aren't necessarily exactly the same in libsma and libpgas.

    _SHMEM_GNC_AVOIDANCE
      If set to 1, the congestion avoidance implementation is used.  If set to
      0, the congestion avoidance implementation is disabled.  Default is 1.

    _SHMEM_GNC_THRESHOLD
      Threshold time in microseconds for dmapp call latency.  If the timed
      dmapp call takes longer than this threshold to complete, the assumption
      is that the network has become congested and another dmapp call will not
      be made for a period of time.  This threshold value is scaled for
      several data transfer size catagories:
        If <= 64 (cache line size) then the threshold is the environment
          variable value.  
        Else if <= SHMEM_BTE_THRESHOLD (default 4096) then the threshold is
          the environment variable value times 10.
        Else if <= _SHMEM_GNC_DATASIZE_THRESHOLD (see below) then the
          threshold is the environment variable value times 100.
        Else if > _SHMEM_GNC_DATASIZE_THRESHOLD then no backoff will
          be done for this transfer.
      Default is 1000 microseconds (1 millisecond).

    _SHMEM_GNC_DATASIZE_THRESHOLD
      Threshold size in bytes for which data transfers are candidates for
      throttling.  If the size of the data transfer is above the threshold,
      no throttling will be done.  The theory is that large transfers can
      take a long time even without congestion and so we can't know from
      the call time if throttling is needed - and we don't want to throttle
      unnecessarily.  I (knaak) don't necessarily agree with this so this
      environment variable, which is not defined for or used by libpgas,
      can be used for testing.
      Default is one cacheline length, 64 bytes.

    _SHMEM_GNC_CONSECUTIVE_HITS
      OS jitter and other transient system activity can cause a single
      dmapp call to take much longer than normal.  But if several
      consecutive calls take much longer than normal, this is a good 
      indication of network congestion.  This environment variable sets
      a threshold for how many consecutive calls, that is greater than the
      threshold value, must be slow before initiating a backoff.  
      Default is 2.

    _SHMEM_GNC_BACKOFF
      An integer amount by which to scale the wait time when congestion has
      been detected:
        wait_time = backoff * ( measured_time - threshold_time )
      The default value has been determine empirically.
      Default is 8.

    _SHMEM_GNC_MAX_DELAY
      Maximum time in microseconds the runtime will sleep due to congestion.
      Default is 2000000 microseconds (2 seconds).

    _SHMEM_GNC_STATS
      If set to 1, PE 0 will print at shmem_finalize() various statistics
      about the congestion avoidance implementation, including configuration,
      number of times congestion was detected, maximum observed latency,
      and time spent waiting.  This data will be gathered from all PEs.
      Default is 0.

#if 0
    _SHMEM_GNC_WARN
    (will probably remove this, or enhance it, if possible, to identify where
     in the user's code the SHMEM call is coming from.)
      If set to 1, PEs will print a warning message to stderr when congestion
      is detected, and when waiting is actually done due to congestion.  At large scale, this
      could potentioally be a lot of output.  This is mostly for debugging 
      at smaller scale.
      Default is 0.
#endif
 */


/****************************************************************************
 *            
 *      Externs
 *           
 ***************************************************************************/

extern int        _sma_gnc_avoidance;           /* _SHMEM_GNC_AVOIDANCE */
extern int        _sma_gnc_stats_enabled;       /* _SHMEM_GNC_STATS     */
extern int        _sma_gnc_warn;                /* _SHMEM_GNC_WARN      */
extern int        _sma_gnc_backoff_factor;      /* _SHMEM_GNC_BACKOFF   */
extern int        _sma_gnc_consecutive_hits_threshold;  /* _SHMEM_GNC_CONSECUTIVE_HITS */
extern int        _sma_gnc_consecutive_hits;
extern int        _sma_gnc_env_display;         /* _SHMEM_GNC_ENV_DISPLAY */
extern uint64_t   _sma_gnc_latency_threshold_1; /* _SHMEM_GNC_THRESHOLD */
extern uint64_t   _sma_gnc_latency_threshold_2; /* _SHMEM_GNC_THRESHOLD */
extern uint64_t   _sma_gnc_latency_threshold_3; /* _SHMEM_GNC_THRESHOLD */
extern uint64_t   _sma_gnc_wait_ticks_limit;    /* _SHMEM_GNC_MAX_DELAY */
extern uint64_t   _sma_gnc_wait_ticks;
extern uint64_t   _sma_gnc_resume_time;
extern uint64_t   _sma_gnc_clock_rate;
extern uint64_t   _sma_gnc_stats[_SHMEM_GNC_STATS_SIZE];
extern uint64_t   _sma_gnc_datasize_threshold;

extern void       _smai_gnc_init(void);
extern void       _smai_gnc_env_display(void);
extern void       _smai_gnc_print_stats(void);

/* The following macro declares local variables passed to and from the
   inlined _smai_gnc_* routines.  They are declared in a macro so that
   the congestion detection and avoidance code does not clutter up the
   primary SHMEM/DMAPP code.  Include this macro once for each routine
   that detects and/or avoids congestion. */
  
#define CONGESTION_LOCAL_DECLARATIONS \
uint64_t _sma_gnc_call_start_time; \
uint64_t _sma_gnc_call_end_time;

/* All the CONGESTION macros can be defined as nothing to completely
   eliminate all runtime congestion code.  */


/****************************************************************************
 *            
 *      Object-like Macros
 *           
 ***************************************************************************/

/* This set of macros simply defines short names for longer, globally
 * visible variable names. */

#define GNC_AVOIDANCE           _sma_gnc_avoidance
#define GNC_STATS_ENABLED       _sma_gnc_stats_enabled
#define GNC_WARN                _sma_gnc_warn
#define GNC_BACKOFF_FACTOR      _sma_gnc_backoff_factor

#define GNC_CLOCK_RATE          _sma_gnc_clock_rate
#define GNC_LATENCY_THRESHOLD_1 _sma_gnc_latency_threshold_1
#define GNC_LATENCY_THRESHOLD_2 _sma_gnc_latency_threshold_2
#define GNC_LATENCY_THRESHOLD_3 _sma_gnc_latency_threshold_3
#define GNC_WAIT_TICKS          _sma_gnc_wait_ticks
#define GNC_WAIT_TICKS_LIMIT    _sma_gnc_wait_ticks_limit
#define GNC_RESUME_TIME         _sma_gnc_resume_time
#define GNC_DATASIZE_THRESHOLD  _sma_gnc_datasize_threshold

#define GNC_STATS_CLOCK_RATE    _sma_gnc_stats[_SHMEM_GNC_STATS_CLOCK_RATE]
#define GNC_STATS_MAX_LATENCY_1 _sma_gnc_stats[_SHMEM_GNC_STATS_MAX_LATENCY_1]
#define GNC_STATS_MAX_LATENCY_2 _sma_gnc_stats[_SHMEM_GNC_STATS_MAX_LATENCY_2]
#define GNC_STATS_MIN_LATENCY   _sma_gnc_stats[_SHMEM_GNC_STATS_MIN_LATENCY]
#define GNC_STATS_MAX_WAIT      _sma_gnc_stats[_SHMEM_GNC_STATS_MAX_WAIT]
#define GNC_STATS_MIN_WAIT      _sma_gnc_stats[_SHMEM_GNC_STATS_MIN_WAIT]
#define GNC_STATS_N_CALLS       _sma_gnc_stats[_SHMEM_GNC_STATS_N_CALLS]
#define GNC_STATS_TOTAL_LATENCY _sma_gnc_stats[_SHMEM_GNC_STATS_TOTAL_LATENCY]
#define GNC_STATS_N_WAITS       _sma_gnc_stats[_SHMEM_GNC_STATS_N_WAITS]
#define GNC_STATS_TOTAL_WAIT    _sma_gnc_stats[_SHMEM_GNC_STATS_TOTAL_WAIT]
#define GNC_STATS_MAX_DATASIZE  _sma_gnc_stats[_SHMEM_GNC_STATS_MAX_DATASIZE]
#define GNC_STATS_MIN_DATASIZE  _sma_gnc_stats[_SHMEM_GNC_STATS_MIN_DATASIZE]


/****************************************************************************
 *            
 *      Inline Routines and Routine-like Macros
 *           
 ***************************************************************************/

/* The "static inline" routines in this file are included in a .h file
 * rather than in a .c file to minimize the overhead of calling them.  */

static inline unsigned long
_smai_rtc (void)
{
  unsigned long high, low;
  /* read the 64 bit process cycle counter into low/high */ 
  /* RDTSC instruction is opcode 0x0f 0x31 */
  asm volatile(".byte 0x0f,0x31" : "=a" (low), "=d" (high));

  return ((unsigned long) high << 32) + low;
}


/* _smai_gnc_wait_if_congestion() does a local spin-wait if the current time
 * has not yet reached the resume time that was set by the last test for
 * congestion.  A resume time of zero indicates that the last test did not
 * detect any congestion.  */

static inline void
_smai_gnc_wait_if_congestion(const char *func, int line)
{
    /* There are 3 conditions for waiting before making a following DMAPP call:
       1) congestion avoidance is enabled
       2) congestion was detected after the previous DMAPP call,
          as flagged by a resume time greater than 0
       3) the current time is still less than the resume time */

    if (_sma_gnc_avoidance && _sma_gnc_resume_time > 0 &&
                              _smai_rtc() < _sma_gnc_resume_time) {

        /* Determine how much more wait time is needed. 
           The obvious value is how many ticks between the current time
           and the resume time.  However, 
           we limit the wait to a maximum of what was calculated as
           needed after the previous DMAPP call.  This check is needed
           because of possible clock skew if processes were moved to 
           different processors since the resume time was set.  */

        uint64_t wait;

        wait = _sma_gnc_resume_time - _smai_rtc();
        if (wait > _sma_gnc_wait_ticks) {
            wait = _sma_gnc_wait_ticks;
        }

#if 0 /* knaak: probably will remove warn */
        /* Not sure how good an idea to do prints here but for good for
           early debugging. */
           
        if (_sma_gnc_warn) {
            printf(" LIBSMA: PE %05d; GNC Await=%9ld; in %s:%d\n",
                    _sma_mype, wait, func, line);
        }
#endif

        /* Update the stats array. */

        if (_sma_gnc_stats_enabled) {
            __sync_fetch_and_add(&(GNC_STATS_N_WAITS), 1);
            __sync_fetch_and_add(&(GNC_STATS_TOTAL_WAIT), wait);
            if (wait > GNC_STATS_MAX_WAIT) {
                GNC_STATS_MAX_WAIT = wait;
            }
            if (wait < GNC_STATS_MIN_WAIT) {
                GNC_STATS_MIN_WAIT = wait;
            }
        }

        /* Now just loop until resume time is reached. */

        _sma_gnc_resume_time = _smai_rtc() + wait;
        while (_smai_rtc() < _sma_gnc_resume_time);

        /* Hopefully, congestion has now lessened or gone away.  Set the
           resume time and the wait ticks to zero for the next DMAPP call.
           It is not necessary to wait before every DMAPP call when there
           is congestion.  It is just necessary to wait often enough and
           long enough to give the network time to catch up. */

        _sma_gnc_resume_time = 0L;
        _sma_gnc_wait_ticks = 0L;
    }

    return;
}

#define WAIT_IF_CONGESTION(); \
_smai_gnc_wait_if_congestion(__func__, __LINE__);


static inline void
_smai_gnc_test_for_congestion(uint64_t call_start_time, uint64_t call_end_time,
                              uint64_t size, const char *func, int line)
{
    uint64_t call_latency = 0;
    uint64_t latency_threshold = 0;
    if (call_start_time < call_end_time) {

        /* Test for this because clock skew could result in negative
           latency.  If it is negative, use the initialized value of 0. */

        call_latency = call_end_time - call_start_time;
    }

    if (unlikely(_sma_gnc_stats_enabled)) {

        /* Even if avoidance is not enabled, getting a measure of the
           range of call times and data sizes is informative (at least
           in specific performance testing).  MAX_LATENCY_1 is the true
           maximum measured latency.  MAX_LATENCY_2 is the maximum 
           latency for those calls that pass the consecutive_hits_threshold
           test. */

        __sync_fetch_and_add(&(GNC_STATS_N_CALLS), 1);
        __sync_fetch_and_add(&(GNC_STATS_TOTAL_LATENCY), call_latency);

        if (call_latency > GNC_STATS_MAX_LATENCY_1) {
            GNC_STATS_MAX_LATENCY_1 = call_latency;
        }
        if (call_latency < GNC_STATS_MIN_LATENCY) {
            GNC_STATS_MIN_LATENCY = call_latency;
        }
        if (size > GNC_STATS_MAX_DATASIZE) {
            GNC_STATS_MAX_DATASIZE = size;
        }
        if (size < GNC_STATS_MIN_DATASIZE) {
            GNC_STATS_MIN_DATASIZE = size;
        }
    }

    if (size <= CACHELINE_LENGTH) {
        latency_threshold = _sma_gnc_latency_threshold_1;
    }
    else if (size <= _sma_dmapp_bte_threshold) {
        latency_threshold = _sma_gnc_latency_threshold_2;
    }
    else if (size <= _sma_gnc_datasize_threshold) {
        latency_threshold = _sma_gnc_latency_threshold_3;
    }
    else {
        /* Setting the threshold to INT64_MAX is essentially saying
           that for transfers above the size threshold, don't test for
           congestion. */
        latency_threshold = INT64_MAX;
    }

    if (_sma_gnc_avoidance && call_latency > latency_threshold &&
        ++_sma_gnc_consecutive_hits > _sma_gnc_consecutive_hits_threshold) {

        if (call_latency > GNC_STATS_MAX_LATENCY_2) {
            GNC_STATS_MAX_LATENCY_2 = call_latency;
        }

        /* The just completed DMAPP call took longer than the threshold.
           We may need to do a wait before the next DMAPP call.  A single
           slow call can be caused by OS jitter or some transient hiccup
           so that would not be a solid indication of network congestion.
           So only if there are consecutive slow calls will we suspect
           congestion.  We multiply the
           amount that the call latency is over the threshold by a multiplier.
           This amplifies the wait time the more the call latency is over
           the threshold. */

        _sma_gnc_wait_ticks = _sma_gnc_backoff_factor *
                              (call_latency - latency_threshold);

        /* But we won't allow a wait of more than _sma_gnc_wait_ticks_limit. */

        if (_sma_gnc_wait_ticks > _sma_gnc_wait_ticks_limit) {
            _sma_gnc_wait_ticks = _sma_gnc_wait_ticks_limit;
        }

        _sma_gnc_resume_time = call_end_time + _sma_gnc_wait_ticks;

        /* Reset the consecutive slow calls counter. */

        _sma_gnc_consecutive_hits = 0;

#if 0 /* knaak: probably will remove warn */
        if (unlikely(_sma_gnc_warn)) {
            printf(" LIBSMA: PE %05d; GNC Sz=%ld Lat=%9ld Ewait=%9ld "
                   "in %s:%d\n",
                   _sma_mype, size, call_latency, _sma_gnc_wait_ticks,
                   func, line);
        }
#endif
    }
    else {

        /* No wait is needed before the next DMAPP call. */

        _sma_gnc_resume_time = 0;
        _sma_gnc_wait_ticks = 0;
    }
    return;
}

#define TEST_FOR_CONGESTION(size); \
_smai_gnc_test_for_congestion(_sma_gnc_call_start_time, \
                              _sma_gnc_call_end_time, (uint64_t)size, \
                              __func__, __LINE__);


/* The start and end timer routines set the block scope variables
   _smai_gnc_call_start_time and _smai_gnc_call_end_time, which are
   declared in the CONGESTION_LOCAL_DECLARATIONS macro.  These are
   then used by the test for congestion routine, which uses them to
   calculate the DMAPP call latency.  This is done to simply hide
   the details from the primary SHMEM library code so that it doesn't
   clutter up the main code flow. */

static inline void
_smai_gnc_start_congestion_timer(uint64_t *call_start_time)
{
    *call_start_time = _smai_rtc();
}

#define START_CONGESTION_TIMER(); \
_smai_gnc_start_congestion_timer(&_sma_gnc_call_start_time);


static inline void
_smai_gnc_stop_congestion_timer(uint64_t *call_end_time)
{
    *call_end_time = _smai_rtc();
}

#define STOP_CONGESTION_TIMER(); \
_smai_gnc_stop_congestion_timer(&_sma_gnc_call_end_time);

#endif /* shmem_congestion.h */

