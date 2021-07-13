#include "common.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void __attribute__((noreturn)) vdie(const char *fmt, va_list va) {
  if (isatty(stdout->_fileno)) {
    fputs("\x1b[1m\x1b[38;5;1merror: \x1b[m", stderr);
  } else {
    fputs("error: ", stderr);
  }
  vfprintf(stderr, fmt, va);
  fputc('\n', stderr);
  // end vararg list here as the caller won't be able
  // to clean it up.
  va_end(va);
  exit(1);
}

void __attribute__((noreturn)) __attribute__((format(printf, 1, 2)))
die(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vdie(fmt, va);
}
void __attribute__((format(printf, 2, 3))) assert(int e, const char *msg, ...) {
  if (!e) {
    // 8K buffer. I hope your message is not THAT long.
    char buff[BUFSIZ];
    buff[0] = 0;

    strncpy(buff, "assertion error: ", BUFSIZ);
    strncat(buff, msg, BUFSIZ);

    va_list va;
    va_start(va, msg);
    vdie(msg, va); 

  }
}
