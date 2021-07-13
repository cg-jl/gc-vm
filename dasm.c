// dissasembler
#include "common.h"
#include "instruction.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FORCE_INLINE __attribute__((always_inline))

static void FORCE_INLINE iname(const char *msg) {
  printf("\x1b[38;5;4m%s\x1b[m", msg);
}

static void FORCE_INLINE inum(i32 value) {
  printf("\x1b[38;5;5m%d\x1b[m", value);
}

static void FORCE_INLINE escape(char v) {
  printf("\x1b[38;5;5m'\x1b[38;5;3m\\%c\x1b[38;5;5m'\x1b[m", v);
}
static void FORCE_INLINE ch(char v) { printf("\x1b[38;5;5m'%c'\x1b[m", v); }

static void FORCE_INLINE i_possible_char(i32 value) {

  switch (value) {
  default:
    if (isprint(value))
      ch(value);
    else
      inum(value);
    break;
  case '\n':
    escape('n');
  }
}

static void FORCE_INLINE istr(const char *v) {
  printf("\x1b[38;5;2m\"%s\"\x1b[m", v);
}

void printInstruction(const Instruction *i) {
  iname(inames[i->type]);
  if (i->type == I_ASSERT) {
    putchar(' ');
    inum(i->assert.expected);
    putchar(' ');
    istr(i->assert.msg);
  } else if (i->type == I_PSH_I32) {
    putchar(' ');
    i_possible_char(i->push.value);
  } else if (i->type == I_DIE) {
    putchar(' ');
    istr(i->die.errmsg);
  }
  putchar('\n');
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file>\n", *argv);
    return 1;
  }

  FILE *in = fopen(argv[1], "rb");
  if (in == NULL) {
    die("Couldn't open `%s`: %s", argv[1], strerror(errno));
  }

  for (Instruction *i = fetchInstruction(in); i != NULL;
       freeInstruction(i), i = fetchInstruction(in)) {
    printInstruction(i);
  }

  fclose(in);

  return 0;
}
