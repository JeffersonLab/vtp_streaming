#
# File:
#    Makefile
#
# Description:
#    Makefile for the VTP test programs
#
#
# $Date$
# $Rev$
#

USE_IPC=0
USE_ACTIVEMQ=0
USE_CODA=0

VTPLIB_PATH		= ../
ARCH			= ${shell uname -m}
KERNEL_VERSION		= ${shell uname -r}

CC				= gcc
AR				= ar
RANLIB		= ranlib
INCS			= -I. -I${VTPLIB_PATH} -I/usr/local/include -I$(CODA)/Linux_armv7l/include
CFLAGS		= -Wall -g ${INCS} \
            -L. -L${VTPLIB_PATH} -lrt -li2c -lm -DLinux_armv7l

#LIBNAMES	= $(CODA)/src/mysql4.1.20/$(OSTYPE_MACHINE)/lib/libmysql4.1.20.a
ifeq ($(USE_CODA),1)
	LIBNAMES  = $(CODA)/src/codautil/$(OSTYPE_MACHINE)/lib/libcodautil.a
endif
ifeq ($(USE_ACTIVEMQ),1)
	LIBNAMES += /usr/local/lib/libactivemq-cpp.so
endif
LIBNAMES += -lapr-1
LIBNAMES += -lcrypt -lssl -lcrypto -ldl -lz
LIBNAMES += -L/usr/kerberos/lib -lpthread -pthread -Wl,-rpath -l/usr/local/lib

#LIBNAMES	= $(CODA)/src/codautil/Linux_armv7l/lib/libcodautil.a
ifeq ($(USE_IPC),1)
	LIBNAMES	+= $(CODA)/src/ipc/Linux_armv7l/lib/libipc.a
endif
#LIBNAMES	+= /usr/local/lib/libactivemq-cpp.so
LIBNAMES	+= -lvtp

PROGS			= vtpLibTest i2cvtpmon vtpSPItest vtpI2Ctest vtpDmaTest i2cvtpsetup vtpConfigTest vtpStatus
SRC			= $(PROGS:%=%.c)
DEPS			= $(SRC:%.c=%.d)

all: $(PROGS)

clean distclean:
	@rm -f $(PROGS) *~

%: %.c
	@echo "Making $@"
	$(CC) $(CFLAGS) ${LIBNAMES} -o $@ $(@:%=%.c)

%.d: %.c
	@echo ""
	@echo "Building $@ from $<"
	@set -e; rm -f $@; \
	$(CC) -MM -shared ${CFLAGS} $(INCS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1 $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

.PHONY: all clean distclean
