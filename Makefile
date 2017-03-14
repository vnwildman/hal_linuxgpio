obj-m += hal_linuxgpio.o
include /usr/share/linuxcnc/Makefile.modinc

install:
	install --verbose -m 644 hal_linuxgpio.so /usr/lib/linuxcnc/rt-preempt/

clean:
	rm -v *.o *.so *.tmp *.ver *.exported
