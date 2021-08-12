#include "parser/parser.h"
#include "token/tokenize.h"

#include <stdbool.h>
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

// Prototype
static void initializer_only(Token *tkn, Token **end_tkn, Initializer *init);
static Initializer *initializer(Token *tkn, Token **end_tkn, Type *ty);
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
static Node *increment_and_decrement(Token *tkn, Token **end_tkn);
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

Node *new_num(Token *tkn, int val) {
  Node *ret = calloc(1, sizeof(Node));
  ret->kind = ND_INT;
  ret->tkn = tkn;
  ret->val = val;
  ret->type = new_type(TY_INT, false);
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
  add_gvar(obj);
  return ret;
}

// kind is ND_ASSIGN if operation only assign
Node *new_assign(NodeKind kind, Token *tkn, Node *lhs, Node *rhs) {
  Node *ret = new_node(ND_ASSIGN, tkn, lhs, rhs);
  ret->assign_type = kind;
  return ret;
}

char *typename[] = {
  "void", "char", "short", "int", "long"
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

// get_type = ("const" type) | (type "const") | type
// type = "char" | "short" | "int" | "long" "long"? "int"?
static Type *get_type(Token *tkn, Token **end_tkn) {
  // We replace the type with a number and count it,
  // which makes it easier to detect duplicates and types.
  // If typename is 'long', we have a duplicate when the long bit
  // and the high bit are 1.
  // Otherwise, we have a duplicate when the high is 1.
  enum {
    VOID  = 1 << 0,
    CHAR  = 1 << 2,
    SHORT = 1 << 4,
    INT   = 1 << 6,
    LONG  = 1 << 8,
  };

  bool is_const = false;
  if (consume(tkn, &tkn, "const")) {
    is_const = true;
  }
  
  int type_cnt = 0;
  Type *ret = NULL;
  while (is_typename(tkn)) {

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
    }

    // Detect duplicates
    char *dup_type = NULL;

    // Avoid check long (so, range -1)
    for (int i = 0; i < (sizeof(typename) / sizeof(char *)) - 1; i++) {
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
        ret = new_type(TY_CHAR, true);
        break;
      case SHORT:
      case SHORT + INT:
        ret = new_type(TY_SHORT, true);
        break;
      case INT:
        ret = new_type(TY_INT, true);
        break;
      case LONG:
      case LONG + INT:
      case LONG + LONG:
      case LONG + LONG + INT:
        ret = new_type(TY_LONG, true);
        break;
      default:
        errorf_tkn(ER_COMPILE, tkn, "Invalid type");
    }
    tkn = tkn->next;
  }

  if (equal(tkn, "const")) {
    if (is_const) {
      errorf_tkn(ER_COMPILE, tkn, "Duplicate const.");
    }
    is_const = true;
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
    fprintf(stderr, "%p\n", tmp->children[0]->children);
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

static Node *create_init_node(Initializer *init, Node **end_node) {
  Node *head = calloc(1, sizeof(Node));

  if (init->children == NULL) {
    head->kind = ND_INIT;
    head->init = init->node;

    if (end_node != NULL) *end_node = head;
    return head;
  }

  Node *now = head;
  for (int i = 0; i < init->ty->array_len; i++) {
    now->lhs = create_init_node(init->children[i], &now);
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
    add_gvar(var);
  } else {
    add_lvar(var);
  }

  Node *ret = new_var(tkn, var);

  if (ty->var_size == 0 && !equal(tkn, "=")) {
    errorf_tkn(ER_COMPILE, tkn, "Size empty array require an initializer.");
  }

  if (consume(tkn, &tkn, "=")) {
    Initializer *init = initializer(tkn, &tkn, var->type);
    ret = new_node(ND_INIT, tkn, ret, create_init_node(init, NULL));
    ret->type = ty;

    // If the lengh of the array is empty, Type will be updated,
    // so it needs to be passed to var as well.
    var->type = init->ty;
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
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
  if (ty == NULL) {
    Node *ret = assign(tkn, &tkn);
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

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
  Node *ret = ternary(tkn, &tkn);
  Token *assign_tkn = tkn;

  if (consume(tkn, &tkn, "=")) {
    ret = new_assign(ND_ASSIGN, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "+=")) {
    ret = new_assign(ND_ADD, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "-=")) {
    ret = new_assign(ND_SUB, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "*=")) {
    ret = new_assign(ND_MUL, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "/=")) {
    ret = new_assign(ND_DIV, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "%=")) {
    ret = new_assign(ND_REMAINDER, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "<<=")) {
    ret = new_assign(ND_LEFTSHIFT, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, ">>=")) {
    ret = new_assign(ND_RIGHTSHIFT, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "&=")) {
    ret = new_assign(ND_BITWISEAND, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "^=")) {
    ret = new_assign(ND_BITWISEXOR, tkn, ret, assign(tkn, &tkn));
  } else if (consume(tkn, &tkn, "|=")) {
    ret = new_assign(ND_BITWISEOR, tkn, ret, assign(tkn, &tkn));
  }

  if (end_tkn != NULL) *end_tkn = tkn;
  add_type(ret);

  if (ret->kind == ND_ASSIGN && ret->lhs != NULL && ret->lhs->type->is_const) {
    errorf_tkn(ER_COMPILE, assign_tkn, "Cannot assign to const variable.");
  }

  return ret;
}

// ternary = logical_or ("?" ternary ":" ternary)?
static Node *ternary(Token *tkn, Token **end_tkn) {
  Node *ret = logical_or(tkn, &tkn);

  if (consume(tkn, &tkn, "?")) {
    Node *tmp = new_node(ND_TERNARY, tkn, NULL, NULL);
    tmp->lhs = ternary(tkn, &tkn);
    if (!consume(tkn, &tkn, ":")) {
      errorf_tkn(ER_COMPILE, tkn, "The ternary operator requires \":\".");
    }
    tmp->rhs = ternary(tkn, &tkn);
    tmp->cond = ret;
    ret = tmp;
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// logical_or = logical_and ("||" logical_or)?
static Node *logical_or(Token *tkn, Token **end_tkn) {
  Node *ret = logical_and(tkn, &tkn);

  if (consume(tkn, &tkn, "||")) {
    ret = new_node(ND_LOGICALOR, tkn, ret, logical_or(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// logical_and = bitwise_or ("&&" logical_and)?
static Node *logical_and(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_or(tkn, &tkn);

  if (consume(tkn, &tkn, "&&")) {
    ret = new_node(ND_LOGICALAND, tkn, ret, logical_and(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_or = bitwise_xor ("|" bitwise_or)?
static Node *bitwise_or(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_xor(tkn, &tkn);
  if (consume(tkn, &tkn, "|")) {
    ret = new_node(ND_BITWISEOR, tkn, ret, bitwise_or(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_xor = bitwise_and ("^" bitwise_xor)?
static Node *bitwise_xor(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_and(tkn, &tkn);
  if (consume(tkn, &tkn, "^")) {
    ret = new_node(ND_BITWISEXOR, tkn, ret, bitwise_xor(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_and = same_comp ("&" bitwise_and)?
static Node *bitwise_and(Token *tkn, Token **end_tkn) {
  Node *ret = same_comp(tkn, &tkn);
  if (consume(tkn, &tkn, "&")) {
    ret = new_node(ND_BITWISEAND, tkn, ret, bitwise_and(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// same_comp = size_comp ("==" same_comp | "!=" sama_comp)?
static Node *same_comp(Token *tkn, Token **end_tkn) {
  Node *ret = size_comp(tkn, &tkn);
  if (consume(tkn, &tkn, "==")) {
    ret = new_node(ND_EQ, tkn, ret, same_comp(tkn, &tkn));
  } else if (consume(tkn, &tkn, "!=")) {
    ret = new_node(ND_NEQ, tkn, ret, same_comp(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// size_comp = bitwise_shift ("<" size_comp  | ">" size_comp | "<=" size_comp |
// ">=" size_comp)?
static Node *size_comp(Token *tkn, Token **end_tkn) {
  Node *ret = bitwise_shift(tkn, &tkn);
  if (consume(tkn, &tkn, "<")) {
    ret = new_node(ND_LC, tkn ,ret, size_comp(tkn, &tkn));
  } else if (consume(tkn, &tkn, ">")) {
    ret = new_node(ND_RC, tkn, ret, size_comp(tkn, &tkn));
  } else if (consume(tkn, &tkn, "<=")) {
    ret = new_node(ND_LEC, tkn, ret, size_comp(tkn, &tkn));
  } else if (consume(tkn, &tkn, ">=")) {
    ret = new_node(ND_REC, tkn, ret, size_comp(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// bitwise_shift = add ("<<" bitwise_shift | ">>" bitwise_shift)?
static Node *bitwise_shift(Token *tkn, Token **end_tkn) {
  Node *ret = add(tkn, &tkn);
  if (consume(tkn, &tkn, "<<")) {
    ret = new_node(ND_LEFTSHIFT, tkn, ret, bitwise_shift(tkn, &tkn));
  } else if (consume(tkn, &tkn, ">>")) {
    ret = new_node(ND_RIGHTSHIFT, tkn, ret, bitwise_shift(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// add = mul ("+" add | "-" add)?
static Node *add(Token *tkn, Token **end_tkn) {
  Node *ret = mul(tkn, &tkn);
  if (consume(tkn, &tkn, "+")) {
    ret = new_node(ND_ADD, tkn, ret, add(tkn, &tkn));
  } else if (consume(tkn, &tkn, "-")) {
    ret = new_node(ND_SUB, tkn, ret, add(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// mul =  cast ("*" mul | "/" mul | "%" mul)?
static Node *mul(Token *tkn, Token **end_tkn) {
  Node *ret = cast(tkn, &tkn);
  if (consume(tkn, &tkn, "*")) {
    ret = new_node(ND_MUL, tkn, ret, mul(tkn, &tkn));
  } else if (consume(tkn, &tkn, "/")) {
    ret = new_node(ND_DIV, tkn, ret, mul(tkn, &tkn));
  } else if (consume(tkn, &tkn, "%")) {
    ret = new_node(ND_REMAINDER, tkn, ret, mul(tkn, &tkn));
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// cast = ("(" get_type pointers? ")") cast |
//        unary
static Node *cast(Token *tkn, Token **end_tkn) {
  if (equal(tkn, "(")) {
    if (get_type(tkn->next, NULL) == NULL) {
      Node *ret = unary(tkn, &tkn);
      if (end_tkn != NULL) *end_tkn = tkn;
      return ret;
    }
    Type *ty = get_type(tkn->next, &tkn);
    ty = pointers(tkn, &tkn, ty);
    if (!consume(tkn, &tkn, ")")) {
      errorf_tkn(ER_COMPILE, tkn, "Cast must end with \")\".");
    }
    Node *ret = new_cast(tkn, cast(tkn, &tkn), ty);
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }
  Node *ret = unary(tkn, &tkn);
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// unary = "sizeof" unary
//         ("+" | "-" | "!" | "~")? address_op
static Node *unary(Token *tkn, Token **end_tkn) {
  if (consume(tkn, &tkn, "sizeof")) {
    Node *ret = new_node(ND_SIZEOF, tkn, unary(tkn, &tkn), NULL);
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }
  Node *ret = NULL;
  if (consume(tkn, &tkn, "+")) {
    ret = new_node(ND_ADD, tkn, new_num(tkn, 0), address_op(tkn, &tkn));
  } else if (consume(tkn, &tkn, "-")) {
    ret = new_node(ND_SUB, tkn, new_num(tkn, 0), address_op(tkn, &tkn));
  } else if (consume(tkn, &tkn, "!")) {
    ret = new_node(ND_LOGICALNOT, tkn, address_op(tkn, &tkn), NULL);
  } else if (consume(tkn, &tkn, "~")) {
    ret = new_node(ND_BITWISENOT, tkn, address_op(tkn, &tkn), NULL);
  }
  if (ret == NULL) {
    ret = address_op(tkn, &tkn);
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// address_op = "&"? indirection
static Node *address_op(Token *tkn, Token **end_tkn) {
  Node *ret = NULL;
  if (consume(tkn, &tkn, "&")) {
    ret = new_node(ND_ADDR, tkn, indirection(tkn, &tkn), NULL);
  }
  if (ret == NULL) {
    ret = indirection(tkn, &tkn);
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// indirection = (increment_and_decrement | "*" indirection)
static Node *indirection(Token *tkn, Token **end_tkn) {
  Node *ret = NULL;
  if (consume(tkn, &tkn, "*")) {
    ret = new_node(ND_CONTENT, tkn, indirection(tkn, &tkn), NULL);
  }
  if (ret == NULL) {
    ret = increment_and_decrement(tkn, &tkn);
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// increment_and_decrement = priority |
//                           ("++" | "--") priority |
//                           priority ("++" | "--")
static Node *increment_and_decrement(Token *tkn, Token **end_tkn) {
  Node *ret = NULL;
  if (consume(tkn, &tkn, "++")) {
    ret = new_node(ND_PREFIX_INC, tkn, priority(tkn, &tkn), NULL);
  } else if (consume(tkn, &tkn, "--")) {
    ret = new_node(ND_PREFIX_INC, tkn, priority(tkn, &tkn), NULL);
  }

  if (ret != NULL) {
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  ret = priority(tkn, &tkn);
  if (consume(tkn, &tkn, "++")) {
    ret = new_node(ND_SUFFIX_INC, tkn, ret, NULL);
  } else if (consume(tkn, &tkn, "--")) {
    ret = new_node(ND_SUFFIX_DEC, tkn, ret, NULL);
  }
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

// priority = num |
//            "({" statement statement* "})" |
//            String(literal) |
//            "(" assign ")" |
//            ident ("(" params? ")")? |
//            ident ("[" assign "]")* |
// params = assign ("," assign)?
// base_type is gen_type()
static Node *priority(Token *tkn, Token **end_tkn) {
  if (consume(tkn, &tkn, "(")) {
    // GNU Statements and Declarations
    if (equal(tkn, "{")) {
      Node *ret = statement(tkn, &tkn, true);
      if (!consume(tkn, &tkn, ")")) {
        errorf_tkn(ER_COMPILE, tkn, "\"(\" and \")\" should be written in pairs.");
      }
      if (end_tkn != NULL) *end_tkn = tkn;
      return ret;
    }
    Node *ret = assign(tkn, &tkn);

    if (!consume(tkn, &tkn, ")")) {
      errorf_tkn(ER_COMPILE, tkn, "\"(\" and \")\" should be written in pairs.");
    }
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  // String literal
  if (tkn->kind == TK_STR) {
    Obj *var = new_obj(new_type(TY_STR, false), tkn->str_lit);
    tkn = tkn->next;
    Node *ret = new_var(tkn, var);
    add_gvar(var);
    if (end_tkn != NULL) *end_tkn = tkn;
    return ret;
  }

  char *ident = get_ident(tkn);
  if (ident != NULL) {
    // function call
    tkn = tkn->next;
    if (consume(tkn, &tkn, "(")) {
      Node *ret = new_node(ND_FUNCCALL, tkn, NULL, NULL);
      ret->func = new_obj(new_type(TY_INT, false), ident); // (Warn: Temporary type)
      ret->type = ret->func->type;

      if (consume(tkn, &tkn, ")")) {
        if (*end_tkn != NULL) *end_tkn = tkn;
        return ret;
      }

      int argc = 0;
      ret->func->args = NULL;
      while (true) {
        Node *tmp = assign(tkn, &tkn);
        tmp->next_stmt = ret->func->args;
        ret->func->args = tmp;
        argc++;

        if (consume(tkn, &tkn, ",")) {
          continue;
        }

        if (!consume(tkn, &tkn, ")")) {
          errorf_tkn(ER_COMPILE, tkn, "Function call must end with \")\".");
        }
        break;
      }
      ret->func->argc = argc;
      if (end_tkn != NULL) *end_tkn = tkn;
      return ret;
    } else {
      // use variable
      Obj *use_var = find_var(ident);
      if (use_var == NULL) {
        errorf_tkn(ER_COMPILE, tkn, "This variable is not definition.");
      }
      Node *ret = new_var(tkn, use_var);
      while (consume(tkn, &tkn, "[")) {
        ret = new_node(ND_ADD, tkn, ret, assign(tkn, &tkn));

        ret = new_node(ND_CONTENT, tkn, ret, NULL);
        if (!consume(tkn, &tkn, "]")) {
          errorf_tkn(ER_COMPILE, tkn, "Must use \"[\".");
        }
      }
      if (end_tkn != NULL) *end_tkn = tkn;
      return ret;
    }
  }

  Node *ret = num(tkn, &tkn);
  if (end_tkn != NULL) *end_tkn = tkn;
  return ret;
}

static Node *num(Token *tkn, Token **end_tkn) {
  if (tkn->kind == TK_NUM_INT) {
    if (end_tkn != NULL) *end_tkn = tkn->next;
    return new_num(tkn, tkn->val);
  }

  if (tkn->kind != TK_CHAR) {
    errorf_tkn(ER_COMPILE, tkn, "Not value.");
  }

  if (end_tkn != NULL) *end_tkn = tkn->next;
  return new_num(tkn, tkn->c_lit);
}
