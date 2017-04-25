all: tardis novdso.so

tardis: tardis.c
	gcc tardis.c -o tardis -lm -Wall -Ofast -std=c99

novdso.so: novdso.c
	gcc -std=c99 -Wall -fPIC -shared -o novdso.so novdso.c
