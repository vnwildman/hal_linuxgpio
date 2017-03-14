obj-m += hal_linuxgpio.o
include /usr/share/linuxcnc/Makefile.modinc

lean:
	rm -v *.o *.so
