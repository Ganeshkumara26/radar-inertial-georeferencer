/*
 * syscalls.c — Minimal bare-metal syscall stubs for newlib-nano
 * Target: STM32H753XI Cortex-M7
 *
 * Provides __errno for libm, and stubs for unused newlib functions.
 */

/* errno is used by libm (sqrt, sqrtf, math_errf) */
int *__errno(void) {
    static int _errno = 0;
    return &_errno;
}

/* Stub out unused newlib hooks */
void _exit(int status) { (void)status; while(1) {} }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }
