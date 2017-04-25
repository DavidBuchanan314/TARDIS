# TARDIS
Trace And Rewrite Delays In Syscalls: Hooking time-related Linux syscalls to warp a process's perspective of time.

This code is rather buggy, mainly due to my lack of understanding of the ptrace API.
You probably shouldn't use it for anything serious, although it could be useful for
testing/debugging certain applications.

## Things to try:

```
$ ./tardis 10000 10000 xclock
$ ./tardis 1 3 glxgears
$ ./tardis 1 -1 glxgears
$ ./tardis 10 10 firefox
$ ./tardis 10 10 /bin/sh
```
