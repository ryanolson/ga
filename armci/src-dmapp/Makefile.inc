libarmci_la_SOURCES += src-dmapp/armci.c
libarmci_la_SOURCES += src-dmapp/armci_impl.h
libarmci_la_SOURCES += src-dmapp/clusterinfo.c
libarmci_la_SOURCES += src-dmapp/cray_memcpy_intel.s
libarmci_la_SOURCES += src-dmapp/cray_memcpy_amd.s
libarmci_la_SOURCES += src-dmapp/groups.c
libarmci_la_SOURCES += src-dmapp/groups.h
libarmci_la_SOURCES += src-dmapp/message.c
libarmci_la_SOURCES += src-dmapp/reg_cache.c
libarmci_la_SOURCES += src-dmapp/reg_cache.h

AM_CPPFLAGS += -I$(top_srcdir)/src-dmapp

include_HEADERS += src-dmapp/armci.h
include_HEADERS += src-dmapp/message.h
