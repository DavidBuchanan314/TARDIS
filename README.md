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

![xclock demo](https://i.imgur.com/UnFYuLs.gif)

## Notes:

- Currently only x86_64 Linux is supported. It should be possible to port to i386 with fairly minimal effort.

- I used `PTRACE_SEIZE`, which only exists since kernel version 3.4.

- `novdso.so` is preloaded to prevent libc from using vDSO - otherwise `ptrace(PTRACE_SYSCALL, ...)`
wouldn't work for those syscalls (Take a look at `man vdso` for more information). You might need to
modify the `LD_PRELOAD` value to be an absolute path for some programs/environments, I only made it
relative for simplicity.

- Certain simple programs, like `glxgears`, don't mind being run with time flowing in reverse! Most programs don't however, and of course there's no way to have a negative delay.

- There are many more syscalls that I still need to handle.

Currently handled syscalls:

- `nanosleep`
- `clock_nanosleep`
- `select`
- `poll`
- `gettimeofday`
- `clock_gettime`
- `time`
