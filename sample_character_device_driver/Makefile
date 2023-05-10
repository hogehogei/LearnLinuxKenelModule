
# kbuild part of makefile
obj-m := sample_character_device_driver.o
#the following is just an example
#ldflags-y := -T foo_sections.lds
# normal makefile
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(shell pwd) $@
