#include "asmgen/asmgen.h"
#include "parser/parser.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *target_file;
static int num_labels = 0;

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(target_file, fmt, ap);
  fprintf(target_file, "\n");
  va_end(ap);
}

static void gen_push(const char *reg) {
  println("  push %%%s", reg);
}

static void gen_pop(const char *reg) {
  println("  pop %%%s", reg);
}

// The value must be placed on the stack before calling this function.
// Address is rdi
static void gen_store(Type *ty) {
  gen_pop("rax");

  if (ty->size == 1) {
    println("  mov %%ax, (%%rdi)");
  } else if (ty->size == 2) {
    println("  mov %%al, (%%rdi)");
  } else if (ty->size == 4) {
    println("  mov %%eax, (%%rdi)");
  } else {
    println("  mov %%rax, (%%rdi)");
  }
}

static void gen_load(Type *ty) {
  println("  mov (%%rax), %%rax");

  if (ty->size == 1) {
    println("  movzx %%ax, %%rax");
  } else if (ty->size == 2) {
    println("  movzx %%al, %%rax");
  } else if (ty->size == 4) {
    println("  mov %%eax, %%eax");
  }
}

static void gen_expr(Node *node) {
  if (node->kind == ND_NONE) {
    return;
  }

  if (node->kind == ND_NUM) {
    println("  mov $%ld, %%rax", node->val);
    return;
  }

  switch (node->kind) {
  case ND_COND: {
    int label = num_labels++;
    gen_expr(node->cond);
    println("  cmp $0, %%rax");
    println("  je .Lfalse%d", label);
    gen_expr(node->lhs);
    println("  jmp .Lnext%d", label);
    println(".Lfalse%d:", label);
    gen_expr(node->rhs);
    println(".Lnext%d:", label);
    return;
  case ND_ASSIGN:
    gen_expr(node->rhs);
    gen_push("rax");
    println("  lea -%d(%%rbp), %%rdi", node->lhs->obj->offset);
    gen_store(node->lhs->obj->ty);
    return;
  case ND_VAR:
    println("  lea -%d(%%rbp), %%rax", node->obj->offset);
    gen_load(node->obj->ty);
    return;
  }
  default:
    break;
  }

  gen_expr(node->lhs);
  gen_push("rax");

  gen_expr(node->rhs);
  println("  mov %%rax, %%rdi");
  gen_pop("rax");

  switch (node->kind) {
  case ND_ADD:
    println("  add %%rdi, %%rax");
    break;
  case ND_SUB:
    println("  sub %%rdi, %%rax");
    break;
  case ND_MUL:
    println("  imul %%rdi, %%rax");
    break;
  case ND_DIV:
  case ND_MOD:
    println("  cqo");
    println("  idiv %%rdi");

    if (node->kind == ND_MOD) {
      println("  mov %%rdx, %%rax");
    }
    break;
  case ND_LSHIFT:
    println("  mov %%rdi, %%rcx");
    println("  sal %%cl, %%rax");
    break;
  case ND_RSHIFT:
    println("  mov %%rdi, %%rcx");
    println("  sar %%cl, %%rax");
    break;
  case ND_LCMP:
  case ND_LECMP:
  case ND_EQ:
  case ND_NEQ:
    println("  cmp %%rdi, %%rax");

    if (node->kind == ND_LCMP) {
      println("  setl %%al");
    } else if (node->kind == ND_LECMP) {
      println("  setle %%al");
    } else if (node->kind == ND_EQ) {
      println("  sete %%al");
    } else {
      println("  setne %%al");
    }

    println("  movzx %%al, %%rax");
    break;
  case ND_BITAND:
    println("  and %%rdi, %%rax");
    break;
  case ND_BITXOR:
    println("  xor %%rdi, %%rax");
    break;
  case ND_BITOR:
    println("  or %%rdi, %%rax");
    break;
  case ND_LOGAND:
    println("  cmp $0, %%rax");
    println("  je 1f");
    println("  cmp $0, %%rdi");
    println("  je 1f");
    println("  mov $1, %%rax");
    println("  jmp 2f");
    println("1:");
    println("  mov $0, %%rax");
    println("2:");
    break;
  case ND_LOGOR:
    println("  cmp $0, %%rax");
    println("  jne 1f");
    println("  cmp $0, %%rdi");
    println("  je 2f");
    println("1:");
    println("  mov $1, %%rax");
    println("  jmp 3f");
    println("2:");
    println("  mov $0, %%rax");
    println("3:");
    break;
  default:
    break;
  }
}

void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_BLOCK:
    for (Node *stmt = node->lhs; stmt != NULL; stmt = stmt->next) {
      gen_stmt(stmt);
    }
    break;
  case ND_RETURN:
    gen_expr(node->lhs);
    println("  mov %%rbp, %%rsp");
    gen_pop("rbp");
    println("  ret");
    break;
  default:
    gen_expr(node);
  }
}

void asmgen(Node *node, char *filename) {
  target_file = fopen(filename, "w");
  Obj *func = node->obj;

  println(".globl main");
  println(".text");
  println(".type %s, @function", func->name);
  println("%s:", func->name);

  // Prologue
  gen_push("rbp");
  println("  mov %%rsp, %%rbp");
  println("  sub $%d, %%rsp", func->offset);

  gen_stmt(node->lhs);

  println("  mov %%rbp, %%rsp");
  gen_pop("rbp");
  println("  ret");

  fclose(target_file);
}
