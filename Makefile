obj-m += ez.o

all: kmod format_file_storage

format_file_storage: CC = gcc
format_file_storage: CFLAGS = -g -Wall

PHONY += kmod
kmod:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

PHONY += clean
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f format_disk_as_ezfs

.PHONY: $(PHONY)
