libarmci_la_SOURCES += src-openib/armci.c
libarmci_la_SOURCES += src-openib/armci_impl.h
libarmci_la_SOURCES += src-openib/armci_openib.h
libarmci_la_SOURCES += src-openib/contig.c
libarmci_la_SOURCES += src-openib/device.c
libarmci_la_SOURCES += src-openib/device.h
libarmci_la_SOURCES += src-openib/fence.c
libarmci_la_SOURCES += src-openib/gpa.c
libarmci_la_SOURCES += src-openib/groups.c
libarmci_la_SOURCES += src-openib/groups.h
libarmci_la_SOURCES += src-openib/memlock.c
libarmci_la_SOURCES += src-openib/message.c
libarmci_la_SOURCES += src-openib/mutex.c
libarmci_la_SOURCES += src-openib/openib.c
libarmci_la_SOURCES += src-openib/openib.h
libarmci_la_SOURCES += src-openib/reg_cache.c
libarmci_la_SOURCES += src-openib/reg_cache.h
libarmci_la_SOURCES += src-openib/strided.c
libarmci_la_SOURCES += src-openib/topology.c
libarmci_la_SOURCES += src-openib/value_ops.c
libarmci_la_SOURCES += src-openib/vector.c
libarmci_la_SOURCES += src-openib/wait.c

AM_CPPFLAGS += -I$(top_srcdir)/src-openib

include_HEADERS += src-openib/armci.h
include_HEADERS += src-openib/message.h
