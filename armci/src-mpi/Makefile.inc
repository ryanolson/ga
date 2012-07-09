libarmci_la_SOURCES += src-mpi/armci.c
libarmci_la_SOURCES += src-mpi/armci_impl.h
libarmci_la_SOURCES += src-mpi/groups.c
libarmci_la_SOURCES += src-mpi/groups.h
libarmci_la_SOURCES += src-mpi/message.c

AM_CPPFLAGS += -I$(top_srcdir)/src-mpi

include_HEADERS += src-mpi/armci.h
include_HEADERS += src-mpi/message.h
