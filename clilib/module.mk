CLILIB_SRC_LIB := clilib.c
CLILIB_SRC := $(CLILIB_SRC_LIB) clilibtest.c

CLILIB_SRC_LIB := $(patsubst %,$(DIRCLILIB)/%,$(CLILIB_SRC_LIB))
CLILIB_SRC := $(patsubst %,$(DIRCLILIB)/%,$(CLILIB_SRC))

CLILIB_OBJ_LIB := $(patsubst %.c,%.o,$(CLILIB_SRC_LIB))
CLILIB_OBJ := $(patsubst %.c,%.o,$(CLILIB_SRC))

CLILIB_DEP_LIB := $(patsubst %.c,%.d,$(CLILIB_SRC_LIB))
CLILIB_DEP := $(patsubst %.c,%.d,$(CLILIB_SRC))

CFLAGS_CLILIB := -I$(DIRIPHDR) -I$(DIRMISC)
LIBS_CLILIB := $(DIRIPHDR)/libiphdr.a

MAKEFILES_CLILIB := $(DIRCLILIB)/module.mk

.PHONY: CLILIB clean_CLILIB distclean_CLILIB unit_CLILIB $(LCCLILIB) clean_$(LCCLILIB) distclean_$(LCCLILIB) unit_$(LCCLILIB)

$(LCCLILIB): CLILIB
clean_$(LCCLILIB): clean_CLILIB
distclean_$(LCCLILIB): distclean_CLILIB
unit_$(LCCLILIB): unit_CLILIB

CLILIB: $(DIRCLILIB)/libclilib.a $(DIRCLILIB)/clilibtest

unit_CLILIB:
	@true

$(DIRCLILIB)/libclilib.a: $(CLILIB_OBJ_LIB) $(MAKEFILES_COMMON) $(MAKEFILES_CLILIB)
	rm -f $@
	ar rvs $@ $(filter %.o,$^)

$(DIRCLILIB)/clilibtest: $(DIRCLILIB)/clilibtest.o $(DIRCLILIB)/libclilib.a $(LIBS_CLILIB) $(MAKEFILES_COMMON) $(MAKEFILES_CLILIB)
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^) $(filter %.a,$^) $(CFLAGS_CLILIB)

$(CLILIB_OBJ): %.o: %.c %.d $(MAKEFILES_COMMON) $(MAKEFILES_CLILIB)
	$(CC) $(CFLAGS) -c -o $*.o $*.c $(CFLAGS_CLILIB)
	$(CC) $(CFLAGS) -c -S -o $*.s $*.c $(CFLAGS_CLILIB)

$(CLILIB_DEP): %.d: %.c $(MAKEFILES_COMMON) $(MAKEFILES_CLILIB)
	$(CC) $(CFLAGS) -MM -MP -MT "$*.d $*.o" -o $*.d $*.c $(CFLAGS_CLILIB)

clean_CLILIB:
	rm -f $(CLILIB_OBJ) $(CLILIB_DEP)

distclean_CLILIB: clean_CLILIB
	rm -f $(DIRCLILIB)/libclilib.a $(DIRCLILIB)/clilibtest

-include $(DIRCLILIB)/*.d
