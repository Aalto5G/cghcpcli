CC := cc
#CC := clang

.SUFFIXES:

DIRCLILIB := clilib
LCCLILIB := clilib
MODULES += CLILIB

DIRCGHCPPRELOAD := cghcppreload
LCCGHCPPRELOAD := cghcppreload
MODULES += CGHCPPRELOAD

CFLAGS := -g -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Werror -std=gnu11 -fPIC

.PHONY: all clean distclean unit

all: $(MODULES)
clean: $(patsubst %,clean_%,$(MODULES))
distclean: $(patsubst %,distclean_%,$(MODULES))
unit: $(patsubst %,unit_%,$(MODULES))

MAKEFILES_COMMON := Makefile opts.mk

WITH_NETMAP=no
NETMAP_INCDIR=
WITH_ODP=no
ODP_DIR=/usr/local
LIBS_ODPDEP=/usr/lib/x86_64-linux-gnu/libssl.a /usr/lib/x86_64-linux-gnu/libcrypto.a
include opts.mk

$(foreach module,$(MODULES),$(eval \
    include $(DIR$(module))/module.mk))
$(foreach module,$(INCLUDES),$(eval \
    include $(DIR$(module))/module.mk))

opts.mk:
	touch opts.mk
