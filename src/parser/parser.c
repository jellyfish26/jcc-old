#include "parser/parser.h"
#include "token/tokenize.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// This structure is used to initialize variable.
// In particular, Array initializer can be nested, so need tree data structure.
typedef struct Initializer Initializer;
struct Initializer {
  Type *ty;
  Token *tkn;

  // The size of the first array can be omitted.
  bool is_flexible;
  Initializer **children;

  Node *node;
};

typedef struct VarAttr VarAttr;

// Prototype
static void initializer_only(Token *tkn, Token **end_tkn, Initializer *init);
static Initializer *initializer(Token *tkn, Token **end_tkn, Type *ty);
static int64_t eval_expr(Node *node);
static bool is_const_expr(Node *node, char **ptr_label);
static Node *topmost(Token *tkn, Token **end_tkn);
static Node *statement(Token *tkn, Token **end_tkn, bool new_scope);
static Node *declarations(Token *tkn, Token**end_tkn, Type *ty, bool is_global);
static Node *assign(Token *tkn, Token **end_tkn);
static Node *ternary(Token *tkn, Token **end_tkn);
static Node *logical_or(Token *tkn, Token **end_tkn);
static Node *logical_and(Token *tkn, Token **end_tkn);
static Node *bitwise_or(Token *tkn, Token **end_tkn);
static Node *bitwise_xor(Token *tkn, Token **end_tkn);
static Node *bitwise_and(Token *tkn, Token **end_tkn);
static Node *same_comp(Token *tkn, Token **end_tkn);
static Node *size_comp(Token *tkn, Token **end_tkn);
static Node *bitwise_shift(Token *tkn, Token **end_tkn);
static Node *add(Token *tkn, Token **end_tkn);
static Node *mul(Token *tkn, Token **end_tkn);
static Node *cast(Token *tkn, Token **end_tkn);
static Node *unary(Token *tkn, Token **end_tkn);
static Node *address_op(Token *tkn, Token **end_tkn);
static Node *indirection(Token *tkn, Token **end_tkn);
static Node *inc_dec(Token *tkn, Token **end_tkn);
static Node *primary(Token *tkn, Token **end_tkn);
static Node *identifier(Token *tkn, Token **end_tkn);
static Node *constant(Token *tkn, Token **end_tkn);


static Node *priority(Token *tkn, Token **end_tkn);
static Node *num(Token *tkn, Token **end_tkn);

Node *new_node(NodeKind kind, Token *tkn, Node *lhs, Node *rhs) {
  Node *ret = calloc(1, sizeof(Node));
  ret->kind = kind;
  ret->lhs = lhs;
  ret->rhs = rhs;
  ret->tkn = tkn;
  return ret;
}

Node *new_num(Token *tkn, int64_t val) {
  Node *ret = calloc(1, sizeof(Node));
  ret->kind = ND_INT;
  ret->tkn = tkn;
  ret->val = val;

  ret->type = new_type(TY_INT, false);
  ret->type->is_const = true;
  return ret;
}

Node *new_cast(Token *tkn, Node *lhs, Type *type) {
  Node *ret = new_node(ND_CAST, tkn, NULL, NULL);
  ret->lhs = lhs;
  add_type(ret);
  ret->type = type;
  return ret;
}

Node *new_var(Token *tkn, Obj *obj) {
  Node *ret = new_node(ND_VAR, tkn, NULL, NULL);
  ret->use_var = obj;
  ret->type = obj->type;
  return ret;
}

Node *new_strlit(Token *tkn, char *strlit) {
  Obj *obj = new_obj(new_type(TY_STR, false), strlit);
  Node *ret = new_var(tkn, obj);
  add_gvar(obj, false);
  return ret;
}

static bool is_addr_type(Node *node) {
  switch (node->type->kind) {
    case TY_PTR:
    case TY_ARRAY:
    case TY_STR:
      return true;
    default:
      return false;
  }
}

Node *new_calc(NodeKind kind, Token *tkn, Node *lhs, Node *rhs) {
  Node *node = new_node(kind, tkn, lhs, rhs);
  add_type(node);

  if (node->lhs->type->kind == TY_PTR || node->rhs->type->kind == TY_PTR) {
    errorf_tkn(ER_COMPILE, tkn, "Invalid operand.");
  }
  return node;
}

Node *new_add(Token *tkn, Node *lhs, Node *rhs) {
  Node *node = new_node(ND_ADD, tkn, lhs, rhs);
  add_type(node);

  if (is_addr_type(node->lhs) && is_addr_type(node->rhs)) {
    errorf_tkn(ER_COMPILE, tkn, "Invalid operand.");
  }

  if (is_addr_type(node->lhs)) {
    node->rhs = new_calc(ND_MUL, tkn, node->rhs, new_num(tkn, node->lhs->type->base->var_size));
  }

  if (is_addr_type(node->rhs)) {
    node->lhs = new_calc(ND_MUL, tkn, node->lhs, new_num(tkn, node->rhs->type->base->var_size));
  }

  return node;
}

Node *new_sub(Token *tkn, Node *lhs, Node *rhs) {
  Node *node = new_node(ND_SUB, tkn, lhs, rhs);
  add_type(node);

  if (node->lhs->type->kind == TY_PTR && node->rhs->type->kind == TY_PTR) {
    node->type = new_type(TY_LONG, false);
  }

  if (is_addr_type(node->lhs) && !is_addr_type(node->rhs)) {
    node->rhs = new_calc(ND_MUL, tkn, node->rhs, new_num(tkn, node->lhs->type->base->var_size));
  }

  if (is_addr_type(node->rhs) && !is_addr_type(node->lhs)) {
    node->lhs = new_calc(ND_MUL, tkn, node->lhs, new_num(tkn, node->rhs->type->base->var_size));
  }

  return node;
}

Node *new_assign(Token *tkn, Node *lhs, Node *rhs) {
  // Remove implicit cast 
  if (lhs->kind == ND_CAST) {
    lhs = lhs->lhs;
  }

  Node *ret = new_node(ND_ASSIGN, tkn, lhs, rhs);

  add_type(ret);
  if (lhs != NULL && lhs->type->is_const) {
    errorf_tkn(ER_COMPILE, tkn, "Cannot assign to const variable.");
  }

  return ret;
}

Node *to_assign(Token *tkn, Node *rhs) {
  return new_assign(tkn, rhs->lhs, rhs);
}

char *typename[] = {
  "void", "char", "short", "int", "long", "signed", "unsigned", "const"
};

static bool is_typename(Token *tkn) {
  for (int i = 0; i < sizeof(typename) / sizeof(char *); i++) {
    if (equal(tkn, typename[i])) {
      return true;
    }
  }
  return false;
}

// If not ident, return NULL.
static char *get_ident(Token *tkn) {
  if (tkn->kind != TK_IDENT) {
    return NULL;
  }
  char *ret = calloc(tkn->len + 1, sizeof(char));
  memcpy(ret, tkn->loc, tkn->len);
  return ret;
}

// get_type = typename typename*
// typename = "char" | "short" | "int" | "long" | "signed" | "unsigned"
static Type *get_type(Token *tkn, Token **end_tkn) {
  // We replace the type with a number and count it,
  // which makes it easier to detect duplicates and types.
  // If typename is 'long', we have a duplicate when the long bit
  // and the high bit are 1.
  // Otherwise, we have a duplicate when the high is 1.
  enum {
    VOID     = 1 << 0,
    CHAR     = 1 << 2,
    SHORT    = 1 << 4,
    INT      = 1 << 6,
    LONG     = 1 << 8,
    SIGNED   = 1 << 10,
    UNSIGNED = 1 << 12,
  };

  bool is_const = false;

  int type_cnt = 0;
  Type *ret = NULL;
  while (is_typename(tkn)) {
    if (equal(tkn, "const")) {
      if (is_const) {
        errorf_tkn(ER_COMPILE, tkn, "Duplicate const.");
      }
      is_const = true;
      tkn = tkn->next;
      continue;
    }

    // Counting Types
    if (equal(tkn,"void")) {
      type_cnt += VOID;
    } else if (equal(tkn, "char")) {
      type_cnt += CHAR;
    } else if (equal(tkn, "short")) {
      type_cnt += SHORT;
    } else if (equal(tkn, "int")) {
      type_cnt += INT;
    } else if (equal(tkn, "long")) {
      type_cnt += LONG;
    } else if (equal(tkn, "signed")) {
      type_cnt += SIGNED;
    } else if (equal(tkn, "unsigned")) {
      type_cnt += UNSIGNED;
    }

    // Detect duplicates
    char *dup_type = NULL;

    // Avoid check long
    for (int i = 0; i < (sizeof(typename) / sizeof(char *) - 1); i++) {
      if (memcmp(typename[i], "long", 4) == 0) {
        continue;
      }

      if (((type_cnt>>(i * 2 + 1))&1) == 1) {
        dup_type = typename[i];
      }
    }

    // Check long
    if (((LONG * 3)&type_cnt) == (LONG * 3)) {
      dup_type = "long";
    }

    if (dup_type != NULL) {
      errorf_tkn(ER_COMPILE, tkn, "Duplicate declaration of '%s'", dup_type);
    }

    switch (type_cnt) {
      case VOID:
        ret = new_type(TY_VOID, false);
        break;
      case CHAR:
      case SIGNED + CHAR:
        ret = new_type(TY_CHAR, true);
        break;
      case UNSIGNED + CHAR:
        ret =  new_type(TY_CHAR, true);
        ret->is_unsigned = true;
        break;
      case SHORT:
      case SHORT + INT:
      case SIGNED + SHORT:
      case SIGNED + SHORT + INT:
        ret = new_type(TY_SHORT, true);
        break;
      case UNSIGNED + SHORT:
      case UNSIGNED + SHORT + INT:
        ret = new_type(TY_SHORT, true);
        ret->is_unsigned = true;
        break;
      case INT:
      case SIGNED:
      case SIGNED + INT:
        ret = new_type(TY_INT, true);
        break;
      case UNSIGNED:
      case UNSIGNED + INT:
        ret = new_type(TY_INT, true);
        ret->is_unsigned = true;
        break;
      case LONG:
      case LONG + INT:
      case LONG + LONG:
      case LONG + LONG + INT:
      case SIGNED + LONG:
      case SIGNED + LONG + INT:
      case SIGNED + LONG + LONG:
      case SIGNED + LONG + LONG + INT:
        ret = new_type(TY_LONG, true);
        break;
      case UNSIGNED + LONG:
      case UNSIGNED + LONG + INT:
      case UNSIGNED + LONG + LONG:
      case UNSIGNED + LONG + LONG + INT:
        ret = new_type(TY_LONG, true);
        ret->is_unsigned = true;
        break;
      default:
        errorf_tkn(ER_COMPILE, tkn, "Invalid type");
    }
    tkn = tkn->next;
  }

  if (ret != NULL) {
    ret->is_const = is_const;
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

static Initializer *new_initializer(Type *ty, bool is_flexible) {
  Initializer *ret = calloc(1, sizeof(Initializer));
  ret->ty = ty;

  if (ty->kind == TY_ARRAY) {
    if (is_flexible && ty->var_size == 0) {
      ret->is_flexible = true;
      return ret;
    }

    ret->children = calloc(ty->array_len, sizeof(Initializer *));
    for (int i = 0; i < ty->array_len; i++) {
      ret->children[i] = new_initializer(ty->base, false);
    }
    return ret;
  }
  return ret;
}

static int count_array_init_elements(Token *tkn, Type *ty) {
  int cnt = 0;
  Initializer *dummy = new_initializer(ty->base, false);
  tkn = skip(tkn, "{");

  while (true) {
    if (cnt > 0 && !consume(tkn, &tkn, ",")) break;
    if (equal(tkn, "}")) break;
    initializer_only(tkn, &tkn, dummy);
    cnt++;
  }

  return cnt;
}

// array_initializer = "{" initializer_only ("," initializer_only)* ","? "}" | "{" "}"
static void array_initializer(Token *tkn, Token **end_tkn, Initializer *init) {
  if (init->is_flexible) {
    int len = count_array_init_elements(tkn, init->ty);
    *init = *new_initializer(array_to(init->ty->base, len), false);
  }

  tkn = skip(tkn, "{");
  if (consume(tkn, &tkn, "}")) {
    if (end_tkn != NULL) *end_tkn = tkn;
    return;
  }

  int idx = 0;
  while (true) {
    if (idx > 0 && !consume(tkn, &tkn, ",")) break;
    if (consume(tkn, &tkn, "}")) break;
    initializer_only(tkn, &tkn, init->children[idx]);
    idx++;
  }

  consume(tkn, &tkn, "}");
  if (end_tkn != NULL) *end_tkn = tkn;
}

// string_initializer = string literal
static void string_initializer(Token *tkn, Token **end_tkn, Initializer *init) {
  // If type is not Array, initializer return address of string literal.
  // Otherwise, char number store in each elements of Array.

  if (init->ty->kind != TY_ARRAY) {
    init->node = new_strlit(tkn, tkn->str_lit);
    tkn = tkn->next;
    if (end_tkn != NULL) *end_tkn = tkn;
    return;
  }

  if (init->is_flexible) {
    int len = 0;
    for (char *chr = tkn->str_lit; *chr != '\0'; read_char(chr, &chr)) len++;
    Initializer *tmp = new_initializer(array_to(init->ty->base, len), false);
    *init = *tmp;
  }

  int idx = 0;
  char *chr = tkn->str_lit;
  while (*chr != '\0') {
    init->children[idx]->node = new_num(tkn, read_char(chr, &chr));
    idx++;
  }

  tkn = tkn->next;
  if (end_tkn != NULL) *end_tkn = tkn;
}

// initializer = array_initializer | string_initializer | assign
static void initializer_only(Token *tkn, Token **end_tkn, Initializer *init) {
  if (tkn->kind == TK_STR) {
    string_initializer(tkn, &tkn, init);
  } else if (init->ty->kind == TY_ARRAY) {
    array_initializer(tkn, &tkn, init);
  } else {
    init->node = assign(tkn, &tkn);
  }

  if (end_tkn != NULL) *end_tkn = tkn;
}

static Initializer *initializer(Token *tkn, Token **end_tkn, Type *ty) {
  Initializer *ret = new_initializer(ty, true);
  initializer_only(tkn, &tkn, ret);
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

static Node *create_init_node(Initializer *init, Node **end_node, bool only_const) {
  Node *head = calloc(1, sizeof(Node));

  if (init->children == NULL) {
    head->kind = ND_INIT;
    head->init = init->node;

    if (only_const && init->node != NULL) {
      char *ptr_label = NULL;
      if (!is_const_expr(init->node, &ptr_label)) {
        errorf_tkn(ER_COMPILE, init->node->tkn, "Require constant expression.");
      }

      head->init = new_num(init->node->tkn, eval_expr(init->node));
      head->init->use_var = calloc(1, sizeof(Obj));
      head->init->use_var->name = ptr_label;
    }

    if (end_node != NULL) *end_node = head;
    return head;
  }

  Node *now = head;
  for (int i = 0; i < init->ty->array_len; i++) {
    now->lhs = create_init_node(init->children[i], &now, only_const);
  }

  if (end_node != NULL) *end_node = now;
  return head->lhs;
}

// pointers = ("*" "const"?)*
static Type *pointers(Token *tkn, Token **end_tkn, Type *ty) {
  while (consume(tkn, &tkn, "*")) {
    ty = pointer_to(ty);
    if (consume(tkn, &tkn, "const")) {
      ty->is_const = true;
    }
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ty;
}

// arrays = "[" num "]" arrays?
// num can empty value if can_empty is true.
static Type *arrays(Token *tkn, Token **end_tkn, Type *ty, bool can_empty) {
  if (!consume(tkn, &tkn, "[")) {
    if (*end_tkn != NULL) *end_tkn = tkn;
    return ty;
  }

  if (!can_empty && tkn->kind != TK_NUM_INT) {
      errorf_tkn(ER_COMPILE, tkn, "Specify the size of array.");
  }

  int val = 0;
  if (tkn->kind == TK_NUM_INT) {
    val = tkn->val;
    tkn = tkn->next;
  }

  tkn = skip(tkn, "]");
  ty = arrays(tkn, &tkn, ty, false);

  if (end_tkn != NULL) *end_tkn = tkn;
  return array_to(ty, val);
}

// declare = ident arrays
static Obj *declare(Token *tkn, Token **end_tkn, Type *ty) {
  char *ident = get_ident(tkn);

  if (ident == NULL) {
    errorf_tkn(ER_COMPILE, tkn, "The variable declaration requires an identifier.");
  }

  ty = arrays(tkn->next, &tkn, ty, true);

  if (end_tkn != NULL) *end_tkn = tkn;
  return new_obj(ty, ident);
}

// declarator = pointers declare ("=" initializer)?
// Return NULL if cannot be declared.
static Node *declarator(Token *tkn, Token **end_tkn, Type *ty, bool is_global) {
  ty = pointers(tkn, &tkn, ty);
  Obj *var = declare(tkn, &tkn, ty);

  if (is_global) {
    add_gvar(var, true);
  } else {
    add_lvar(var);
  }

  Node *ret = new_var(tkn, var);

  if (ty->var_size == 0 && !equal(tkn, "=")) {
    errorf_tkn(ER_COMPILE, tkn, "Size empty array require an initializer.");
  }

  if (equal(tkn, "=")) {
    Initializer *init = initializer(tkn->next, &tkn, var->type);
    ret = new_node(ND_INIT, tkn, ret, create_init_node(init, NULL, is_global));
    ret->type = ty;

    if (is_global) {
      ret->lhs->use_var->val = ret->rhs->init->val;
    }

    // If the lengh of the array is empty, Type will be updated,
    // so it needs to be passed to var as well.
    var->type = init->ty;
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// Evaluate a given node as a constant expression;
static int64_t eval_expr(Node *node) {
  add_type(node);

  switch (node->kind) {
    case ND_ADD:
      return eval_expr(node->lhs) + eval_expr(node->rhs);
    case ND_SUB:
      return eval_expr(node->lhs) + eval_expr(node->rhs);
    case ND_MUL:
      return eval_expr(node->lhs) * eval_expr(node->rhs);
    case ND_DIV:
      return eval_expr(node->lhs) / eval_expr(node->rhs);
    case ND_REMAINDER:
      return eval_expr(node->lhs) % eval_expr(node->rhs);
    case ND_EQ:
      return eval_expr(node->lhs) == eval_expr(node->rhs);
    case ND_NEQ:
      return eval_expr(node->lhs) != eval_expr(node->rhs);
    case ND_LC:
      return eval_expr(node->lhs) < eval_expr(node->rhs);
    case ND_LEC:
      return eval_expr(node->lhs) <= eval_expr(node->rhs);
    case ND_LEFTSHIFT:
      return eval_expr(node->lhs) << eval_expr(node->rhs);
    case ND_RIGHTSHIFT:
      return eval_expr(node->lhs) >> eval_expr(node->rhs);
    case ND_BITWISEAND:
      return eval_expr(node->lhs) & eval_expr(node->rhs);
    case ND_BITWISEOR:
      return eval_expr(node->lhs) | eval_expr(node->rhs);
    case ND_BITWISEXOR:
      return eval_expr(node->lhs) ^ eval_expr(node->rhs);
    case ND_LOGICALAND:
      return eval_expr(node->lhs) && eval_expr(node->rhs);
    case ND_LOGICALNOT:
      return !eval_expr(node->lhs);
    case ND_BITWISENOT:
      return ~eval_expr(node->lhs);
    case ND_TERNARY:
      return eval_expr(node->cond) ? eval_expr(node->lhs) : eval_expr(node->rhs);
    case ND_CAST:
      switch (node->type->kind) {
        case TY_CHAR:
          return (int8_t)eval_expr(node->lhs);
        case TY_SHORT:
          return (int16_t)eval_expr(node->lhs);
        case TY_INT:
          return (int32_t)eval_expr(node->lhs);
        case TY_LONG:
          return (int64_t)eval_expr(node->lhs);
        default:
          return 0;
      }
    case ND_VAR:
      if (is_addr_type(node)) {
        return 0;
      }
      return node->use_var->val;
    case ND_CONTENT:
      if (node->lhs->kind == ND_ADDR) {
        return eval_expr(node->lhs->lhs);
      }
      return eval_expr(node->lhs);
    case ND_INT:
      return node->val;
    default:
      return 0;
  }
}

static bool is_const_expr(Node *node, char **ptr_label) {
  add_type(node);

  switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_REMAINDER:
    case ND_EQ:
    case ND_NEQ:
    case ND_LC:
    case ND_LEC:
    case ND_LEFTSHIFT:
    case ND_RIGHTSHIFT:
    case ND_BITWISEAND:
    case ND_BITWISEOR:
    case ND_BITWISEXOR:
    case ND_LOGICALAND:
    case ND_LOGICALOR:
      return is_const_expr(node->lhs, ptr_label) && is_const_expr(node->rhs, ptr_label);
    case ND_LOGICALNOT:
    case ND_BITWISENOT:
      return is_const_expr(node->lhs, ptr_label);
    case ND_TERNARY:
      return is_const_expr(node->cond, ptr_label);
    case ND_CAST:
      switch (node->type->kind) {
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
          return is_const_expr(node->lhs, ptr_label);
        default:
          return false;
      }
    case ND_ADDR: {
      if (*ptr_label != NULL) {
        return false;
      }
      if (node->lhs->kind != ND_VAR) {
        return false;
      }
      *ptr_label = node->lhs->use_var->name;
      return true;
    }
    case ND_CONTENT:
      if (node->lhs->kind == ND_ADDR) {
        return is_const_expr(node->lhs->lhs, ptr_label);
      }
      return is_const_expr(node->lhs, ptr_label);
    case ND_INT:
      return true;
    case ND_VAR: {
      Obj *var = node->use_var;
      if (var->type->kind == TY_ARRAY) {
        if (*ptr_label != NULL) {
          return false;
        }

        *ptr_label = var->name;
        return true;
      }

      return var->is_global && var->type->is_const;
    }
    default:
      return false;
  }
}

static Node *last_stmt(Node *now) {
  while (now->next_stmt != NULL) {
    now = now->next_stmt;
  }
  return now;
}

Node *program(Token *tkn) {
  lvars = NULL;
  gvars = NULL;
  used_vars = NULL;
  Node *head = calloc(1, sizeof(Node));
  Node *now = head;
  while (!is_eof(tkn)) {
    now->next_block = topmost(tkn, &tkn);
    now = now->next_block;
  }
  return head->next_block;
}

// topmost = get_type ident "(" params?")" statement |
//           get_type declarations ";"
// params = get_type ident ("," get_type ident)*
static Node *topmost(Token *tkn, Token **end_tkn) {
  Token *head_tkn = tkn;
  Type *ty = get_type(tkn, &tkn);

  if (ty == NULL) {
    errorf_tkn(ER_COMPILE, tkn, "Undefined type.");
  }

  // Global variable declare
  if (!equal(tkn->next, "(")) {
    Node *ret = declarations(tkn, &tkn, ty, true);

    if (!consume(tkn, &tkn, ";")) {
      errorf_tkn(ER_COMPILE, tkn, "A ';' is required at the end of the declaration.");
    }

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  char *ident = get_ident(tkn);
  if (ident == NULL) {
    errorf_tkn(ER_COMPILE, tkn,
        "Identifiers are required for function declaration.");
  }

  // Fuction definition
  Node *ret = new_node(ND_FUNC, tkn, NULL, NULL);
  ret->func = new_obj(ty, ident);
  new_scope_definition();

  if (!consume(tkn->next, &tkn, "(")) {
    errorf_tkn(ER_COMPILE, tkn, "Function declaration must start with '('.");
  }

  // Set arguments
  if (!consume(tkn, &tkn, ")")) while (true) {
    Type *arg_ty = get_type(tkn, &tkn);
    if (arg_ty == NULL) {
      errorf_tkn(ER_COMPILE, tkn, "Undefined type.");
    }

    Node *lvar = new_var(tkn, declare(tkn, &tkn, arg_ty));
    add_lvar(lvar->use_var);
    lvar->lhs = ret->func->args;
    ret->func->args = lvar;
    ret->func->argc++;

    if (!equal(tkn, ",") && consume(tkn, &tkn, ")")) {
      break;
    }

    consume(tkn, &tkn, ",");
  }

  ret->next_stmt = statement(tkn, &tkn, false);
  out_scope_definition();
  ret->func->vars_size = init_offset();

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

static Node *inside_roop; // inside for or while

// statement = { statement* } |
//             ("return")? assign ";" |
//             "if" "(" assign ")" statement ("else" statement)? |
//             "for" "(" declarations? ";" assign? ";" assign?")" statement |
//             "while" "(" assign ")" statement |
//             "break;" |
//             "continue;" |
//             declarations ";"
static Node *statement(Token *tkn, Token **end_tkn, bool new_scope) {

  // Block statement
  if (equal(tkn, "{")) {
    if (new_scope) new_scope_definition();

    Node *ret = new_node(ND_BLOCK, tkn, NULL, NULL);

    Node *head = calloc(1, sizeof(Node));
    Node *now = head;
    
    tkn = tkn->next;
    while (!consume(tkn, &tkn, "}")) {
      now->next_stmt = statement(tkn, &tkn, true);
      now = last_stmt(now);
    }

    ret->next_block = head->next_stmt;

    if (new_scope) out_scope_definition();
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "if")) {
    tkn = skip(tkn->next, "(");
    new_scope_definition();

    Node *ret = new_node(ND_IF, tkn, NULL, NULL);
    ret->cond = assign(tkn, &tkn);
    Obj *top_var= lvars->objs;
    tkn = skip(tkn, ")");

    ret->then = statement(tkn, &tkn, false);

    // Transfer variables.
    if (top_var != NULL && lvars->objs == top_var) {
      lvars->objs = NULL;
    } else if (top_var != NULL) {
      for (Obj *obj = lvars->objs; obj != NULL; obj = obj->next) {
        if (obj->next == top_var) {
          obj->next = NULL;
          break;
        }
      }
    }
    
    out_scope_definition();

    if (equal(tkn, "else")) {
      new_scope_definition();

      if (top_var != NULL) add_lvar(top_var);
      ret->other = statement(tkn->next, &tkn, false);

      out_scope_definition();
    }

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "for")) {
    tkn = skip(tkn->next, "(");
    new_scope_definition();

    Node *roop_state = inside_roop;
    Node *ret = new_node(ND_FOR, tkn, NULL, NULL);
    inside_roop = ret;


    if (!consume(tkn, &tkn, ";")) {
      Type *ty = get_type(tkn, &tkn);
      ret->init = declarations(tkn, &tkn, ty, false);
      
      tkn = skip(tkn, ";");
    }

    if (!consume(tkn, &tkn, ";")) {
      ret->cond = assign(tkn, &tkn);
      tkn = skip(tkn, ";");
    }

    if (!consume(tkn, &tkn, ")")) {
      ret->loop = assign(tkn, &tkn);
      tkn = skip(tkn, ")");
    }

    ret->then = statement(tkn, &tkn, false);
    out_scope_definition();

    inside_roop = roop_state;

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "while")) {
    tkn = skip(tkn->next, "(");
    new_scope_definition();

    Node *roop_state = inside_roop;
    Node *ret = new_node(ND_WHILE, tkn, NULL, NULL);
    inside_roop = ret;

    ret->cond = assign(tkn, &tkn);

    tkn = skip(tkn, ")");

    ret->then = statement(tkn, &tkn, false);
    inside_roop = roop_state;

    out_scope_definition();
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "return")) {
    Node *ret = new_node(ND_RETURN, tkn, assign(tkn->next, &tkn), NULL);
    tkn = skip(tkn, ";");

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "break")) {
    if (inside_roop == NULL) {
      errorf_tkn(ER_COMPILE, tkn, "Not within loop.");
    }

    Node *ret = new_node(ND_LOOPBREAK, tkn, inside_roop, NULL);
    tkn = skip(tkn->next, ";");

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  if (equal(tkn, "continue")) {
    if (inside_roop == NULL) {
      errorf_tkn(ER_COMPILE, tkn, "Not within loop.");
    }

    Node *ret = new_node(ND_CONTINUE, tkn, inside_roop, NULL);
    tkn = skip(tkn->next, ";");

    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  Type *ty = get_type(tkn, &tkn);
  Node *ret = declarations(tkn, &tkn, ty, false);
  tkn = skip(tkn, ";");

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// declarations = declarator ("," declarator)* |
//                assign
// Return last node, head is connect node.
static Node *declarations(Token *tkn, Token**end_tkn, Type *ty, bool is_global) {
  if (ty == NULL) return assign(tkn, end_tkn);

  Node *ret = declarator(tkn, &tkn, ty, is_global);
  Node *now = ret;

  while (consume(tkn, &tkn, ",")) {
    now->next_stmt = declarator(tkn, &tkn, ty, is_global);
    now = now->next_stmt;
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}


// assign = ternary ("=" assign | "+=" assign | "-=" assign
//                   "*=" assign | "/=" assign | "%=" assign
//                   "<<=" assign | ">>=" assign
//                   "&=" assign | "^=" assign | "|=" assign)?
static Node *assign(Token *tkn, Token **end_tkn) {
  Node *node = ternary(tkn, &tkn);

  if (equal(tkn, "="))
    return new_assign(tkn, node, assign(tkn->next, end_tkn));

  if (equal(tkn, "+="))
    return to_assign(tkn, new_add(tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "-="))
    return to_assign(tkn, new_sub(tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "*="))
    return to_assign(tkn, new_calc(ND_MUL, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "/="))
    return to_assign(tkn, new_calc(ND_DIV, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "%="))
    return to_assign(tkn, new_calc(ND_REMAINDER, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "<<="))
    return to_assign(tkn, new_calc(ND_LEFTSHIFT, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, ">>="))
    return to_assign(tkn, new_calc(ND_RIGHTSHIFT, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "&="))
    return to_assign(tkn, new_calc(ND_BITWISEAND, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "^="))
    return to_assign(tkn, new_calc(ND_BITWISEXOR, tkn, node, assign(tkn->next, end_tkn)));

  if (equal(tkn, "|="))
    return to_assign(tkn, new_calc(ND_BITWISEOR, tkn, node, assign(tkn->next, end_tkn)));

  add_type(node);
  if (end_tkn != NULL) *end_tkn = tkn;
  return node;
}

// ternary = logical_or ("?" ternary ":" ternary)?
static Node *ternary(Token *tkn, Token **end_tkn) {
  Node *ret = logical_or(tkn, &tkn);

  if (equal(tkn, "?")) {
    ret = new_node(ND_TERNARY, tkn, ret, NULL);
    ret->cond = ret->lhs;
    ret->lhs = ternary(tkn->next, &tkn);
    
    tkn = skip(tkn, ":");

    ret->rhs = ternary(tkn, &tkn);
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// logical_or = logical_and ("||" logical_and)*
static Node *logical_or(Token *tkn, Token **end_tkn) {
  Node *ret = logical_and(tkn, &tkn);

  while (equal(tkn, "||")) {
    Token *operand = tkn;
    ret = new_calc(ND_LOGICALOR, operand, ret, logical_and(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// logical_and = bitwise_or ("&&" bitwise_or)*
static Node *logical_and(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_or(tkn, &tkn);

  while (equal(tkn, "&&")) {
    Token *operand = tkn;
    ret = new_calc(ND_LOGICALAND, operand, ret, bitwise_or(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_or = bitwise_xor ("|" bitwise_xor)*
static Node *bitwise_or(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_xor(tkn, &tkn);

  while (equal(tkn, "|")) {
    Token *operand = tkn;
    ret = new_calc(ND_BITWISEOR, operand, ret, bitwise_xor(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_xor = bitwise_and ("^" bitwise_and)*
static Node *bitwise_xor(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_and(tkn, &tkn);

  while (equal(tkn, "^")) {
    Token *operand = tkn;
    ret = new_calc(ND_BITWISEXOR, operand, ret, bitwise_and(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_and = same_comp ("&" same_comp)*
static Node *bitwise_and(Token *tkn, Token **end_tkn) {
  Node *ret = same_comp(tkn, &tkn);

  while (equal(tkn, "&")) {
    Token *operand = tkn;
    ret = new_calc(ND_BITWISEAND, operand, ret, same_comp(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// same_comp = size_comp ("==" size_comp | "!=" size_comp)*
static Node *same_comp(Token *tkn, Token **end_tkn) {
  Node *ret = size_comp(tkn, &tkn);

  while (equal(tkn, "==") || equal(tkn, "!=")) {
    NodeKind kind = equal(tkn, "==") ? ND_EQ : ND_NEQ;
    Token *operand = tkn;
    ret = new_node(kind, operand, ret, same_comp(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// size_comp = bitwise_shift ("<" bitwise_shift |
//                            ">" bitwise_shift |
//                            "<=" bitwise_shift|
//                            ">=" bitwise_shift)?
static Node *size_comp(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_shift(tkn, &tkn);

  while (equal(tkn, "<") || equal(tkn, ">") ||
         equal(tkn, "<=") || equal(tkn, ">=")) {
    NodeKind kind = equal(tkn, "<") || equal(tkn, ">") ? ND_LC : ND_LEC;

    if (equal(tkn, ">") || equal(tkn, ">=")) {
      ret = new_node(kind, tkn, bitwise_shift(tkn->next, &tkn), ret);
    } else {
      ret = new_node(kind, tkn, ret, bitwise_shift(tkn->next, &tkn));
    }
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_shift = add ("<<" add | ">>" add )*
static Node *bitwise_shift(Token *tkn, Token **end_tkn) {
  Node *ret = add(tkn, &tkn);

  while (equal(tkn, "<<") || equal(tkn, ">>")) {
    NodeKind kind = equal(tkn, "<<") ? ND_LEFTSHIFT : ND_RIGHTSHIFT;
    Token *operand = tkn;
    ret = new_calc(kind, operand, ret, add(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token *tkn, Token **end_tkn) {
  Node *ret = mul(tkn, &tkn);

  while (equal(tkn, "+") || equal(tkn, "-")) {
    Token *operand = tkn;
    if (equal(tkn, "+")) {
      ret = new_add(operand, ret, mul(tkn->next, &tkn));
    }

    if (equal(tkn, "-")) {
      ret = new_sub(operand, ret, mul(tkn->next, &tkn));
    }
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// mul = cast ("*" cast | "/" cast | "%" cast )*
static Node *mul(Token *tkn, Token **end_tkn) {
  Node *ret = cast(tkn, &tkn);

  while (equal(tkn, "*") || equal(tkn, "/") || equal(tkn, "%")) {
    NodeKind kind = ND_VOID;
    Token *operand = tkn;

    if (equal(tkn, "*")) {
      kind = ND_MUL;
    } else if (equal(tkn, "/")) {
      kind = ND_DIV;
    } else {
      kind = ND_REMAINDER;
    }

    ret = new_node(kind, operand, ret, cast(tkn->next, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// cast = ("(" get_type pointers? ")") cast |
//        unary
static Node *cast(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "(") && get_type(tkn->next, NULL) != NULL) {
    Type *ty = get_type(tkn->next, &tkn);
    ty = pointers(tkn, &tkn, ty);

    tkn = skip(tkn, ")");

    return new_cast(tkn, cast(tkn, end_tkn), ty);
  }

  return unary(tkn, end_tkn);
}


// unary = ("sizeof" | "+" | "-" | "!" | "~") unary |
//         address_op
static Node *unary(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "sizeof")) {
    // Type size
    if (equal(tkn->next, "(") && get_type(tkn->next->next, NULL) != NULL) {
      Type *ty = get_type(tkn->next->next, &tkn);
      ty = pointers(tkn, &tkn, ty);
      ty = arrays(tkn, &tkn, ty, false);

      tkn = skip(tkn, ")");
      if (end_tkn != NULL) *end_tkn = tkn;
      return new_num(tkn, ty->var_size);
    }

    Node *node = unary(tkn->next, end_tkn);
    add_type(node);

    return new_num(tkn, node->type->var_size);
  }

  if (equal(tkn, "+") || equal(tkn, "-")) {
    NodeKind kind = equal(tkn, "+") ? ND_ADD : ND_SUB;
    return new_node(kind, tkn, new_num(tkn, 0), unary(tkn->next, end_tkn));
  }

  if (equal(tkn, "!") || equal(tkn, "~")) {
    NodeKind kind = equal(tkn, "!") ? ND_LOGICALNOT : ND_BITWISENOT;
    return new_node(kind, tkn, unary(tkn->next, end_tkn), NULL);
  }

  return address_op(tkn, end_tkn);
}

// address_op = "&"? indirection
static Node *address_op(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "&")) {
    return new_node(ND_ADDR, tkn, indirection(tkn->next, end_tkn), NULL);
  }

  return indirection(tkn, end_tkn);
}

// indirection = (inc_dec | "*" indirection)
static Node *indirection(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "*")) {
    return new_node(ND_CONTENT, tkn, indirection(tkn->next, end_tkn), NULL);
  }

  return inc_dec(tkn, end_tkn);
}


// inc_dec = primary | ("++" | "--") primary | primary ("++" | "--")
//
// Convert ++a to (a += 1), --a to (a -= 1)
// Convert a++ to ((a += 1) - 1), a-- to ((a -= 1) + 1);
static Node *inc_dec(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "++")) {
    return to_assign(tkn, new_add(tkn, primary(tkn->next, end_tkn), new_num(tkn, 1)));
  }

  if (equal(tkn, "--")) {
    return to_assign(tkn, new_sub(tkn, primary(tkn->next, end_tkn), new_num(tkn, 1)));
  }

  Node *node = primary(tkn, &tkn);

  if (equal(tkn, "++")) {
    node = to_assign(tkn, new_add(tkn, node, new_num(tkn, 1)));
    node = new_sub(tkn, node, new_num(tkn, 1));

    if (end_tkn != NULL) *end_tkn = tkn->next;
    return node;
  }

  if (equal(tkn, "--")) {
    node = to_assign(tkn, new_sub(tkn, node, new_num(tkn, 1)));
    node = new_add(tkn, node, new_num(tkn, 1));
    tkn = tkn->next;
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return node;
}

// primary-expression = gnu-statement-expr |
//                      identifier
//                      constant
//                      string-literal
//                      "(" expr ")"
// 
// gnu-statement-expr = "({" statement statement* "})"
static Node *primary(Token *tkn, Token **end_tkn) {
  // GNU Statements
  if (equal(tkn, "(") && equal(tkn->next, "{")) {
    Node *ret = statement(tkn->next, &tkn, true);
 
    tkn = skip(tkn, ")");
 
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  // identifier
  Node *node = identifier(tkn, &tkn);
  if (node != NULL) {
    if (end_tkn != NULL) *end_tkn = tkn;
    return node;
  }

  // constant
  node = constant(tkn, &tkn);
  if (node != NULL) {
    if (end_tkn != NULL) *end_tkn = tkn;
    return node;
  }

  if (equal(tkn, "(")) {
    Node *node = assign(tkn->next, &tkn);

    tkn = skip(tkn, ")");
    if (end_tkn != NULL) *end_tkn = tkn;
    return node;
  }

  if (tkn->kind != TK_STR) {
    errorf_tkn(ER_COMPILE, tkn, "Grammatical error.");
  }

  if (end_tkn != NULL) *end_tkn = tkn->next;
  return new_strlit(tkn, tkn->str_lit);
}

// identifier         = ident |
//                      ident ( "(" params? ")" ) |
//                      ident ( "[" expression "]" )
// params             = expression ( "," expression )?
static Node *identifier(Token *tkn, Token **end_tkn) {
  char *ident = get_ident(tkn);
  if (ident == NULL) {
    if (end_tkn != NULL) *end_tkn = tkn;
    return NULL;
  }

  if (equal(tkn->next, "(")) {
    Node *node = new_node(ND_FUNCCALL, tkn, NULL, NULL);
    node->type = new_type(TY_INT, false);
    node->func = new_obj(node->type, ident);
    tkn = tkn->next->next;

    while (!consume(tkn, &tkn, ")")) {
      if (node->func->args != NULL) {
        tkn = skip(tkn, ",");
      }

      Node *expr = assign(tkn, &tkn);
      expr->next_stmt = node->func->args;
      node->func->args = expr;
      node->func->argc++;
    }

    if (end_tkn != NULL) *end_tkn = tkn;
    return node;
  }

  Obj *var = find_var(ident);
  if (var == NULL) {
    errorf_tkn(ER_COMPILE, tkn, "This variable is not declaration.");
  }

  Node *node = new_var(tkn, var);
  tkn = tkn->next;
  while (consume(tkn, &tkn, "[")) {
    node = new_add(tkn, node, assign(tkn, &tkn));
    node = new_node(ND_CONTENT, tkn, node, NULL);
    tkn = skip(tkn, "]");
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return node;
}

// constant = interger-constant |
//            character-constant
static Node *constant(Token *tkn, Token **end_tkn) {
  Node *node = NULL;
  if (tkn->kind == TK_NUM_INT) {
    node = new_num(tkn, tkn->val);
  }

  if (tkn->kind == TK_CHAR) {
    node = new_num(tkn, tkn->c_lit);
  }

  if (end_tkn != NULL) {
    *end_tkn = (node == NULL ? tkn : tkn->next);
  }
  return node;
}
