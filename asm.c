// instructions (very simple):
// 0x00 -> print current from the stack.
// 0x01 -> read character (i32) and push it.
// 0x02 +4byte int               -> push i32 (constant)
// 0x03                          -> pop two & push pair
// 0x04                          -> swap the two
// 0x10                          -> call GC
// 0x12 +4byte int +"string\0"   -> assert the number of allocated objects is
// <int>, otherwise fail with <string>
//
// mnemonics:
// out :: print current
// in :: 0x01
// push <constant> :: push i32
// pair
// swap
// gc
// assert_allocated <constant> <string> :: assert
// print <string> :: print a string of text. with newline.
// halt :: halt
// die <string> :: make the program die
#define _GNU_SOURCE
#include "common.h"
#include "instruction.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__attribute_malloc__ __attribute__((nonnull(2)))
buf_create(usize elem_size, usize *cap) {
  *cap = 4;
  return calloc(4, elem_size);
}

void __attribute__((nonnull(1, 4)))
buf_check(void **buf, usize size, usize current_len, usize *cap) {
  if (*cap == current_len) {
    *cap *= 2;
    *buf = reallocarray(*buf, *cap, size);
  }
}

#define BUF_CHECK(buf, el, cur_len, cap_var)                                   \
  buf_check((void **)&(buf), sizeof(el), (cur_len), &(cap_var))
#define BUF_PUSH(buf, el, cur_len, cap_var)                                    \
  do {                                                                         \
    BUF_CHECK(buf, el, cur_len, cap_var);                                      \
    buf[cur_len] = el;                                                         \
    (cur_len)++;                                                               \
  } while (0)

#define BUF_EXTEND(buf, cur_len, cur_cap, other_buf, other_len)                \
  for (usize i = 0; i < (other_len); i++)                                      \
  BUF_PUSH(buf, (other_buf)[i], cur_len, cur_cap)

typedef uint32_t u32;

void trim_space(const char **src) {
  for (; **src && isspace(**src); (*src)++)
    ;
}

typedef enum {
  TOK_MNEM,
  TOK_CTANT,
  TOK_EOL,
  TOK_DIRECTIVE,
  TOK_IDENT,
  TOK_UNK,
} TType;

typedef enum {
  MNEM_OUT,
  MNEM_IN,
  MNEM_PUSH,
  MNEM_PAIR,
  MNEM_SWP,
  MNEM_GC,
  MNEM_POP,
  MNEM_ASSERT,
  MNEM_PRINT,
  MNEM_DIE,
  MNEM_HALT,
  MNEM_UNK,
} Mnemonic;

typedef enum {
  NUM_DEC,
  NUM_HEX,
  NUM_UNK,
} NType;

typedef struct {
  i32 value;
  NType type;
} Number;

typedef enum { CONST_NUM, CONST_STR, CONST_IDENT } CType;
typedef enum { DIRECTIVE_REPEAT, DIRECTIVE_END, DIRECTIVE_UNK } Directive;

Directive dir_type(const char *src) {
  if (strcasecmp(src, "repeat") == 0)
    return DIRECTIVE_REPEAT;
  if (strcasecmp(src, "end") == 0)
    return DIRECTIVE_END;
  return DIRECTIVE_UNK;
}

typedef struct {
  CType c_type;
  union {
    Number num;
    const char *str;
  };
} Constant;

typedef struct {
  TType type;
  const char *src;
  union {
    Mnemonic mnemonic;
    Directive directive;
    Constant constant;
  };
  usize col;
  usize line;
} Token;

Mnemonic mnem_type(const char *msg) {
  if (strcasecmp(msg, "out") == 0)
    return MNEM_OUT;
  if (strcasecmp(msg, "in") == 0)
    return MNEM_IN;
  if (strcasecmp(msg, "halt") == 0)
    return MNEM_HALT;
  if (strcasecmp(msg, "die") == 0)
    return MNEM_DIE;
  if (strcasecmp(msg, "push") == 0)
    return MNEM_PUSH;
  if (strcasecmp(msg, "pair") == 0)
    return MNEM_PAIR;
  if (strcasecmp(msg, "swap") == 0)
    return MNEM_SWP;
  if (strcasecmp(msg, "assert_allocated") == 0) {
    return MNEM_ASSERT;
  }
  if (strcasecmp(msg, "gc") == 0)
    return MNEM_GC;
  if (strcasecmp(msg, "print") == 0)
    return MNEM_PRINT;
  if (strcasecmp(msg, "pop") == 0)
    return MNEM_POP;
  return MNEM_UNK;
}

int ishexdig(int c) {
  return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') ||
         ('a' <= c && c <= 'f');
}

NType num_type(const char *src) {
  // accept + or -.
  if (*src == '-' || *src == '+')
    src++;
  if (*src == '0') {
    src++;
    if (*src == 'x') {
      src++;
      // verify that rest is hexdigits
      for (; *src; src++) {
        if (!ishexdig(*src))
          return NUM_UNK;
      }
      return NUM_HEX;
    }
    if (*src)
      return NUM_UNK;
    return NUM_DEC; // zero can't have anything behind.
  }

  // verify that everything is a digit.
  for (; *src && isdigit(*src); src++)
    ;
  if (*src)
    return NUM_UNK;
  return NUM_DEC;
}

void identify(Token *tok) {

  tok->type = TOK_UNK;

  if (tok->src == NULL) {
    tok->type = TOK_EOL;
    return;
  }

  if (*tok->src == '%') {
    Directive dtype = dir_type(tok->src + 1);
    assert(dtype != DIRECTIVE_UNK, "Unkown directive: `%s` at %lu:%lu",
           tok->src + 1, tok->line, tok->col);
    tok->type = TOK_DIRECTIVE;
    tok->directive = dtype;
    return;
  }

  if (*tok->src == '"') {
    tok->type = TOK_CTANT;
    tok->constant.c_type = CONST_STR;
    tok->constant.str = tok->src + 1;
    return;
  }

  NType may_num;
  if ((may_num = num_type(tok->src)) != NUM_UNK) {
    tok->type = TOK_CTANT;
    tok->constant.c_type = CONST_NUM;
    tok->constant.num = (Number){.type = may_num, .value = -1};
    return;
  }

  Mnemonic may_kw;
  if ((may_kw = mnem_type(tok->src)) != MNEM_UNK) {
    tok->type = TOK_MNEM;
    tok->mnemonic = may_kw;
    return;
  }

  if (isalpha(*tok->src) || *tok->src == '_') {
    // check that the rest is alphanumeric
    const char *start = tok->src + 1;
    bool id_fine = true;
    for (; *start; ++start) {
      if (!isalnum(*start) && *start != '_') {
        id_fine = false;
        break;
      }
    }
    if (id_fine) {
      tok->type = TOK_IDENT;
      return;
    }
  }
}

char *lex_space(char **line_) {
  char *line = *line_;
  if (*line == 0)
    return NULL;
  char *src = line;
  // skipping a string means skipping until '"'
  if (*line == '"') {
    line++;
    for (; *line && *line != '"'; ++line)
      ;
    line++;
    // otherwise skip till space.
  } else {
    for (; *line && !isspace(*line); ++line)
      ;
  }
  *line = 0;
  line++;
  // skip space after the token
  for (; *line && isspace(*line); ++line)
    ;
  *line_ = line;

  return src;
}

usize next_tok(char **line, Token *tok) {
  char *start = *line;
  char *end = lex_space(line);
  usize plus = end - start;

  tok->src = end;

  return plus;
}

u32 decode_hex(char v) {
  if ('0' <= v && v <= '9')
    return (u32)v - '0';
  if ('A' <= v && v <= 'F')
    return (u32)v + 10 - 'A';
  if ('a' <= v && v <= 'f')
    return (u32)v + 10 - 'a';
  return 0;
}

void parse_num(Token *tok) {
  i32 multiplier = 1;
  Number *num = &tok->constant.num;
  const char *src = tok->src;
  if (*src == '+')
    src++;
  else if (*src == '-') {
    multiplier = -1;
    src++;
  }
  if (tok->constant.num.type == NUM_DEC) {
    u32 val = 0;
    u32 dig = 0;
    for (const char *_src = src; *_src; ++_src) {
      dig = *_src - '0';
      u32 next = val * 10 + dig;
      // overflow
      // if highest bit is set, as well. It overflowed maximum cap.
      assert(!(next < val || next & 0x80000000),
             "Number overflows integer capacity at %lu:%lu: %s", tok->line,
             tok->col, tok->src);

      val = next;
    }

    num->value = (i32)val * multiplier;
  } else {
    // hexadecimal
    src += 2;
    u32 val = 0;
    u32 dig = 0;
    for (const char *_src = src; *_src; ++_src) {
      dig = decode_hex(*_src);
      u32 next = val * 16 + dig;
      // overflow
      // if highest bit is set, as well. It overflowed maximum cap.
      assert(!(next < val || next & 0x80000000),
             "Number overflows integer capacity at %lu:%lu: %s", tok->line,
             tok->col, tok->src);

      val = next;
    }

    num->value = (i32)val * multiplier;
  }
}

typedef struct {
  Mnemonic opcode; // 0xfa is used to tell the assembler to generate print code.
  const char *str; // for instructions with strings.
  i32 num;         // for instructions with numbers
} Op;

static const u8 opcodes[] = {
    [MNEM_OUT] = I_PRINT,     [MNEM_IN] = I_READ_I32, [MNEM_PUSH] = I_PSH_I32,
    [MNEM_PAIR] = I_PAIR,     [MNEM_SWP] = I_SWP,     [MNEM_GC] = I_GC,
    [MNEM_ASSERT] = I_ASSERT, [MNEM_PRINT] = 0xfa,    [MNEM_POP] = I_POP,
    [MNEM_HALT] = I_HALT,     [MNEM_DIE] = I_DIE,
};

const char *mnemonic_name(Mnemonic mnem) {
  // print is an alias.
  switch (mnem) {
  case MNEM_PRINT:
    return "print";
  default:
    return inames[opcodes[mnem]];
  }
}

void assert_eof(char **line, Token *current, const char *msg) {
  next_tok(line, current);
  assert(current->type == TOK_EOL, "%s", msg);
}

static void opcode(FILE *fp, Mnemonic opcode) {
  fwrite(&opcodes[opcode], 1, 1, fp);
}
static void out_val(FILE *fp, i32 value) { fwrite(&value, 4, 1, fp); }
static void out_str(FILE *fp, const char *str) {
  *strchrnul(str, '"') = 0;
  fputs(str, fp);
  fputc(0, fp);
}

static void push(FILE *fp, i32 value) {
  opcode(fp, MNEM_PUSH);
  fwrite(&value, 4, 1, fp);
}

static void pop(FILE *fp) { opcode(fp, MNEM_POP); }
static void pout(FILE *fp) { opcode(fp, MNEM_OUT); }
static void in(FILE *fp) { opcode(fp, MNEM_IN); }
static void pair(FILE *fp) { opcode(fp, MNEM_PAIR); }
static void swp(FILE *fp) { opcode(fp, MNEM_SWP); }

static void out_assert(FILE *fp, i32 val, const char *str) {
  opcode(fp, MNEM_ASSERT);
  out_val(fp, val);
  out_str(fp, str);
}

static void gc(FILE *fp) { opcode(fp, MNEM_GC); }
static void halt(FILE *fp) { opcode(fp, MNEM_HALT); }
static void out_die(FILE *fp, const char *errmsg) {
  opcode(fp, MNEM_DIE);
  out_str(fp, errmsg);
}

void process_op(FILE *out, const Op *op) {
  switch (op->opcode) {
  case MNEM_HALT:
    halt(out);
    break;
  case MNEM_DIE:
    out_die(out, op->str);
    break;
  case MNEM_PRINT:
    // loop the string in reverse
    {

      // make sure the pairs are aligned.
      push(out, op->str[0]);
      push(out, op->str[1]);
      pair(out);
      *strchrnul(op->str, '"') = 0;
      for (const char *start = &op->str[2]; *start; start++) {
        push(out, *start);
        pair(out);
      }
      push(out, '\n');
      pair(out);

      pout(out);
      pop(out);
      // force a deallocation.
      gc(out);
    }
    break;
  case MNEM_OUT:
    pout(out);
    break;
  case MNEM_POP:
    pop(out);
    break;
  case MNEM_PUSH:
    push(out, op->num);
    break;
  case MNEM_IN:
    in(out);
    break;
  case MNEM_PAIR:
    pair(out);
    break;
  case MNEM_SWP:
    swp(out);
    break;
  case MNEM_UNK:
    break;
  case MNEM_ASSERT:
    out_assert(out, op->num, op->str);
    break;
  case MNEM_GC:
    gc(out);
    break;
  }

  if (errno) {
    perror("fwrite");
    exit(1);
  }
}

typedef enum { SCOPE_NORMAL, SCOPE_REPEAT } SType;

typedef struct {
  Token *tokens;
  usize tokens_len;
  usize tokens_size;
  const char *original_line;
  usize line_no;
  bool is_original;
} TokLine;

void print_tokline(const TokLine *line);

void release_parsed_line(TokLine *l) {
  if (!l->is_original)
    return;
  free((void *)l->original_line);
  free(l->tokens);
}

typedef struct {
  const char *name;
  Constant constant;
} Binding;

typedef enum { OUT_SCOPE, OUT_SINGLE } OType;

struct __scope;

typedef struct {
  OType type;
  union {
    TokLine *line;
    struct __scope *inner_scope;
  };
} Output;

typedef struct __scope {
  SType scope_type;
  usize decl_line;
  Output **out;
  usize scope_len;
  usize scope_size;
  struct __scope *next;

  Binding *constants;
  usize consts_len;
  usize consts_cap;

  union {
    struct {
      usize n;
      // The variable for the loop. If ommitted, it's _.
      const char *var_name;
    } repeat;
  };
} Scope;

typedef enum {
  // directives like %repeat need a scope
  IM_BEGIN_SCOPE,
  // %end directive
  IM_END_SCOPE,
  // %repeat, %const (future).
  IM_DIRECTIVE,
  // no operation. useful for commented lines.
  IM_NOOP,
  // anything that is not the other three is an instruction
  IM_INSTR,
} IMType;

typedef struct {
  IMType type;
  TokLine *line;
  union {
    Scope *new_scope;
  };
} IMCode;

Output *out_line(TokLine *line) {
  Output *out = malloc(sizeof(Output));
  out->type = OUT_SINGLE;
  out->line = line;
  return out;
}

Output *out_scope(Scope *s) {
  Output *out = malloc(sizeof(Output));
  out->type = OUT_SCOPE;
  out->inner_scope = s;
  return out;
}

TokLine *tokenize_line(const char *line_, usize line_no) {
  TokLine *l = malloc(sizeof(TokLine));
  l->is_original = true;
  char *line = strdup(line_);
  l->line_no = line_no;
  l->original_line = line;
  l->tokens_len = 0;
  l->tokens_size = 4;
  l->tokens = calloc(4, sizeof(*l->tokens));

  Token current;
  current.col = 0;

  // check later to push EOF
  do {
    usize lexed = next_tok(&line, &current);
    current.line = line_no;
    identify(&current);
    assert(current.type != TOK_UNK, "Unknown token: `%s` at %lu:%lu",
           current.src, current.line, current.col);
    BUF_PUSH(l->tokens, current, l->tokens_len, l->tokens_size);
    current.col += lexed;
  } while (current.type != TOK_EOL);

  return l;
}

void __attribute__((nonnull)) push_out(Scope *current, Output *out) {
  if (current->scope_len == current->scope_size) {
    current->scope_size *= 2;
    current->out = reallocarray(current->out, current->scope_size, sizeof(out));
  }
  current->out[current->scope_len] = out;
  (current->scope_len)++;
  //  BUF_PUSH(current->lines, line, current->scope_len, current->scope_size);
}

void __attribute__((nonnull)) push_line(Scope *current, TokLine *line) {
  push_out(current, out_line(line));
}

void __attribute__((nonnull)) push_scope(Scope *current, Scope *sc) {
  push_out(current, out_scope(sc));
}

// returns a normal scope.
Scope *__attribute_const__ new_scope() {
  Scope *s = malloc(sizeof(Scope));
  s->decl_line = 0;
  s->scope_type = SCOPE_NORMAL;
  s->scope_len = 0;
  s->scope_size = 4;
  s->out = calloc(4, sizeof(Output *));
  s->consts_len = 0;
  s->consts_cap = 4;
  s->constants = calloc(4, sizeof(Binding));
  s->next = NULL;
  return s;
}

// Find variable in **single** scope.
bool __attribute__((nonnull(2, 3))) __attribute_const__
scope_find(const Scope *s, const char *name, Constant *dest) {
  if (s == NULL)
    return false;
  for (usize i = 0; i < s->consts_len; i++) {
    if (strcmp(name, s->constants[i].name) == 0) {
      *dest = s->constants[i].constant;
      return true;
    }
  }
  return false;
}

// Find variable using the scope chain
bool __attribute__((nonnull(2, 3))) __attribute_const__
find_constant(const Scope *s, const char *name, Constant *dest) {
  if (s == NULL)
    return false;
  do {
    if (scope_find(s, name, dest))
      return true;
    s = s->next;
  } while (s != NULL);
  return false;
}

bool resolve_const(const Scope *s, const char *name, Constant *dest) {
  if (!find_constant(s, name, dest))
    return false;
  if (dest->c_type == CONST_IDENT)
    return resolve_const(s, dest->str, dest);
  return true;
}

void __attribute__((nonnull(1, 2)))
set_constant(Scope *s, const char *name, Constant value) {
  // replace the constant if it's there.
  for (usize i = 0; i < s->consts_len; i++) {
    Binding *c = &s->constants[i];
    if (strcmp(name, c->name) == 0) {
      c->constant = value;
      return;
    }
  }
  // else, push a new one.
  Binding c = (Binding){.name = name, .constant = value};
  BUF_PUSH(s->constants, c, s->consts_len, s->consts_cap);
}

void release_scope(Scope *s);

void release_output(Output *out) {
  switch (out->type) {
  case OUT_SCOPE:
    release_scope(out->inner_scope);
    free(out->inner_scope);
    break;
  case OUT_SINGLE:
    release_parsed_line(out->line);
    free(out->line);
    break;
  }
}

void __attribute__((nonnull)) release_scope(Scope *s) {
  for (usize i = 0; i < s->scope_len; i++) {
    release_output(s->out[i]);
    free(s->out[i]);
  }
  // free scope lines and tokens
  free(s->out);
  free(s->constants);

  if (s->scope_type == SCOPE_REPEAT) {
    free((void *)s->repeat.var_name);
  }
}

Scope *__attribute__((nonnull(2))) __attribute_const__
repeat_scope(usize n, const char *var_name) {
  Scope *s = new_scope();
  s->scope_type = SCOPE_REPEAT;
  s->repeat.n = n;
  s->repeat.var_name = var_name;
  return s;
}

Op __attribute_const__ new_op(Mnemonic opcode) {
  Op op;
  op.opcode = opcode;
  return op;
}

static const char *expected_token_explanations[] = {
    [TOK_MNEM] = "mnemonic",   [TOK_DIRECTIVE] = "directive",
    [TOK_EOL] = "end of line", [TOK_IDENT] = "identifier",
    [TOK_CTANT] = "constant",
};

// note: the tokens can mutate while the tokline can't, what???,
Token *__attribute__((nonnull)) please_tok(const TokLine *line, usize *index) {
  assert(line->tokens_len > *index, "Insufficient tokens at line %lu",
         line->line_no);
  return &line->tokens[(*index)++];
}

Token *__attribute__((nonnull(1, 2))) __attribute_const__
expect_tok(const TokLine *line, usize *index, TType expected_type) {
  Token *tok = please_tok(line, index);
  assert(tok->type == expected_type,
         "Got %s while expecting %s at %lu:%lu: `%s`",
         expected_token_explanations[tok->type],
         expected_token_explanations[expected_type], line->line_no, tok->col,
         tok->src);

  return tok;
}

#define MAX_OPSPEC_ARGS 2

// specification for an operation.
typedef struct {
  const Mnemonic opcode;
  const usize args_len;
  const CType args[MAX_OPSPEC_ARGS];
} OpSpec;

static const char *ctant_names[] = {
    [CONST_NUM] = "number",
    [CONST_STR] = "string",
};

typedef struct {
  CType type;
  union {
    const char *str;
    i32 value;
  };
} Resolved;

// resolves the constant to a non intermediate state,
// that is either string or numeric value.
void __attribute__((nonnull)) _resolve_constant(Token *tok, const Scope *s) {
  Constant *ctant = &tok->constant;
  if (ctant->c_type == CONST_NUM) {
    // the number needs to be parsed.
    // TODO: expressions here.
    parse_num(tok);
  }
  if (ctant->c_type == CONST_IDENT) {
    // find the binding
    assert(resolve_const(s, tok->src, ctant),
           "Couldn't find constant `%s` from %lu:%lu in the current scope",
           tok->src, tok->line, tok->col);
  }
  // if it's a string, it's already resolved.
}

Constant __attribute__((nonnull))
resolve_constant(const Token *tok, const Scope *s) {
  Token replacable = *tok;
  _resolve_constant(&replacable, s);
  assert(replacable.constant.c_type != CONST_IDENT,
         "Identifier should have be gone or errored out");
  return replacable.constant;
}

// TODO: make some work to accept expressions
Token *__attribute__((nonnull))
expect_constant(const TokLine *line, usize *index) {

  Token *tok = please_tok(line, index);

  assert(tok->type == TOK_CTANT || tok->type == TOK_IDENT,
         "Got %s while expecting a constant expression at %lu:%lu: `%s`",
         expected_token_explanations[tok->type], line->line_no, tok->col,
         tok->src);

  if (tok->type == TOK_IDENT) {
    tok->type = TOK_CTANT;
    tok->constant.c_type = CONST_IDENT;
    tok->constant.str = tok->src;
  }

  return tok;
}

Constant __attribute__((nonnull(1, 2, 4)))
expect_constant_kind(const TokLine *line, usize *index, CType type,
                     const Scope *s) {

  Token *tok = expect_constant(line, index);
  Constant ctant = resolve_constant(tok, s);
  assert(ctant.c_type == type, "Expected %s, got %s at %lu:%lu: `%s`",
         ctant_names[type], ctant_names[ctant.c_type], tok->line, tok->col,
         tok->src);
  return ctant;
}

// i32 __attribute__((nonnull))
// expect_number(const TokLine *line, usize *index, const Scope *s) {
//   Token *tok = expect_constant(line, index);
//   resolve_constant(tok, s);
//   const Constant *ctant = &tok->constant;
//   assert(ctant->c_type == CONST_NUM, "Expected number, got %s at %lu:%lu:
//   `%s`",
//          ctant_names[ctant->c_type], tok->line, tok->col, tok->src);

//   return ctant->num.value;
// }

// const char *__attribute__((nonnull))
// expect_str(const TokLine *line, usize *index, const Scope *s) {
//   Token *tok = expect_constant(line, index);
//   resolve_constant(tok, s);
//   const Constant *ctant = &tok->constant;
//   assert(ctant->c_type == CONST_STR, "Expected number, got %s at %lu:%lu:
//   `%s`",
//          ctant_names[ctant->c_type], tok->line, tok->col, tok->src);
//   return ctant->str;
// }

void __attribute__((nonnull(1, 2, 3, 4)))
opcode_insert(Op *op, const TokLine *line, usize *index, const Scope *s,
              CType ctype) {

  Constant ctant = expect_constant_kind(line, index, ctype, s);


  switch (ctant.c_type) {
    // this one is already handled by `resolve_cosntant`.
  case CONST_IDENT:
    break;
  case CONST_NUM:
    op->num = ctant.num.value;
    break;
  case CONST_STR:
    op->str = ctant.str;
    break;
  }
}

void expect_eol(const TokLine *args, usize i) {
  (void)expect_tok(args, &i, TOK_EOL);
}
const OpSpec specs[] = {
    [MNEM_ASSERT] = (OpSpec){.opcode = MNEM_ASSERT,
                             .args_len = 2,
                             .args = {CONST_NUM, CONST_STR}},
    [MNEM_DIE] =
        (OpSpec){.opcode = MNEM_DIE, .args_len = 1, .args = {CONST_STR}},
    [MNEM_GC] = (OpSpec){.opcode = MNEM_GC, .args_len = 0},
    [MNEM_HALT] = (OpSpec){.opcode = MNEM_HALT, .args_len = 0},
    [MNEM_IN] = (OpSpec){.opcode = MNEM_IN, .args_len = 0},
    [MNEM_OUT] = (OpSpec){.opcode = MNEM_OUT, .args_len = 0},
    [MNEM_PAIR] = (OpSpec){.opcode = MNEM_PAIR, .args_len = 0},
    [MNEM_PRINT] =
        (OpSpec){.opcode = MNEM_PRINT, .args_len = 1, .args = {CONST_STR}},
    [MNEM_PUSH] =
        (OpSpec){.opcode = MNEM_PUSH, .args_len = 1, .args = {CONST_NUM}},
    [MNEM_POP] = (OpSpec){.opcode = MNEM_POP, .args_len = 0},
    [MNEM_SWP] = (OpSpec){.opcode = MNEM_SWP, .args_len = 0}};

Op __attribute_const__ __attribute__((nonnull))
parse(const TokLine *line, const Scope *scope) {
  usize i = 0;
  Op op;
  Token *fst = expect_tok(line, &i, TOK_MNEM);
  op.opcode = fst->mnemonic;

  const OpSpec *spec = &specs[op.opcode];

  for (usize j = 0; j < spec->args_len; j++) {
    opcode_insert(&op, line, &i, scope, spec->args[j]);
  }

  if (line->tokens[spec->args_len + 1].type != TOK_EOL) {
    if (spec->args_len == 0) {
      die("Opcode `%s` takes no arguments", mnemonic_name(op.opcode));
    } else if (spec->args_len == 1) {
      die("Opcode `%s` takes one argument", mnemonic_name(op.opcode));
    } else {
      die("Opcode `%s` takes %lu arguments", mnemonic_name(op.opcode),
          spec->args_len);
    }
  }

  return op;
}

void print_tok(const Token *tok);

TokLine *advance(const TokLine *original, usize n) {
  TokLine *l = malloc(sizeof(TokLine));
  *l = *original;
  assert(l->tokens_len > n, "Tried to advance line more than expected");
  l->is_original = false;
  l->tokens += n;
  l->tokens_len -= n;
  return l;
}

i32 __attribute__((nonnull(1, 3)))
expect_number(const TokLine *args, usize index, const Scope *s) {
  return expect_constant_kind(args, &index, CONST_NUM, s).num.value;
}

const char *__attribute__((nonnull(1, 3)))
expect_str(const TokLine *args, usize index, const Scope *s) {
  return expect_constant_kind(args, &index, CONST_NUM, s).str;
}

void __attribute__((nonnull(1, 2)))
parse_repeat(IMCode *code, const TokLine *args, const Scope *s) {
  usize i = 1;
  i32 n = expect_number(args, i, s);
  assert(n >= 0, "Attempt to repeat a negative amount: %d", n);
  i = 2;
  const char *str = args->tokens_len > 3
                        ? strdup(expect_tok(args, &i, TOK_IDENT)->src)
                        : NULL;
  (void)expect_tok(args, &i, TOK_EOL);
  code->type = IM_BEGIN_SCOPE;
  code->new_scope = repeat_scope(n, str);
  code->new_scope->decl_line = args->line_no;
}

void __attribute__((nonnull(1, 2)))
parse_end(IMCode *code, const TokLine *args, const Scope *s) {
  (void)s;
  expect_eol(args, 1);
  code->type = IM_END_SCOPE;
}

static void (*directive_parsers[])(IMCode *code, const TokLine *args,
                                   const Scope *s) = {
    [DIRECTIVE_REPEAT] = parse_repeat,
    [DIRECTIVE_END] = parse_end,
};

void __attribute__((nonnull(1, 2)))
parse_directive(IMCode *code, const TokLine *line, Directive directive,
                const Scope *s) {
  directive_parsers[directive](code, line, s);
}

IMCode get_code(TokLine *line, const Scope *s) {
  if (line->tokens_len == 0)
    return (IMCode){.type = IM_NOOP, .line = line};

  IMCode code;
  usize i = 0;
  Token *first = please_tok(line, &i);
  switch (first->type) {
  case TOK_DIRECTIVE:
    parse_directive(&code, line, first->directive, s);
    release_parsed_line(line);
    free(line);
    break;
  case TOK_MNEM:
    code.type = IM_INSTR;
    code.line = line;
    break;
  default:
    die("Expected directive or mnemonic, got instead %s at %lu:%lu: `%s`",
        expected_token_explanations[first->type], line->line_no, first->col,
        first->src);
  }
  return code;
}

static void __attribute__((nonnull)) flatten_scope(Scope *s, FILE *out);

static void __attribute__((nonnull))
flatten_output(const Output *out, FILE *outf, Scope *s) {
  switch (out->type) {
  case OUT_SCOPE:
    out->inner_scope->next = s;
    flatten_scope(out->inner_scope, outf);
    out->inner_scope->next = NULL;
    return;
  case OUT_SINGLE: {
    Op op = parse(out->line, s);
    process_op(outf, &op);
    return;
  }
  }
}

// TODO: macro name tokens
// macro names
// parsing macros
// complete parse routine.

// Flatten a normal scope. Plain old simple.
// Just put the instructions one by one.
static void flatten_normal_scope(Scope *s, FILE *outf) {
  for (usize i = 0; i < s->scope_len; i++) {
    flatten_output(s->out[i], outf, s);
  }
}

// Flatten a %repeat macro, which repeats its inner instructions and
// gives access to a constant for the block that will change on each iteration.
// the language cannot jump, it is not turing complete. So this is the only way
// you can do loops without hurting your hand  badly.
// interior mutability: will mutate internally, but that doesn't raise any
// problem. The variables inside the scopes will be **overriden** on each call,
// no assumption is made about them.
static void flatten_repeat_scope(Scope *s, FILE *outf) {

  Constant c;
  c.c_type = CONST_NUM;

  for (c.num.value = 0; c.num.value < s->repeat.n; c.num.value++) {

    // the scope IS mutated from the inside.
    // there is no change that the values will be read badly as every time
    // this function is called the loop overrides the variable.
    if (s->repeat.var_name)
      set_constant((Scope *)s, s->repeat.var_name, c);

    flatten_normal_scope(s, outf);
  }
}

static void (*scope_flatteners[])(Scope *s, FILE *) = {
    [SCOPE_NORMAL] = flatten_normal_scope,
    [SCOPE_REPEAT] = flatten_repeat_scope};

static void flatten_scope(Scope *s, FILE *out) {
  return scope_flatteners[s->scope_type](s, out);
}

char *__attribute_const__ trim_line(char *line) {
  char *end = strchrnul(line, ';');
  // trim right
  for (; *(end - 1) && isspace(*(end - 1)); --end)
    ;
  // trim left.
  for (; *line && isspace(*line); ++line)
    ;

  *end = 0;
  return line;
}

IMCode type_only(IMType type) { return (IMCode){.line = NULL, .type = type}; }

void __attribute__((nonnull))
follow_imcode(IMCode *code, Scope **scope, FILE *out) {

  switch (code->type) {
  case IM_DIRECTIVE:
    puts("directives");
    die("Sorry, directives are not implemented.");
  case IM_NOOP:
    break;
  case IM_INSTR:
    assert(code->line != NULL, "No line specified");
    assert(*scope != NULL, "%s", "Trying to push instructions without a scope");
    push_line(*scope, code->line);
    break;
  case IM_BEGIN_SCOPE: {
    code->new_scope->next = *scope;
    *scope = code->new_scope;
  } break;
  case IM_END_SCOPE:
    assert(*scope != NULL, "popped too many scopes");
    // scope ended. pop it from the environment stack.
    // push it into the last scope.

    Scope *current = *scope;
    Scope *last = current->next;
    // move 'current' to 'last's output.
    push_scope(last, current);
    *scope = last;
    break;
  }
}

void print_ctant(const Token *tok) {
  const Constant *ctant = &tok->constant;
  if (ctant->c_type == CONST_IDENT)
    printf("%s", ctant->str);
  else if (ctant->c_type == CONST_NUM) {
    printf("\x1b[38;5;5m%s\x1b[m", tok->src);
  } else {
    printf("\x1b[38;5;10m%s\x1b[m", tok->src);
  }
}

void print_mnemonic(const char *msg) { printf("\x1b[38;5;12m%s\x1b[m", msg); }

void print_tok(const Token *tok) {
  switch (tok->type) {
  case TOK_EOL:
    break;
  case TOK_CTANT:
    print_ctant(tok);
    break;
  case TOK_MNEM:
    print_mnemonic(tok->src);
    break;
  case TOK_IDENT:
    printf("%s", tok->src);
    break;
  case TOK_DIRECTIVE:
    print_mnemonic(tok->src);
    break;
  default:
    die("unreachable");
  }
}

void print_tokline(const TokLine *line) {
  printf("%lu ", line->line_no);

  for (usize i = 0; i < line->tokens_len; i++) {
    print_tok(&line->tokens[i]);
    if (i != line->tokens_len - 1)
      putchar(' ');
  }
  putchar('\n');
}

// quick assembler line by line. doesn't support anything more than mnemonics,
// basic constants (hex and dec), basic strings (no escape support) and their
// arguments separated.
int main(int argc, const char *argv[]) {
  if (argc != 2 && argc != 3) {
    printf("Usage: %s <file> [<out>]\n", *argv);
    return 1;
  }

  FILE *fp = fopen(argv[1], "rb");
  if (fp == NULL) {
    perror("fopen");
    return 1;
  }

  FILE *out = fopen(argc > 2 ? argv[2] : "a.out", "wb");
  if (out == NULL) {
    perror("fopen");
    return 1;
  }

  setvbuf(out, NULL, _IONBF, 0);

  Scope *current = new_scope();
  Scope *first = current;

  char *line = 0;
  usize line_sz = 0;
  usize line_no = 1;
  i32 line_len = 0;

  for (; (line_len = getline(&line, &line_sz, fp)) > 0; line_no++) {
    char *trimmed = trim_line(line);
    if (!*trimmed)
      continue;
    TokLine *tok_line = tokenize_line(trimmed, line_no);
    IMCode code = get_code(tok_line, current);
    follow_imcode(&code, &current, out);
  }

  if (line_len == -1 && errno != 0) {
    perror("getline");
    return 1;
  }

  assert(current == first,
         "Please consider giving scope at line %lu an end marker with `%%end`",
         current->decl_line);

  flatten_scope(current, out);
  release_scope(current);
  free(current);

  if (line)
    free(line);

  fclose(fp);
  fclose(out);

  return 0;
}
