#define _GNU_SOURCE
#include "common.h"
#include "instruction.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { OBJ_INT, OBJ_PAIR } ObjType;

typedef struct _object {
  ObjType type;
  bool is_marked; // useful for GC
  // IMO not needing this.
  // GC can collect from its stack if it marks the pointers
  // correctly (using NULLs when popping).
  // with this, we're maintaining double stack. This, although
  // not really a problem (just 8 bytes) is not necessary.
  struct _object *next;

  union {
    /* OBJ_INT */
    i32 value;

    /* OBJ_PAIR */
    struct {
      struct _object *head;
      struct _object *tail;
    };
  };
} Object;

// VM
#define STACK_MAX 256
#define INITIAL_GC_THRESHOLD 100

typedef struct {
  Object *stack[STACK_MAX];
  Object *first;
  i32 stack_size;
  i32 num_objects;
  i32 max_objects;
  bool has_halted;
} VM;

VM *newVM() {
  VM *vm = malloc(sizeof(*vm));
  memset(vm, 0, sizeof(*vm));
  vm->max_objects = INITIAL_GC_THRESHOLD;
  return vm;
}

void push(VM *vm, Object *value) {
  assert(vm->stack_size < STACK_MAX, "Stack overflow");
  vm->stack[vm->stack_size++] = value;
}

Object *pop(VM *vm) {
  assert(vm->stack_size > 0, "Stack underflow");
  return vm->stack[--vm->stack_size];
}

void gc(VM *);

Object *newObject(VM *vm, ObjType type) {
  if (vm->num_objects == vm->max_objects)
    gc(vm);

  Object *object = malloc(sizeof(*object));
  object->next = NULL;
  object->type = type;
  object->is_marked = false;

  // prepend new object
  object->next = vm->first;
  vm->first = object;
  vm->num_objects++;

  return object;
}

// Push a single integer value.
void pushInt(VM *vm, i32 intValue) {
  Object *object = newObject(vm, OBJ_INT);
  object->value = intValue;
  push(vm, object);
}

/// Pop last two values and put them in a pair.
Object *pushPair(VM *vm) {
  Object *obj = newObject(vm, OBJ_PAIR);
  obj->tail = pop(vm);
  obj->head = pop(vm);

  push(vm, obj);
  return obj;
}

void mark(Object *obj) {
  if (obj->is_marked)
    return;
  obj->is_marked = true;
  if (obj->type == OBJ_PAIR) {
    mark(obj->head);
    mark(obj->tail);
  }
}

// mark all reachable objects.
void markAll(VM *vm) {
  for (usize i = 0; i < vm->stack_size; i++) {
    mark(vm->stack[i]);
  }
}

void sweep(VM *vm) {
  Object **object = &vm->first;
  while (*object) {
    if (!(*object)->is_marked) {
      /* This object wasn't reached, so remove it from the list and free it. */
      Object *unreached = *object;

      *object = unreached->next;

      free(unreached);

      vm->num_objects--;
    } else {
      /* This object was reached, so unmark it (for the next GC) and move on to
       the next. */
      (*object)->is_marked = 0;
      object = &(*object)->next;
    }
  }
}

void gc(VM *vm) {
  markAll(vm);
  sweep(vm);
  vm->max_objects = vm->num_objects * 2;
}

void objPrint(const Object *obj) {
  switch (obj->type) {
  case OBJ_INT:
    putchar(obj->value);
    break;
  case OBJ_PAIR:
    objPrint(obj->head);
    objPrint(obj->tail);
    break;
    // putchar('(');
    // objPrint(obj->head);
    // printf(", ");
    // objPrint(obj->tail);
    // putchar(')');
  }
}

void freeVM(VM *vm) {
  vm->stack_size = 0;
  gc(vm);
  free(vm);
}

void test1() {
  printf("Test 1: Objects on stack are preserved.\n");
  VM *vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);

  gc(vm);
  assert(vm->num_objects == 2, "Should have preserved objects.");
  freeVM(vm);
}

void test2() {
  printf("Test 2: Unreached objects are collected.\n");
  VM *vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  pop(vm);
  pop(vm);

  gc(vm);
  assert(vm->num_objects == 0, "Should have collected objects.");
  freeVM(vm);
}

void test3() {
  printf("Test 3: Reach nested objects.\n");
  VM *vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  pushPair(vm);
  pushInt(vm, 3);
  pushInt(vm, 4);
  pushPair(vm);
  pushPair(vm);

  gc(vm);
  assert(vm->num_objects == 7, "Should have reached objects.");
  freeVM(vm);
}

void test4() {
  printf("Test 4: Handle cycles.\n");
  VM *vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  Object *a = pushPair(vm);
  pushInt(vm, 3);
  pushInt(vm, 4);
  Object *b = pushPair(vm);

  /* Set up a cycle, and also make 2 and 4 unreachable and collectible. */
  a->tail = b;
  b->tail = a;

  gc(vm);
  assert(vm->num_objects == 4, "Should have collected objects.");
  freeVM(vm);
}

void perfTest() {
  printf("Performance Test.\n");
  VM *vm = newVM();

  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 20; j++) {
      pushInt(vm, i);
    }

    for (int k = 0; k < 20; k++) {
      pop(vm);
    }
  }
  freeVM(vm);
}

void swap(VM *vm) {
  Object *obj1 = pop(vm);
  Object *obj2 = pop(vm);
  push(vm, obj1);
  push(vm, obj2);
}

void interpret(VM *vm, const Instruction *i) {
  switch (i->type) {
  case I_DIE:
    die("program error: %s", i->die.errmsg);
  case I_HALT:
    vm->has_halted = true;
    break;
  case I_POP:
    pop(vm);
    break;
  case I_PRINT: {
    Object *o = pop(vm);
    objPrint(o);
    push(vm, o);
  } break;
  case I_READ_I32: {
    i32 ch = getchar();
    pushInt(vm, ch);
    break;
  }
  case I_PSH_I32:
    pushInt(vm, i->push.value);
    break;
  case I_PAIR:
    pushPair(vm);
    break;
  case I_SWP:
    swap(vm);
    break;
  case I_GC:
    gc(vm);
    break;
  case I_ASSERT:
    assert(vm->num_objects == i->assert.expected, "%s", i->assert.msg);
    break;
  }
}

void _run(VM *vm, FILE *fp) {
  Instruction *i;

  while ((i = fetchInstruction(fp)) != NULL && !vm->has_halted) {
    interpret(vm, i);
    // no jump instruction -> never goes back.
    freeInstruction(i);
  }
}

void run(const char *filename) {
  FILE *fp = filename == NULL ? stdin : fopen(filename, "rb");
  assert(fp != NULL, "%s", strerror(errno));

  VM *vm = newVM();
  _run(vm, fp);

  freeVM(vm);
  if (fp != stdin)
    fclose(fp);
}

int main(int argc, const char *argv[]) {
  setvbuf(stdout, NULL, _IONBF, 0);

  const char *fname = NULL;
  if (argc > 0 && argc != 2) {
    printf("Usage: %s [<file>]\n", *argv);
    return 1;
  }
  if (argc == 2) {
    fname = argv[1];
  }

  run(fname);

  return 0;
}
