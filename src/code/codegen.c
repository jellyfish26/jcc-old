#include "code/codegen.h"
#include "parser/parser.h"
#include "token/tokenize.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *output_file;

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  fprintf(output_file, "\n");
  va_end(ap);
}

//
// About assembly
//

void compile_node(Node *node);
bool gen_operation(RegKind left_reg, RegKind right_reg, int reg_size, OpKind op);
int get_type_size(Type *type);

static const char *reg_8byte[] = {
  "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "QWORD PTR [rax]"
};

static const char *reg_4byte[] = {
  "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
  "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
  "DWORD PTR [rax]"
};

static const char *reg_2byte[] = {
  "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
  "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
  "WORD PTR [rax]"
};

static const char *reg_1byte[] = {
  "al", "bl", "cl", "dl", "sil", "dil", "bpl", "spl",
  "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
  "BYTE PTR [rax]"
};

static void gen_push(const char *reg) {
  println("  push %s", reg);
}

static void gen_pop(const char *reg) {
  println("  pop %s", reg);
}

static void gen_emptypop(int num) {
  println("  add rsp, %d", num * 8);
}

// Compute the address of a given node.
// In the case of a local variable, it computes the relative address to the base pointer,
// and stores the absolute address in the RAX register.
void gen_addr(Node *node) {
  switch (node->kind) {
    case ND_VAR:
      // String literal
      if (node->use_var->type->kind == TY_STR) {
        println("  mov rax, offset .LC%d", node->use_var->offset);
        return;
      }

      if (node->use_var->is_global) {
        println("  mov rax, offset %s", node->use_var->name);
        return;
      }

      println("  mov rax, rbp");
      println("  sub rax, %d", node->use_var->offset);
      return;
    case ND_CONTENT:
      compile_node(node->lhs);
      return;
    default:
      errorf_tkn(ER_COMPILE, node->tkn, "Not a variable.");
  }
}

static void gen_load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STR) {
    // If it is an array or a string, it will automatically be treated as a pointer
    // and we cannot load the content direclty.
    return;
  }

  char *unsi = ty->is_unsigned ? "movz" : "movs";

  // When char and short size values are loaded with mov instructions,
  // they may contain garbage in the lower 32bits,
  // so we are always extended to int.
  if (ty->var_size == 1) {
    println("  %sx eax, BYTE PTR [rax]", unsi);
  } else if (ty->var_size == 2) {
    println("  %sx eax, WORD PTR [rax]", unsi);
  } else if (ty->var_size == 4) {
    println("  mov eax, DWORD PTR [rax]");
  } else {
    println("  mov rax, QWORD PTR [rax]");
  }
}

// Store the value of the rax register at the address pointed to by the top of the stack.
static void gen_store(Type *ty) {
  gen_pop("rdi");

  if (ty->var_size == 1) {
    println("  mov BYTE PTR [rdi], al");
  } else if (ty->var_size == 2) {
    println("  mov WORD PTR [rdi], ax");
  } else if (ty->var_size == 4) {
    println("  mov DWORD PTR [rdi], eax");
  } else {
    println("  mov QWORD PTR [rdi], rax");
  }
}

static const char *get_reg(RegKind reg, int reg_size) {
  switch (reg_size) {
    case 1:
      return reg_1byte[reg];
    case 2:
      return reg_2byte[reg];
    case 4:
      return reg_4byte[reg];
    case 8:
      return reg_8byte[reg];
    default:
      return reg_8byte[reg];
  }
}

int get_type_size(Type *type) {
  switch (type->kind) {
    case TY_CHAR:
      return 1;
    case TY_SHORT:
      return 2;
    case TY_INT:
      return 4;
    case TY_LONG:
      return 8;
    default:
      return 8;
  }
}

static const char *args_reg[] = {
  "rdi", "rsi", "rdx", "rcx", "r8", "r9",
};

static RegKind regacy_args_reg[] = {
  REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9,
};

static char i32i8[]  = "movsx eax, al";
static char i32u8[]  = "movzx eax, al";
static char i32i16[] = "movsx eax, ax";
static char i32u16[] = "movzx eax, ax";
static char i64i32[] = "movsxd rax, eax";
static char i64u32[] = "mov eax, eax";

static char *cast_table[][8] = {
// i8     i16     i32   i64     u8     u16     u32   u64      to/from
  {NULL,  NULL,   NULL, i64i32, i32u8, i32u16, NULL, i64i32}, // i8
  {i32i8, NULL,   NULL, i64i32, i32u8, i32u16, NULL, i64i32}, // i16
  {i32i8, i32i16, NULL, i64i32, i32u8, i32u16, NULL, i64i32}, // i32
  {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // i64

  {i32i8, NULL,   NULL, i64i32, i32u8, NULL,   NULL, i64i32}, // u8
  {i32i8, i32i16, NULL, i64i32, i32u8, NULL,   NULL, i64i32}, // u16
  {i32i8, i32i16, NULL, i64u32, i32u8, i32u16, NULL, i64u32}, // u32
  {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // u64
};

static int type_to_cast_table_idx(Type *type) {
  int ret;
  switch (type->kind) {
    case TY_CHAR:
      ret = 0;
      break;
    case TY_SHORT:
      ret = 1;
      break;
    case TY_INT:
      ret = 2;
      break;
    case TY_LONG:
      ret = 3;
      break;
    default:
      return 0;
  };
  if (type->is_unsigned) {
    ret += 4;
  }
  return ret;
}

static void gen_cast(Node *node) {
  if (node->kind != ND_CAST) {
    return;
  }

  compile_node(node->lhs);
  int from = type_to_cast_table_idx(node->lhs->type);
  int to = type_to_cast_table_idx(node->type);
  if (cast_table[from][to] != NULL) {
    println("  %s", cast_table[from][to]);
  }
}

static void gen_comp(char *comp_op, char *lreg, char *rreg) {
  println("  cmp %s, %s", lreg, rreg);
  println("  %s al", comp_op);
  println("  movzx rax, al");
}


// OP_MOV: left_reg = right_reg
// OP_MOVSX: left_reg = right_reg (Move with Sign-Extension, Size of left_reg is REG_SIZE_4)
// OP_ADD: left_reg = left_reg + right_reg
// OP_SUB: left_reg = left_reg - right_reg
// OP_MUL: left_reg = left_reg * right_reg (Cannot use REG_MEM both left_reg and right_reg)
// OP_DIV: left_reg = left_reg / right_reg (Overwrite rax and rdx registers)
// OP_REMAINDER: left_reg = left_reg % right_reg (Overwrite rax and rdx registers)
// OP_BITWISE_SHIFT_LEFT: left_reg = left_reg << right_reg (Overwrite rcx register)
// OP_BITWISE_SHIFT_RIGHT: left_reg = left_reg >> right_reg (overwrite rcx register)
// OP_BITWISE_(AND | XOR | OR | NOT): left_reg = left_reg = (and | xor | or | not) right_reg
bool gen_operation(RegKind left_reg, RegKind right_reg, int reg_size, OpKind op) {

  // normal operation
  switch (op) {
    case OP_MOV:
      if (left_reg == REG_MEM || reg_size >= 4) {
        println("  mov %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      } else {
        println("  movzx %s, %s", get_reg(left_reg, 8), get_reg(right_reg, reg_size));
      }
      return true;
    case OP_MOVSX:
      println("  movsx %s, %s", get_reg(left_reg, 4), get_reg(right_reg, reg_size));
      return true;
    case OP_ADD:
      println("  add %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_SUB:
      println("  sub %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_MUL: {
      if (reg_size == 1) {
        reg_size = 2;
      }
      println("  imul %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    }
    case OP_DIV:
    case OP_REMAINDER: {
      if (left_reg == REG_MEM) {
        gen_push("rax");
      }
      if (reg_size <= 2) {
        gen_operation(REG_RAX, left_reg, reg_size, OP_MOVSX);
      } else if (left_reg != REG_RAX) {
        gen_operation(REG_RAX, left_reg, reg_size, OP_MOV);
      }
      if (reg_size == 8) {
        println("  cqo");
      } else {
        println("  cdq");
      }
      if (reg_size <= 4) {
        println("  idiv %s", get_reg(right_reg, 4));
      } else {
        println("  idiv %s", get_reg(right_reg, reg_size));
      }
      if (op == OP_DIV) {
        gen_operation(REG_RDX, REG_RAX, 8, OP_MOV);
      }
      if (left_reg == REG_MEM) {
        gen_pop("rax");
      }
      gen_operation(left_reg, REG_RDX, reg_size, OP_MOV);
      return true;
    }
    case OP_LEFT_SHIFT:
    case OP_RIGHT_SHIFT: {
      if (right_reg != REG_RCX) {
        gen_operation(REG_RCX, right_reg, reg_size, OP_MOV);
      }
      if (op == OP_LEFT_SHIFT) {
        println("  sal %s, %s", get_reg(left_reg, reg_size), get_reg(REG_RCX, reg_size));
      } else {
        println("  sar %s, %s", get_reg(left_reg, reg_size), get_reg(REG_RCX, reg_size));
      }
      return true;
    }
    case OP_BITWISE_AND:
      println("  and %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_XOR:
      println("  xor %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_OR:
      println("  or %s, %s", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_NOT:
      println("  not %s", get_reg(left_reg, reg_size));
      return true;
  }
  return false;
}

int branch_label = 0;


// Right to left
void expand_assign(Node *node) {
  gen_addr(node->lhs);
  gen_push("rax");

  compile_node(node->rhs);
  gen_store(node->type);
}

static void gen_logical(Node *node, int label) {
  compile_node(node->lhs);
  println("  cmp rax, 0");

  if (node->kind == ND_LOGICALAND) {
    println("  je .Lfalse%d", label);
  } else {
    println("  jne .Ltrue%d", label);
  }

  compile_node(node->rhs);
  println("  cmp rax, 0");
  println("  je .Lfalse%d", label);

  println(".Ltrue%d:", label);
  println("  mov rax, 1");
  println("  jmp .Lthen%d", label);
  println(".Lfalse%d:", label);
  println("  mov rax, 0");
  println(".Lthen%d:", label);
}

void expand_ternary(Node *node, int label) {
  compile_node(node->cond);
  println("  cmp rax, 0");
  println("  je .Lfalse%d", label);

  compile_node(node->lhs);
  println("  jmp .Lnext%d", label);
  println(".Lfalse%d:", label);

  compile_node(node->rhs);
  println(".Lnext%d:", label);
}

static void gen_lvar_init(Node *node) {
  gen_addr(node->lhs);

  for (Node *init = node->rhs; init != NULL; init = init->lhs) {
    gen_push("rax");
    if (init->init != NULL) {
      compile_node(init->init);
    } else {
      println("  mov rax, 0");
    }

    gen_store(node->type);
    println("  mov rax, rdi");
    println("  add rax, %d", node->type->var_size);
  }
}

static void gen_gvar_init(Node *node) {
  println(".data");
  println("%s:", node->lhs->use_var->name);

  char *asm_ty;
  switch (node->type->kind) {
    case TY_CHAR:
      asm_ty = ".byte";
      break;
    case TY_SHORT:
      asm_ty = ".short";
      break;
    case TY_INT:
      asm_ty = ".long";
      break;
    case TY_LONG:
    case TY_PTR:
      asm_ty = ".quad";
      break;
    default:
      return;
  }

  for (Node *init = node->rhs; init != NULL; init = init->lhs) {
    if (init->init == NULL) {
      println("  %s 0", asm_ty);
      continue;
    }

    char *ptr_label = init->init->use_var->name;
    if (ptr_label == NULL) {
      println("  %s %d", asm_ty, init->init->val);
    } else {
      println("  %s %s%+d", asm_ty, ptr_label, init->init->val);
    }
  }
}

static void gen_gvar_define(Obj *var) {
  println(".data");
  if (var->type->kind == TY_STR) {
    println(".LC%d:", var->offset);
    println("  .string \"%s\"", var->name);
    return;
  }
  println("%s:", var->name);
  switch (var->type->kind) {
    case TY_CHAR:
      println("  .zero 1");
      break;
    case TY_SHORT:
      println("  .zero 2");
      break;
    case TY_INT:
      println("  .zero 4");
      break;
    case TY_LONG:
    case TY_PTR:
    case TY_ARRAY:
      println("  .zero %d", var->type->var_size);
      break;
    default:
      return;
  }
}

void compile_node(Node *node) {
  if (node->kind == ND_INT) {
    println("  mov rax, %ld", node->val);
    return;
  }

  if (node->kind == ND_CAST) {
    gen_cast(node);
    return;
  } 

  if (node->kind == ND_BLOCK) {
    for (Node *now_stmt = node->next_block; now_stmt;
         now_stmt = now_stmt->next_stmt) {
      compile_node(now_stmt);
    }
    return;
  }

  if (node->kind == ND_INIT) {
    gen_addr(node->lhs);
    gen_lvar_init(node);
    return;
  }

  switch (node->kind) {
    case ND_VAR:
      gen_addr(node);
      gen_load(node->type);
      return;
    case ND_ADDR:
      gen_addr(node->lhs);
      return;
    case ND_ASSIGN:
      expand_assign(node);
      return;
    case ND_RETURN:
      compile_node(node->lhs);
      println("  mov rsp, rbp");
      gen_pop("rbp");
      println("  ret");
      return;
    case ND_IF: {
      int now_label = branch_label++;
      compile_node(node->cond);
      println("  cmp rax, 0");
      println("  je .Lelse%d", now_label);

      // "true"
      compile_node(node->then);
      println("  jmp .Lend%d", now_label);

      println(".Lelse%d:", now_label);
      // "else" statement
      if (node->other != NULL) {
        compile_node(node->other);
      }

      // continue
      println(".Lend%d:", now_label);
      return;
    }
    case ND_TERNARY: {
      int now_label = branch_label++;
      expand_ternary(node, branch_label);
      return;
    }
    case ND_WHILE:
    case ND_FOR: {
      int now_label = branch_label++;
      node->label = now_label;
      if (node->init) {
        compile_node(node->init);
      }

      println(".Lbegin%d:", now_label);

      // judege expr
      if (node->cond != NULL) {
        compile_node(node->cond);
        println("  cmp rax, 0");
        println("  je .Lend%d", now_label);
      }

      compile_node(node->then);

      // repeat expr
      if (node->loop != NULL) {
        compile_node(node->loop);
      }

      // finally
      println("  jmp .Lbegin%d", now_label);
      println(".Lend%d:", now_label);
      return;
    }
    case ND_LOOPBREAK: {
      int now_label = node->lhs->label;
      println("  jmp .Lend%d", now_label);
      return;
    }
    case ND_CONTINUE: {
      int now_label = node->lhs->label;
      println("  jmp .Lbegin%d", now_label);
      return;
    }
    case ND_CONTENT: {
      compile_node(node->lhs);
      gen_load(node->type);
      return;
    }
    case ND_LOGICALAND:
    case ND_LOGICALOR: {
      gen_logical(node, branch_label++);
      return;
    }
    case ND_BITWISENOT: {
      compile_node(node->lhs);
      println("  not rax");
      return;
    }
    case ND_LOGICALNOT: {
      compile_node(node->lhs);
      println("  cmp rax, 0");
      println("  sete al");
      println("  movzx rax, al");
      return;
    }
    default:
      break;
  }

  if (node->kind == ND_FUNCCALL) {
    char *name = calloc(node->func->name_len + 1, sizeof(char));
    memcpy(name, node->func->name, node->func->name_len);
    int arg_count = 0;
    for (Node *now_arg = node->func->args; now_arg != NULL; now_arg = now_arg->next_stmt) {
      compile_node(now_arg);
      gen_push("rax");
      arg_count++;
    }

    for (int arg_idx = 0; arg_idx < arg_count && arg_idx < 6; arg_idx++) {
      gen_pop(args_reg[arg_idx]);
    }
    println("  call %s", name);
    if (arg_count > 6) {
      gen_emptypop(arg_count - 6);
    }
    return;
  }

  // lhs: rax, rhs: rdi
  compile_node(node->rhs);
  gen_push("rax");
  compile_node(node->lhs);
  gen_pop("rdi");

  // Default register is 32bit
  char *rax = "eax", *rdi = "edi", *rdx = "edx";

  if (node->lhs->type->kind == TY_LONG || node->lhs->type->base != NULL) {
    rax = "rax";
    rdi = "rdi";
    rdx = "rdx";
  }

  // calculation
  switch (node->kind) {
    case ND_ADD:
      println("  add %s, %s", rax, rdi);
      break;
    case ND_SUB:
      println("  sub %s, %s", rax, rdi);
      break;
    case ND_MUL:
      println("  imul %s, %s", rax, rdi);
      break;
    case ND_DIV:
    case ND_REMAINDER:
      if (node->type->is_unsigned) {
        println("  mov rdx, 0");
        println("  div %s", rdi);
      } else {
        if (node->lhs->type->var_size == 8) {
          println("  cqo");
        } else {
          println("  cdq");
        }
        println("  idiv %s", rdi);
      }

      if (node->kind == ND_REMAINDER) {
        println("  mov rax, rdx");
      }
      break;
    case ND_LEFTSHIFT:
      println("  mov rcx, rdi");
      println("  sal %s, cl", rax);
      break;
    case ND_RIGHTSHIFT:
      println("  mov rcx, rdi");
      if (node->lhs->type->is_unsigned) {
        println("  shr %s, cl", rax);
      } else {
        println("  sar %s, cl", rax);
      }
      break;
    case ND_BITWISEAND:
      println("  and %s, %s", rax, rdi);
      break;
    case ND_BITWISEXOR:
      println("  xor %s, %s", rax, rdi);
      break;
    case ND_BITWISEOR:
      println("  or %s, %s", rax, rdi);
      break;
    case ND_EQ:
      gen_comp("sete", rax, rdi);
      break;
    case ND_NEQ:
      gen_comp("setne", rax, rdi);
      break;
    case ND_LC:
      gen_comp("setl", rax, rdi);
      break;
    case ND_LEC:
      gen_comp("setle", rax, rdi);
      break;
    default:
      break;
  }
}


void codegen(Node *head, char *filename) {
  output_file = fopen(filename, "w");
  println(".intel_syntax noprefix");
  for (Obj *gvar = gvars; gvar != NULL; gvar = gvar->next) {
    // Only string literal
    if (gvar->type->kind == TY_STR) {
      gen_gvar_define(gvar);
    }
  }

  // Expand functions
  for (Node *node = head; node != NULL; node = node->next_block) {
    if (node->kind != ND_FUNC) {
      for (Node *var = node; var != NULL; var = var->next_stmt) {
        if (var->kind == ND_INIT) {
          gen_gvar_init(var);
        } else {
          gen_gvar_define(var->use_var);
        }
      }
      continue;
    }
    Obj *func = node->func;
    println(".global %s", func->name);
    println(".text");
    println("%s:", func->name);

    // Prologue
    gen_push("rbp");
    println("  mov rbp, rsp");
    println("  sub rsp, %d", func->vars_size);

    // Set arguments (use register);
    int argc = func->argc - 1;
    for (Node *arg = func->args; arg != NULL; arg = arg->lhs) {
      if (argc < 6) {
        gen_addr(arg);
        gen_operation(REG_MEM, regacy_args_reg[argc], get_type_size(arg->use_var->type), OP_MOV);
      }
      argc--;
    }

    // Set arguements (use stack due more than 7 arguments)
    argc = func->argc - 1;
    for (Node *arg = func->args; arg != NULL; arg = arg->lhs) {
      if (argc >= 6) {
        gen_addr(arg);
        gen_push("rax");
        println("  mov rax, QWORD PTR [rbp + %d]", 8 + (argc - 5) * 8);
        println("  mov rdi, rax");
        gen_pop("rax");
        gen_operation(REG_MEM, REG_RDI, get_type_size(arg->use_var->type), OP_MOV);
      }
      argc--;
    }

    compile_node(node->next_stmt);

    println("  mov rsp, rbp");
    gen_pop("rbp");
    println("  ret");
  }
  fclose(output_file);
}
