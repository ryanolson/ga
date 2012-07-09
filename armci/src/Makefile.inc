libarmci_la_SOURCES += src/collectives/message.c
libarmci_la_SOURCES += src/common/aggregate.c
libarmci_la_SOURCES += src/common/armci.c
libarmci_la_SOURCES += src/common/ccopy.c
libarmci_la_SOURCES += src/common/clusterinfo.c
libarmci_la_SOURCES += src/common/pack.c
libarmci_la_SOURCES += src/common/iterator.c
libarmci_la_SOURCES += src/include/iterator.h
libarmci_la_SOURCES += src/ft/armci_chkpt.h
libarmci_la_SOURCES += src/ft/armci_storage.h
libarmci_la_SOURCES += src/include/acc.h
libarmci_la_SOURCES += src/include/armcip.h
libarmci_la_SOURCES += src/include/atomics-i386.h
libarmci_la_SOURCES += src/include/copy.h
libarmci_la_SOURCES += src/include/fujitsu-vpp.h
libarmci_la_SOURCES += src/include/kr_malloc.h
libarmci_la_SOURCES += src/include/locks.h
libarmci_la_SOURCES += src/include/memlock.h
libarmci_la_SOURCES += src/include/request.h
libarmci_la_SOURCES += src/include/semaphores.h
libarmci_la_SOURCES += src/include/shmalloc.h
libarmci_la_SOURCES += src/include/shmem.h
libarmci_la_SOURCES += src/include/shmlimit.h
libarmci_la_SOURCES += src/include/signaltrap.h
libarmci_la_SOURCES += src/include/spinlock.h
libarmci_la_SOURCES += src/include/tas-alpha.h
libarmci_la_SOURCES += src/include/tas-i386.h
libarmci_la_SOURCES += src/include/tas-ia64.h
libarmci_la_SOURCES += src/include/tas-ppc.h
libarmci_la_SOURCES += src/include/utils.h
libarmci_la_SOURCES += src/locks/locks.c
libarmci_la_SOURCES += src/locks/memlock.c
libarmci_la_SOURCES += src/locks/mutex.c
libarmci_la_SOURCES += src/locks/semaphores.c
libarmci_la_SOURCES += src/memory/kr_malloc.c
libarmci_la_SOURCES += src/memory/memory.c
libarmci_la_SOURCES += src/progress/fence.c
libarmci_la_SOURCES += src/progress/wait.c
libarmci_la_SOURCES += src/xfer/caccumulate.c
libarmci_la_SOURCES += src/xfer/rmw.c
libarmci_la_SOURCES += src/xfer/strided.c
libarmci_la_SOURCES += src/xfer/vector.c
if ENABLE_F77
libarmci_la_SOURCES += src/common/fcopy.f
libarmci_la_SOURCES += src/xfer/faccumulate.f
endif # ENABLE_F77
if MSG_COMMS_MPI
libarmci_la_SOURCES += src/common/groups.c
endif
if ARMCI_NETWORK_BGML
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/bgml
libarmci_la_SOURCES += src/devices/bgml/bgmlacc.h
libarmci_la_SOURCES += src/devices/bgml/bgml.c
libarmci_la_SOURCES += src/devices/bgml/bgmldefs.h
libarmci_la_SOURCES += src/devices/bgml/bgml.h
libarmci_la_SOURCES += src/devices/bgml/bgmltas-ppc.h
endif
if ARMCI_NETWORK_CRAY_SHMEM
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/cray-shmem
libarmci_la_SOURCES += src/memory/shmalloc.c
endif
if ARMCI_NETWORK_DCMF
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/dcmf
libarmci_la_SOURCES += src/devices/dcmf/armcix.h
if DCMF_VER_2
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_acc.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_fence.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_get.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_impl.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_lock.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_put.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_rmw.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf2/armcix_wait.c
endif
if DCMF_VER_0_2
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_acc.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_fence.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_get.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_impl.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_lock.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_put.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_rmw.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.2.0/armcix_wait.c
endif
if DCMF_VER_0_3
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_acc.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_fence.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_get.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_impl.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_lock.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_put.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_rmw.c
libarmci_la_SOURCES += src/devices/dcmf/dcmf-0.3.0/armcix_wait.c
endif
endif # ARMCI_NETWORK_DCMF
if ARMCI_NETWORK_LAPI
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/lapi
libarmci_la_SOURCES += src/common/async.c
libarmci_la_SOURCES += src/common/request.c
libarmci_la_SOURCES += src/devices/lapi/lapi.c
libarmci_la_SOURCES += src/devices/lapi/lapidefs.h
libarmci_la_SOURCES += src/memory/buffers.c
endif
if ARMCI_NETWORK_MPI_MT
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/mpi-mt
libarmci_la_SOURCES += src/common/ds-shared.c
libarmci_la_SOURCES += src/common/request.c
libarmci_la_SOURCES += src/common/spawn.c
libarmci_la_SOURCES += src/devices/mpi-mt/mpi2_client.c
libarmci_la_SOURCES += src/devices/mpi-mt/mpi2.h
libarmci_la_SOURCES += src/devices/mpi-mt/mpi2_server.c
libarmci_la_SOURCES += src/memory/buffers.c
endif
if ARMCI_NETWORK_MPI_SPAWN
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/mpi-spawn
libarmci_la_SOURCES += src/common/ds-shared.c
libarmci_la_SOURCES += src/common/request.c
libarmci_la_SOURCES += src/devices/mpi-spawn/mpi2_client.c
libarmci_la_SOURCES += src/devices/mpi-spawn/mpi2.h
libarmci_la_SOURCES += src/devices/mpi-spawn/mpi2_server.c
libarmci_la_SOURCES += src/memory/buffers.c
endif
if ARMCI_NETWORK_OPENIB
endif
if ARMCI_NETWORK_SOCKETS
AM_CPPFLAGS += -I$(top_srcdir)/src/devices/sockets
libarmci_la_SOURCES += src/common/async.c
libarmci_la_SOURCES += src/common/ds-shared.c
libarmci_la_SOURCES += src/common/request.c
libarmci_la_SOURCES += src/common/spawn.c
libarmci_la_SOURCES += src/devices/sockets/dataserv.c
libarmci_la_SOURCES += src/devices/sockets/sockets.c
libarmci_la_SOURCES += src/devices/sockets/sockets.h
libarmci_la_SOURCES += src/memory/buffers.c
endif
if ENABLE_CHECKPOINT
libarmci_la_SOURCES += src/ft/armci_chkpt.c
libarmci_la_SOURCES += src/ft/armci_storage.c
endif
if ARMCI_ENABLE_GPC_CALLS
libarmci_la_SOURCES += src/common/gpc.c
endif
if MACX
libarmci_la_SOURCES += src/common/signaltrap.c
libarmci_la_SOURCES += src/memory/shmalloc.c
libarmci_la_SOURCES += src/memory/winshmem.c
AM_CPPFLAGS += -DSHMEM -DMMAP
else
if CYGWIN
libarmci_la_SOURCES += src/common/signaltrap.c
libarmci_la_SOURCES += src/memory/shmalloc.c
libarmci_la_SOURCES += src/memory/winshmem.c
AM_CPPFLAGS += -DSHMEM -DMMAP
else
if SYSV
libarmci_la_SOURCES += src/common/signaltrap.c
libarmci_la_SOURCES += src/memory/shmem.c
libarmci_la_SOURCES += src/memory/shmlimit.c
endif
endif
endif
if NB_NONCONT
libarmci_la_SOURCES += src/common/noncont.c
endif
if THREAD_SAFE
libarmci_la_SOURCES += src/common/utils.c
libarmci_la_SOURCES += src/util/threads.c
endif
if INLINE_TAS_OKAY
libarmci_la_SOURCES += src/locks/tas.c
else
if INLINE_TAS_WITH
nodist_libarmci_la_SOURCES += src/locks/tas_as.s
endif
endif
if INLINE_X86COPY_NEEDED
if INLINE_X86COPY_OKAY
libarmci_la_SOURCES += src/common/x86copy.c
else
if INLINE_X86COPY_WITH
nodist_libarmci_la_SOURCES += src/common/x86copy_as.s
endif
endif
endif
if NEC
libarmci_la_SOURCES += src/locks/tas-sx.s

src/locks/tas-sx.o: src/locks/tas-sx.s
	$(AM_V_CCAS) \
	$(CCAS) $(AM_CCASFLAGS) $(CCASFLAGS) \
	-c -o src/locks/tas-sx.o $(top_srcdir)/src/locks/tas-sx.s

src/locks/tas-sx.lo: src/locks/tas-sx.s
	$(AM_V_CCAS) \
	$(LIBTOOL) $(AM_V_lt) --tag=CC $(AM_LIBTOOLFLAGS) \
	$(LIBTOOLFLAGS) --mode=compile $(CCAS) $(AM_CCASFLAGS) \
	$(CCASFLAGS) -c -o src/locks/tas-sx.lo $(top_srcdir)/src/locks/tas-sx.s
endif

include_HEADERS += src/include/armci.h
include_HEADERS += src/include/gpc.h
include_HEADERS += src/include/message.h

AM_CPPFLAGS += -I$(top_srcdir)/src/include

if ENABLE_SHARED
MAYBE_PIC = -fPIC -DPIC
endif

src/locks/tas_as.s: src/locks/tas.c
	$(ARMCI_TAS_AS_V) \
	$(ARMCI_TAS_AS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	$(AM_CPPFLAGS) $(CPPFLAGS) -S $(MAYBE_PIC) -o $@ \
	$(top_srcdir)/src/locks/tas.c

src/common/x86copy_as.s: src/common/x86copy.c
	$(ARMCI_X86COPY_AS_V) \
	$(ARMCI_X86COPY_AS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	$(AM_CPPFLAGS) $(CPPFLAGS) -S $(MAYBE_PIC) -o $@ \
	$(top_srcdir)/src/common/x86copy.c

CLEANFILES += src/common/x86copy_as.s
CLEANFILES += src/locks/tas_as.s
