all: tardis novdso.so

tardis: tardis.c
	gcc tardis.c -o tardis -lm -Wall -Ofast -std=c99 -DPID_MAX=$(shell cat /proc/sys/kernel/pid_max)

novdso.so: novdso.c
	gcc -std=c99 -Wall -fPIC -shared -o novdso.so novdso.c

clean:
	rm -f tardis novdso.so
