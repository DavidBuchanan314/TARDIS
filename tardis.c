#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <wait.h>
#include <time.h>
#include <sys/ptrace.h>
#include <elf.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <math.h>
#include <stdbool.h>

#define MICROSECONDS 1000000
#define NANOSECONDS (MICROSECONDS*1000)
#define NUM_SYSCALLS 512 // this is higher than the real number, but shouldn't matter
#ifndef PID_MAX
#define PID_MAX 32768 // XXX: assumption
#endif
#define NUM_CLKIDS 16 // XXX: incorrect, but "works" anyway

double starttimes[NUM_CLKIDS], delayfactor, timefactor;
bool leavesys[PID_MAX];
void (*before_handlers[NUM_SYSCALLS])(pid_t, struct user_regs_struct *);
void (*after_handlers[NUM_SYSCALLS])(pid_t, struct user_regs_struct *);

int is64bit(pid_t pid) {
	struct user_regs_struct x64regs;
	struct iovec iov = {
		.iov_base = &x64regs,
		.iov_len = sizeof(x64regs)
	};
	
	ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov);
	return iov.iov_len == sizeof(x64regs);
}

void read_block(pid_t pid, void * dst, void * src, size_t len) {
	for (size_t i = 0; i < len; i += sizeof(void *)) {
		*(void **)(dst + i) = (void *)ptrace(PTRACE_PEEKDATA, pid, src + i, NULL); // XXX
	}
}

void write_block(pid_t pid, void * src, void * dst, size_t len) {
	for (size_t i = 0; i < len; i += sizeof(void *)) {
		ptrace(PTRACE_POKEDATA, pid, dst + i, *(void **)(src + i)); // XXX
	}
}

void scale_timespec(struct timespec * ts, double factor, double starttime) {
	double time = ts->tv_sec + (double)ts->tv_nsec / NANOSECONDS;
	if (starttime != 0) {
		time = starttime + (time - starttime) * factor;
	} else {
		time *= factor;
	}
	ts->tv_sec = time;
	ts->tv_nsec = fmod(time, 1) * NANOSECONDS;
}

void scale_timeval(struct timeval * tv, double factor, double starttime) {
	double time = tv->tv_sec + (double)tv->tv_usec / MICROSECONDS;
	if (starttime != 0) {
		time = starttime + (time - starttime) * factor;
	} else {
		time *= factor;
	}
	tv->tv_sec = time;
	tv->tv_usec = fmod(time, 1) * MICROSECONDS;
}

/* pre-syscall handlers */

void before_nanosleep(pid_t pid, struct user_regs_struct * uregs) {
	struct timespec ts;
	read_block(pid, &ts, (void *)uregs->rdi, sizeof(struct timespec));
	scale_timespec(&ts, 1.0/delayfactor, 0);
	write_block(pid, &ts, (void *)uregs->rdi, sizeof(struct timespec));
}

void before_poll(pid_t pid, struct user_regs_struct * uregs) {
	int timeout = uregs->rdx & 0xFFFFFFFF; // isolate edx
	if (timeout > 0) {
		uregs->rdx = timeout / delayfactor; // not sure if this behaves the way I want
	}
	ptrace(PTRACE_SETREGS, pid, 0, uregs);
}

void before_select(pid_t pid, struct user_regs_struct * uregs) {
	if (uregs->r8 != 0) {
		struct timeval tv;
		read_block(pid, &tv, (void *)uregs->r8, sizeof(struct timeval));
		scale_timeval(&tv, 1.0/delayfactor, 0);
		write_block(pid, &tv, (void *)uregs->r8, sizeof(struct timeval));
	}
}

void before_clock_nanosleep(pid_t pid, struct user_regs_struct * uregs) {
	struct timespec rqtp;
	read_block(pid, &rqtp, (void *)uregs->rdx, sizeof(struct timespec));
	scale_timespec(&rqtp, 1.0/delayfactor, 0);
	write_block(pid, &rqtp, (void *)uregs->rdx, sizeof(struct timespec));
}

/* post-syscall handlers */

void after_gettimeofday(pid_t pid, struct user_regs_struct * uregs) {
	struct timeval tv;
	read_block(pid, &tv, (void *)uregs->rdi, sizeof(struct timeval));
	scale_timeval(&tv, timefactor, starttimes[CLOCK_REALTIME]);
	write_block(pid, &tv, (void *)uregs->rdi, sizeof(struct timeval));
}

void after_clock_gettime(pid_t pid, struct user_regs_struct * uregs) {
	struct timespec ts;
	read_block(pid, &ts, (void *)uregs->rsi, sizeof(struct timespec));
	scale_timespec(&ts, timefactor, starttimes[uregs->rdi]); // FIXME check bounds
	write_block(pid, &ts, (void *)uregs->rsi, sizeof(struct timespec));
}

void after_time(pid_t pid, struct user_regs_struct * uregs) {
	uregs->rdi = starttimes[CLOCK_REALTIME] + (uregs->rdi - starttimes[CLOCK_REALTIME]) * timefactor;
	ptrace(PTRACE_SETREGS, pid, 0, uregs);
}

void after_clock_nanosleep(pid_t pid, struct user_regs_struct * uregs) {
	struct timespec rmtp;
	read_block(pid, &rmtp, (void *)uregs->rcx, sizeof(struct timespec));
	scale_timespec(&rmtp, 1.0/delayfactor, 0);
	write_block(pid, &rmtp, (void *)uregs->rcx, sizeof(struct timespec));
}

int main(int argc, char *argv[], char *envp[]) {
	
	if (argc < 3) {
		printf("USAGE: %s DELAY_FACTOR TIME_FACTOR COMMAND [ARGS]...\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	delayfactor = strtod(argv[1], NULL);
	timefactor = strtod(argv[2], NULL);
	
	before_handlers[SYS_nanosleep] = before_nanosleep;
	before_handlers[SYS_poll] = before_poll;
	before_handlers[SYS_select] = before_select;
	before_handlers[SYS_clock_nanosleep] = before_clock_nanosleep;
	
	after_handlers[SYS_gettimeofday] = after_gettimeofday;
	after_handlers[SYS_clock_gettime] = after_clock_gettime;
	after_handlers[SYS_time] = after_time;
	after_handlers[SYS_clock_nanosleep] = after_clock_nanosleep;
	
	struct timespec sts;
	for (clockid_t id = 0; id < NUM_CLKIDS; id++) {
		clock_gettime(id, &sts); // sometimes id will be invalid, but it shouldn't matter
		starttimes[id] = sts.tv_sec + (double)sts.tv_nsec / NANOSECONDS;
	}
	
	pid_t child = fork();
	
	if(child == 0) {
		/* child */
		envp[0] = "LD_PRELOAD=./novdso.so"; // FIXME: Do something more sensible
		kill(getpid(), SIGSTOP);
		execvpe(argv[3], &argv[3], envp);
		perror("execvpe"); // execvpe only returns on error
		exit(-1);
	}
	#ifdef DEBUG
	    fprintf(stderr, "Child spawned with PID %d\n", child);
	#endif
	ptrace(PTRACE_SEIZE, child, 0, PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE);
	wait(NULL); // wait for SIGSTOP to happen
	ptrace(PTRACE_SYSCALL, child, 0, 0); // continue execution
	
	if (is64bit(child)) {
		#ifdef DEBUG
		    fprintf(stderr, "Child is 64-bit\n");
		#endif
	} else {
		fprintf(stderr, "ERROR: 32-bit processes are currently unsupported\n");
		exit(-1);
	}
	
	for (;;) { // TODO: Understand ptrace, simplify code structure
		struct user_regs_struct uregs;
		int status;
		pid_t pid = waitpid(-1, &status, 0);
		if (WIFEXITED(status)) {
			if (pid == child) {
				exit(WEXITSTATUS(status));
			} else {
				continue;
			}
		}
		if (WIFSTOPPED(status) && WSTOPSIG(status) != SIGTRAP && WSTOPSIG(status) != SIGSTOP) {
			if (WSTOPSIG(status) & 0x80) {
				// handle syscall
			} else {
				ptrace(PTRACE_SYSCALL, pid, 0, WSTOPSIG(status));
				continue;
			}
		} else {
			ptrace(PTRACE_SYSCALL, pid, 0, 0);
			continue;
		}
		
		ptrace(PTRACE_GETREGS, pid, 0, &uregs);
		if (!leavesys[pid]) {
			#ifdef DEBUG
				fprintf(stderr, "[pid %d]   syscall(%llu)\t0x%016llX 0x%016llX 0x%016llX = ...\n", pid, uregs.orig_rax, uregs.rdi, uregs.rsi, uregs.rdx);
			#endif
			if (uregs.orig_rax < NUM_SYSCALLS && before_handlers[uregs.orig_rax] != NULL) {
				before_handlers[uregs.orig_rax](pid, &uregs);
			}
		} else {
			#ifdef DEBUG
				fprintf(stderr, "... 0x%llX\n", uregs.rax);
			#endif
			if (uregs.orig_rax < NUM_SYSCALLS && after_handlers[uregs.orig_rax] != NULL) {
				after_handlers[uregs.orig_rax](pid, &uregs);
			}
		}
		leavesys[pid] ^= true;
		ptrace(PTRACE_SYSCALL, pid, 0, 0);
	}
}
