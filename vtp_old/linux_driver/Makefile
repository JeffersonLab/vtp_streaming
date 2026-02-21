obj-m	:= vtp.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

all: modules

install: modules_install udev_install

modules:
	@echo
	@echo "Building VTP kernel module"
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

modules_install: modules
	@echo
	@echo "Installing VTP kernel module"
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	@rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

