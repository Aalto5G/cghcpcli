CC := cc
#CC := clang

.SUFFIXES:

DIRHASHLIST := pptk/hashlist
LCHASHLIST := pptk/hashlist
MODULES += HASHLIST

DIRLOG := pptk/log
LCLOG := pptk/log
MODULES += LOG

DIRMISC := pptk/misc
LCMISC := pptk/misc
MODULES += MISC

DIRIPHDR := pptk/iphdr
LCIPHDR := pptk/iphdr
MODULES += IPHDR

DIRCLILIB := clilib
LCCLILIB := clilib
MODULES += CLILIB

DIRCGHCPPRELOAD := cghcppreload
LCCGHCPPRELOAD := cghcppreload
MODULES += CGHCPPRELOAD

DIRCGHCPPROXYCMD := cghcpproxycmd
LCCGHCPPROXYCMD := cghcpproxycmd
MODULES += CGHCPPROXYCMD

CFLAGS := -g -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -std=gnu11 -fPIC

.PHONY: all clean distclean unit

all: $(MODULES)
clean: $(patsubst %,clean_%,$(MODULES))
distclean: $(patsubst %,distclean_%,$(MODULES))
unit: $(patsubst %,unit_%,$(MODULES))

MAKEFILES_COMMON := Makefile opts.mk

WITH_NETMAP=no
WITH_WERROR=no
NETMAP_INCDIR=
WITH_ODP=no
ODP_DIR=/usr/local
LIBS_ODPDEP=/usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a
include opts.mk

ifeq ($(WITH_WERROR),yes)
CFLAGS := $(CFLAGS) -Werror
endif

$(foreach module,$(MODULES),$(eval \
    include $(DIR$(module))/module.mk))
$(foreach module,$(INCLUDES),$(eval \
    include $(DIR$(module))/module.mk))

opts.mk:
	touch opts.mk
