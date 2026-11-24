/*
 * syscalls.c - Stub newlib toi gian (firmware khong dung file/stdio).
 * _write chuyen ra UART de printf (neu sau nay dung) van in duoc log.
 */
#include <errno.h>
#include <sys/stat.h>
#include "hal.h"

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
    return len;
}

int _read(int fd, char *buf, int len)   { (void)fd; (void)buf; (void)len; errno = ENOSYS; return -1; }
int _close(int fd)                      { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st)     { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                     { (void)fd; return 1; }
