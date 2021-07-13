#ifndef __INSTRUCTION_H__
#define __INSTRUCTION_H__

#include "common.h"

// instructions (very simple):
// 0x00 -> print current from the stack.
// 0x01 -> read character (i32) and push it.
// 0x02 +4byte int               -> push i32 (constant)
// 0x03                          -> pop two & push pair
// 0x04                          -> swap the two
// 0x05                          -> pop
// 0x10                          -> call GC
// 0x12 +4byte int +"string\0"   -> assert the number of allocated objects is
// <int>, otherwise fail with <string>

typedef enum {
  I_PRINT = 0x00,
  I_READ_I32 = 0x01,
  I_PSH_I32 = 0x02,
  I_PAIR = 0x03,
  I_SWP = 0x04,
  I_POP = 0x05,
  I_HALT = 0x06,
  I_DIE = 0x07, // prints `errmsg` to stderr and dies.
  I_GC = 0x10,
  I_ASSERT = 0x12,
} IType;

typedef struct {
  IType type;

  union {
    struct {
      const char *errmsg;
    } die;
    struct {
      const char *msg;
      i32 expected;
    } assert;
    struct {
      i32 value;
    } push;
  };
} Instruction;

struct _IO_FILE;
typedef struct _IO_FILE FILE;

// fetch an instruction from <in>. the instruction is allocated
// and the receiver should free it.
Instruction *fetchInstruction(FILE *in);

void freeInstruction(Instruction *inst);
extern const char *inames[];

#endif // !__INSTRUCTION_H__
