#
# File:
#    Makefile
#
# Description:
#    Makefile for the JLab VTP module running Linux on an ARMv7 processor
#
# CLAS stuff
#MAIN = vtp
#include $(CODA)/src/Makefile.include
#
#
#

USE_IPC  ?= 0
USE_CODA ?= 0

# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG   ?= 1
QUIET	?= 1
#
#
BASENAME=vtp
ARCH=${shell uname -m}
KERNEL_VERSION=${shell uname -r}

CC			= gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -DLinux_$(ARCH) -L.
INCS			= -I. -I/usr/local/include


LIBS			= lib${BASENAME}.a
SOLIBS			= lib${BASENAME}.so

ifeq ($(USE_CODA),1)
	LIBNAMES        = $(CODA)/src/codautil/Linux_armv7l/lib/libcodautil.a
	INCS		+= -I$(CODA)/src/codautil/codautil.s
endif
ifeq ($(USE_IPC),1)
	LIBNAMES        = $(CODA)/src/ipc/Linux_armv7l/lib/libipc.a
	INCS		+= -I$(CODA)/src/ipc/ipc.s
	CFLAGS		+= -DIPC
endif

ifdef DEBUG
	CFLAGS		+= -Wall -g
else
	CFLAGS		+= -O2
endif

SRC			= vtpLib.c vtpConfig.c vtp-i2c.c vtp-spi.c \
				 si5341_cfg.c vtp-ltm.c
HDRS			= $(SRC:.c=.h)
OBJ			= $(SRC:.c=.o)
DEPS			= $(SRC:.c=.d) vtpserver.d

ifeq ($(QUIET),1)
	Q = @
else
	Q =
endif


all: echoarch $(SOLIBS) vtpserver

%.o: %.c
	@echo " CC     $@"
	$(Q)$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(SOLIBS): $(OBJ)
	@echo " CC     $@"
	$(Q)$(CC) -fpic -shared $(CFLAGS) $(LIBNAMES) $(INCS) -o $@ $(SRC)
	@echo " AR     $(LIBS)"
	$(Q)$(AR) r $(LIBS) $(OBJ)
	@echo " RANLIB $(LIBS)"
	$(Q)$(RANLIB) $(LIBS)

%.d: %.c
	@echo " DEP    $@"
	$(Q)set -e; rm -f $@; \
	$(CC) -MM -shared $(INCS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

vtpserver: vtpserver.c $(SOLIBS)
	@echo " CC     $@"
	$(Q)$(CC) $(CFLAGS) -lvtp -lrt -lm -li2c $(LIBNAMES) $(INCS) -o $@ $<


-include $(DEPS)

clean:
	$(Q)rm -vf ${OBJ} ${LIBS} $(LIBS:.a=.so) ${DEPS} ${DEPS}.* vtpserver

realclean: clean
	$(Q)rm -vf *~

install: echoarch $(LIBS) vtpserver
	@echo " INST   ${LIBS} $(LIBS:.a=.so) ${HDRS}"
	-$(Q)cp *.a $(CODA)/Linux-armv7l/lib/
	-$(Q)cp *.so $(CODA)/Linux-armv7l/lib/
	-$(Q)cp *.h $(CODA)/Linux-armv7l/include/
	-$(Q)cp *.h $(CODA)/linuxvme/include/
	-$(Q)cp vtpserver $(CODA)/Linux-armv7l/bin/

echoarch:
	@echo "Make for $(ARCH)"

.PHONY: clean echoarch
