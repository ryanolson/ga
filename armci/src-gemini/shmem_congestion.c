
/*
 * Copyright (c) 2010 Cray Inc.
 *
 * The contents of this file is proprietary information of Cray Inc. 
 * and may not be disclosed without prior written consent.
 *
 * $HeadURL:$
 * $LastChangedRevision:$
 */

/* 
 * Gemini Network Congestion (gnc) detection and avoidance.
 * See shmem_congestion.h for more details.
 */

/****************************************************************************
 *          
 *      Include files
 *         
 ***************************************************************************/

#include "shmem_internal.h"
#include "shmem_congestion.h"


/****************************************************************************
 *            
 *      Globals
 *           
 ***************************************************************************/

int             _sma_gnc_avoidance;
int             _sma_gnc_stats_enabled;
int             _sma_gnc_warn;
int             _sma_gnc_backoff_factor;
int             _sma_gnc_consecutive_hits_threshold;
int             _sma_gnc_consecutive_hits;
int             _sma_gnc_env_display;

uint64_t        _sma_gnc_latency_threshold_usecs;
uint64_t        _sma_gnc_latency_threshold_1;
uint64_t        _sma_gnc_latency_threshold_2;
uint64_t        _sma_gnc_latency_threshold_3;
uint64_t        _sma_gnc_wait_ticks;
uint64_t        _sma_gnc_wait_ticks_limit_usecs;
uint64_t        _sma_gnc_wait_ticks_limit;
uint64_t        _sma_gnc_resume_time;
uint64_t        _sma_gnc_clock_rate;
uint64_t        _sma_gnc_datasize_threshold;
uint64_t        _sma_gnc_stats[_SHMEM_GNC_STATS_SIZE];


/*****************************************************************************
 *                                                                           *
 *      Functions                                                            *
 *                                                                           *
 ****************************************************************************/

/*
 * Get the processor clock rate to convert between clock ticks and seconds,
 * or microseconds.  For congestion avoidance, we mostly report in terms
 * of microseconds so this function returns ticks per microseconds.
 */

uint64_t
_smai_gnc_clock_rate(void)
{
    FILE *fp;
    char string[BUFSIZ];
    char *mhz_line;
    int mhz;
    int rc;

    /* Get the processor clock rate. */

    fp = fopen("/proc/cpuinfo", "r");
    if (fp == NULL) {
        _smai_abort(-1, " LIBSMA ERROR: could not read /proc/cpuinfo\n");
    }

    mhz_line = NULL;
    while (mhz_line == NULL) {
        if (fgets(string, BUFSIZ, fp) == NULL) {
            break;
        }
        else {
            mhz_line = strstr(string, "MHz");
        }
    }
    fclose(fp);

    if (mhz_line == NULL) {
        _smai_abort(-1, " LIBSMA ERROR: could not find processor clock info\n");
    }

    rc = sscanf(mhz_line, "MHz : %d.", &mhz);
    if (rc == 0) {
        _smai_abort(-1, " LIBSMA ERROR: could not determine processor clock rate\n");
    }

    return (uint64_t)mhz;
}


void
_smai_gnc_init(void)
{
    /* Set a few global variables. */

    _sma_gnc_resume_time = 0L;

    _sma_gnc_consecutive_hits = 0;

    _sma_gnc_clock_rate = _smai_gnc_clock_rate();

    /* These are the environment variables that control congestion avoidance
       code in libsma.  These are all undocumented, not intended
       to be set by users.  See shmem_congestion.h for descriptions. */
  
    _sma_gnc_avoidance = _smai_env_process_int(
            "_SHMEM_GNC_AVOIDANCE", 1, 0, 1, 1);

    _sma_gnc_stats_enabled = _smai_env_process_int(
            "_SHMEM_GNC_STATS", 0, 0, 9, 1);

    _sma_gnc_warn = _smai_env_process_int(
            "_SHMEM_GNC_WARN", 0, 0, 1, 1);

    _sma_gnc_backoff_factor = _smai_env_process_int(
            "_SHMEM_GNC_BACKOFF", 8, 0, INT_MAX, 1);          

    _sma_gnc_consecutive_hits_threshold = _smai_env_process_int(
            "_SHMEM_GNC_CONSECUTIVE_HITS", 2, 0, 9, 1);          

    _sma_gnc_env_display = _smai_env_process_int(
            "_SHMEM_GNC_ENV_DISPLAY", 0, 0, 1, 1);

    _sma_gnc_latency_threshold_usecs = _smai_env_process_long(
            "_SHMEM_GNC_THRESHOLD", 1000, 0, LONG_MAX, 1);

    _sma_gnc_latency_threshold_1 = _sma_gnc_latency_threshold_usecs *
                                   _sma_gnc_clock_rate;
    _sma_gnc_latency_threshold_2 =  10 * _sma_gnc_latency_threshold_1;
    _sma_gnc_latency_threshold_3 = 100 * _sma_gnc_latency_threshold_1;

    _sma_gnc_wait_ticks_limit_usecs = _smai_env_process_long(
            "_SHMEM_GNC_MAX_DELAY", 2000000L, 0, LONG_MAX, 1);
    _sma_gnc_wait_ticks_limit = _sma_gnc_wait_ticks_limit_usecs *
                                _sma_gnc_clock_rate;

    _sma_gnc_datasize_threshold = _smai_env_process_long(
            "_SHMEM_GNC_DATASIZE_THRESHOLD", 419430400L, 0, LONG_MAX, 1);

    /* Now the stats array. */

    GNC_STATS_MAX_LATENCY_1 = 0;
    GNC_STATS_MAX_LATENCY_2 = 0;
    GNC_STATS_MIN_LATENCY   = INT64_MAX;
    GNC_STATS_MAX_WAIT      = 0;
    GNC_STATS_MIN_WAIT      = INT64_MAX;
    GNC_STATS_MAX_DATASIZE  = 0;
    GNC_STATS_MIN_DATASIZE  = INT64_MAX;
    GNC_STATS_N_CALLS       = 0;
    GNC_STATS_TOTAL_LATENCY = 0;
    GNC_STATS_N_WAITS       = 0;
    GNC_STATS_TOTAL_WAIT    = 0;

    return;
}


void
_smai_gnc_env_display(void)
{
    /* Called just by PE 0 and only if _sma_gnc_env_display set. */

    printf("  _SHMEM_GNC_ENV_DISPLAY     = %d\n", _sma_gnc_env_display);
    printf("  _SHMEM_GNC_AVOIDANCE       = %d\n", _sma_gnc_avoidance);
    printf("  _SHMEM_GNC_THRESHOLD       = %ld usecs\n",
                                           _sma_gnc_latency_threshold_usecs);
    printf("  _SHMEM_GNC_MAX_DELAY       = %ld usecs\n",
                                           _sma_gnc_wait_ticks_limit_usecs);
    printf("  _SHMEM_GNC_DATASIZE_THRESHOLD = %ld\n",
                                           _sma_gnc_datasize_threshold);
    printf("  _SHMEM_GNC_BACKOFF         = %d\n", _sma_gnc_backoff_factor);
    printf("  _SHMEM_GNC_CONSECUTIVE_HITS = %d\n",
                                           _sma_gnc_consecutive_hits_threshold);
    printf("  _SHMEM_GNC_STATS           = %d\n", _sma_gnc_stats_enabled);
    printf("  _SHMEM_GNC_WARN            = %d\n", _sma_gnc_warn);

    return;
}


/* Gather and print Gemini Network Congestion Avoidance stats. */

void
_smai_gnc_print_stats(void)
{
    if (unlikely(_sma_gnc_stats_enabled)) {
        int i;
        int j;
        int rc;
        int size = _SHMEM_GNC_STATS_SIZE;
        uint64_t *recv_buf;
        uint64_t max_latency_1 = 0;
        uint64_t max_latency_2 = 0;
        uint64_t min_latency = INT64_MAX;
        uint64_t max_wait = 0;
        uint64_t min_wait = INT64_MAX;
        uint64_t max_datasize = 0;
        uint64_t min_datasize = INT64_MAX;
        uint64_t n_calls = 0;
        uint64_t total_latency = 0;
        uint64_t n_waits = 0;
        uint64_t total_wait = 0;

        fflush(0);

        recv_buf = malloc(_sma_npes * sizeof(_sma_gnc_stats));
        if (recv_buf == NULL) {
            fprintf(_sma_errfile, " LIBSMA ERROR: malloc failed in finalize\n");
            goto err_end;
        }

        /* All PEs convert ticks to usecs. */

        GNC_STATS_MAX_LATENCY_1 /= _sma_gnc_clock_rate;
        GNC_STATS_MAX_LATENCY_2 /= _sma_gnc_clock_rate;
        GNC_STATS_MIN_LATENCY   /= _sma_gnc_clock_rate;
        GNC_STATS_MAX_WAIT      /= _sma_gnc_clock_rate;
        GNC_STATS_MIN_WAIT      /= _sma_gnc_clock_rate;
        GNC_STATS_TOTAL_LATENCY /= _sma_gnc_clock_rate;
        GNC_STATS_TOTAL_WAIT    /= _sma_gnc_clock_rate;


        if (_sma_gnc_stats_enabled > 1) {
            /* Verbose stats - from all PEs */
            uint64_t min;

            if (GNC_STATS_N_WAITS == 0) {
                min = 0;
            }
            else {
                min = GNC_STATS_MIN_WAIT;
            }
            printf(" LIBSMA: PE %05d; GNC N=%ld ml=%ld Ml1=%ld Ml2=%ld "
                   "Mw=%ld mw=%ld\n",
                   _sma_mype, GNC_STATS_N_WAITS,
                   GNC_STATS_MIN_LATENCY, GNC_STATS_MAX_LATENCY_1,
                   GNC_STATS_MAX_LATENCY_2, GNC_STATS_MAX_WAIT, min);
        }

        /* All PEs share the stats, though only PE 0 needs them. */

        rc = PMI_Allgather(_sma_gnc_stats, recv_buf, sizeof(_sma_gnc_stats));
        if (rc != PMI_SUCCESS) {
            fprintf(_sma_errfile, " LIBSMA ERROR: "
                        "PMI_Allgather failed (rc = %d)\n", rc);
            free(recv_buf);
            goto err_end;
        }

        if (_sma_mype == 0) {

            /* PE 0 will coalesce the data and print it. */

            for (i=0; i<_sma_npes*size; i+=size) {

                /* Latency */
                j = _SHMEM_GNC_STATS_MAX_LATENCY_1;
                if (recv_buf[i+j] > max_latency_1) {
                    max_latency_1 = recv_buf[i+j];
                }
                j = _SHMEM_GNC_STATS_MAX_LATENCY_2;
                if (recv_buf[i+j] > max_latency_2) {
                    max_latency_2 = recv_buf[i+j];
                }
                j = _SHMEM_GNC_STATS_MIN_LATENCY;
                if (recv_buf[i+j] < min_latency /* && recv_buf[i+j] != 0 */) {
                    min_latency = recv_buf[i+j];
                }

                /* Wait time */
                j = _SHMEM_GNC_STATS_MAX_WAIT;
                if (recv_buf[i+j] > max_wait) {
                    max_wait = recv_buf[i+j];
                }
                j = _SHMEM_GNC_STATS_MIN_WAIT;
                if (recv_buf[i+j] < min_wait && recv_buf[i+j] != 0) {
                    min_wait = recv_buf[i+j];
                }

                /* Datasize */ 
                j = _SHMEM_GNC_STATS_MAX_DATASIZE;
                if (recv_buf[i+j] > max_datasize) {
                    max_datasize = recv_buf[i+j];
                }
                j = _SHMEM_GNC_STATS_MIN_DATASIZE;
                if (recv_buf[i+j] < min_datasize && recv_buf[i+j] != 0) {
                    min_datasize = recv_buf[i+j];
                }

                /* N calls */
                j = _SHMEM_GNC_STATS_N_CALLS;
                n_calls += recv_buf[i+j];

                /* Total call latency */
                j = _SHMEM_GNC_STATS_TOTAL_LATENCY;
                total_latency += recv_buf[i+j];

                /* N waits */
                j = _SHMEM_GNC_STATS_N_WAITS;
                n_waits += recv_buf[i+j];

                /* Total wait */
                j = _SHMEM_GNC_STATS_TOTAL_WAIT;
                total_wait += recv_buf[i+j];
            }

            if (max_latency_1 == 0) {
                min_latency = 0;
            }

            if (n_waits == 0) {
                min_wait = 0;
            }

            printf(" LIBSMA: DMAPP call datasizes(bytes):    "
                   "min=%ld  max=%ld\n",
                   min_datasize,max_datasize);

            printf(" LIBSMA: DMAPP call latency(usecs): N=%3.2e "
                   "min=%3.2e max=%3.2e total=%3.2e max2=%3.2e\n",
                   (double)n_calls, (double)min_latency, (double)max_latency_1,
                   (double)total_latency, (double)max_latency_2);

            printf(" LIBSMA: DMAPP call waits(usecs):   N=%3.2e "
                   "min=%3.2e max=%3.2e total=%3.2e\n",
                   (double)n_waits, (double)min_wait, (double)max_wait,
                   (double)total_wait);

            if (total_latency > 0) {
                printf(" LIBSMA: DMAPP total call waits %d%% of total call "
                       "latency\n", (int)((total_wait * 100) / total_latency));
            }
        }

    free(recv_buf);

    }

err_end:
    return;
}

