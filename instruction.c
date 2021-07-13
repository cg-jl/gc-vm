#define _GNU_SOURCE // for `reallocarray` extension
#include "instruction.h"
#include "common.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *inames[] = {
    [I_ASSERT] = "assert_allocated",
    [I_GC] = "gc",
    [I_PAIR] = "pair",
    [I_POP] = "pop",
    [I_PRINT] = "out",
    [I_PSH_I32] = "push",
    [I_READ_I32] = "in",
    [I_SWP] = "swap",
    [I_HALT] = "halt",
    [I_DIE] = "die",
};

void freeInstruction(Instruction *inst) {
  assert(inst != NULL, "attempting to free null pointer");
  if (inst->type == I_ASSERT)
    free((void *)inst->assert.msg);
  else if (inst->type == I_DIE)
    free((void *)inst->die.errmsg);
  free(inst);
}

char *read_raw_str(FILE *in) {
  u8 b = 0;
  char *buf = calloc(4, sizeof(char));
  usize cap = 4;
  usize len = 0;

  for (; fread(&b, 1, 1, in) == 1 && b != 0;) {
    if (len == cap) {
      cap *= 2;
      buf = reallocarray(buf, cap, sizeof(char));
    }
    buf[len] = b;
    len++;
  }

  if (errno) {
    die("while reading fron input: %s", strerror(errno));
  }

  if (len == cap) {
    cap++;
    buf = reallocarray(buf, cap, sizeof(char));
  }
  buf[len] = 0;

  assert(b == 0, "Expected string to end in zero byte");

  return buf;
}

Instruction *fetchInstruction(FILE *fp) {

  u8 first;
  if (fread(&first, 1, 1, fp) != 1) {
    assert(errno == 0, strerror(errno));
    return NULL;
  }

  // don't allocate till we know we read something.
  Instruction *i = malloc(sizeof(*i));

  switch (first) {
  default:
    die("Not a known instruction code: 0x%x\n", first);
    exit(1);

  // nothing to do.
  case I_PAIR:
  case I_READ_I32:
  case I_HALT:
  case I_SWP:
  case I_GC:
  case I_POP:
  case I_PRINT:
    break;

  case I_DIE:
    i->die.errmsg = read_raw_str(fp);
    break;

  case I_PSH_I32:
    assert(fread(&i->push.value, 4, 1, fp) == 1, "push: expected constant");
    break;
  case I_ASSERT: {
    assert(fread(&i->assert.expected, 4, 1, fp) == 1,
           "assert: expected constant");

    i->assert.msg = read_raw_str(fp);

  } break;
  }

  i->type = (IType)first;

  return i;
}
