libarmci_la_SOURCES += src-gemini/armci.c
libarmci_la_SOURCES += src-gemini/armci_impl.h
libarmci_la_SOURCES += src-gemini/clusterinfo.c
libarmci_la_SOURCES += src-gemini/cray_memcpy_intel.s
libarmci_la_SOURCES += src-gemini/cray_memcpy_amd.s
libarmci_la_SOURCES += src-gemini/groups.c
libarmci_la_SOURCES += src-gemini/groups.h
libarmci_la_SOURCES += src-gemini/message.c
libarmci_la_SOURCES += src-gemini/reg_cache.c
libarmci_la_SOURCES += src-gemini/reg_cache.h

AM_CPPFLAGS += -I$(top_srcdir)/src-gemini

include_HEADERS += src-gemini/armci.h
include_HEADERS += src-gemini/message.h
