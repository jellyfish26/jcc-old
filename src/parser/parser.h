#pragma once

#include "token/tokenize.h"

#include <stdint.h>

typedef enum {
  ND_ADD,
  ND_SUB,
  ND_MUL,
  ND_DIV,
  ND_NUM,
  ND_MOD,
  ND_LSHIFT,
  ND_RSHIFT,
  ND_LCMP,
  ND_LECMP,
  ND_EQ,
  ND_NEQ,
  ND_BITAND,
  ND_BITXOR,
  ND_BITOR,
  ND_LOGAND,
  ND_LOGOR,
} NodeKind;

typedef struct Node Node;
struct Node {
  Token *tkn;

  NodeKind kind;
  Node *lhs;
  Node *rhs;

  int64_t val;
};

Node *parser(Token *tkn);
