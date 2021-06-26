#include "code/codegen.h"
#include "parser/parser.h"
#include "variable/variable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// About assembly
//

const char *reg_8byte[] = {
  "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "QWORD PTR [rax]"
};

const char *reg_4byte[] = {
  "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
  "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
  "DWORD PTR [rax]"
};

const char *reg_2byte[] = {
  "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
  "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
  "WORD PTR [rax]"
};

const char *reg_1byte[] = {
  "al", "bl", "cl", "dl", "sil", "dil", "bpl", "spl",
  "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
  "BYTE PTR [rax]"
};

const char *get_reg(RegKind reg_kind, RegSizeKind reg_size) {
  switch (reg_size) {
    case REG_SIZE_1:
      return reg_1byte[reg_kind];
    case REG_SIZE_2:
      return reg_2byte[reg_kind];
    case REG_SIZE_4:
      return reg_4byte[reg_kind];
    case REG_SIZE_8:
      return reg_8byte[reg_kind];
  };
}

RegSizeKind convert_type_to_size(Type *var_type) {
  switch (var_type->kind) {
    case TY_CHAR:
      return REG_SIZE_1;
    case TY_INT:
      return REG_SIZE_4;
    case TY_LONG:
      return REG_SIZE_8;
    default:
      return REG_SIZE_8;
  }
}

void gen_compare(char *comp_op, Type *var_type) {
  printf("  cmp %s, %s\n", 
      get_reg(REG_RAX, convert_type_to_size(var_type)),
      get_reg(REG_RDI, convert_type_to_size(var_type)));
  printf("  %s al\n", comp_op);
  printf("  movzx rax, al\n");
}


void gen_var_address(Node *node) {
  if (node->kind != ND_VAR && node->kind != ND_ADDR) {
    errorf(ER_COMPILE, "Not variable");
  }
  Var *var = node->var;

  if (var->global) {
    char *var_name = calloc(var->len + 1, sizeof(char));
    memcpy(var_name, var->str, var->len);
    printf("  mov rax, offset %s\n", var_name);
  } else {
    printf("  mov rax, rbp\n");
    printf("  sub rax, %d\n", node->var->offset);
  }
  printf("  push rax\n");
}

// OP_MOV: left_reg = right_reg
// OP_ADD: left_reg = left_reg + right_reg
// OP_SUB: left_reg = left_reg - right_reg
// OP_MUL: left_reg = left_reg * right_reg (Cannot use REG_MEM both left_reg and right_reg)
// OP_DIV: left_reg = left_reg / right_reg (Overwrite rax and rdx registers)
// OP_REMAINDER: left_reg = left_reg % right_reg (Overwrite rax and rdx registers)
// OP_BITWISE_SHIFT_LEFT: left_reg = left_reg << right_reg (Overwrite rcx register)
// OP_BITWISE_SHIFT_RIGHT: left_reg = left_reg >> right_reg (overwrite rcx register)
// OP_BITWISE_(AND | XOR | OR | NOT): left_reg = left_reg = (and | xor | or | not) right_reg
bool gen_operation(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size, OpKind op) {

  // normal operation
  switch (op) {
    case OP_MOV:
      printf("  mov %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_ADD:
      printf("  add %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_SUB:
      printf("  sub %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_MUL: {
      if (left_reg == REG_MEM) {
        return false;
      }
      if (reg_size == REG_SIZE_1) {
        reg_size = REG_SIZE_2;
      }
      printf("  imul %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    }
    case OP_DIV:
    case OP_REMAINDER: {
      if (left_reg == REG_MEM) {
        printf("  push %s\n", get_reg(REG_RAX, REG_SIZE_8));
      }
      if (left_reg != REG_RAX) {
        gen_operation(REG_RAX, left_reg, reg_size, OP_MOV);
      }
      if (reg_size == REG_SIZE_8) {
        printf("  cqo\n");
      } else {
        printf("  cdq\n");
      }
      printf("  idiv %s\n", get_reg(right_reg, reg_size));
      if (op == OP_DIV) {
        gen_operation(REG_RDX, REG_RAX, reg_size, OP_MOV);
      }
      if (left_reg == REG_MEM) {
        printf("  pop %s\n", get_reg(REG_RAX, REG_SIZE_8));
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
        printf("  sal %s, %s\n", get_reg(left_reg, reg_size), get_reg(REG_RCX, reg_size));
      } else {
        printf("  sar %s, %s\n", get_reg(left_reg, reg_size), get_reg(REG_RCX, reg_size));
      }
      return true;
    }
    case OP_BITWISE_AND:
      printf("  and %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_XOR:
      printf("  xor %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_OR:
      printf("  or %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
      return true;
    case OP_BITWISE_NOT:
      printf("  not %s\n", get_reg(left_reg, reg_size));
      return true;
  }
  return false;
}

int label = 0;

const RegKind args_reg[] = {
  REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9
};

void compile_node(Node *node);
void expand_logical_and(Node *node, int label);
void expand_logical_or(Node *node, int label);

void expand_variable(Node *node) {
  gen_var_address(node);
  printf("  pop rax\n");
  Type *var_type = node->var->var_type;
  if (var_type->kind != TY_ARRAY) {
    gen_operation(REG_RAX, REG_MEM, convert_type_to_size(var_type), OP_MOV);
  }
  printf("  push rax\n");
}

// If left node is a direct variable, get the address of the variable.
// If left node is a indirect, get original address.
void gen_assignable_address(Node *node) {
  if (node->kind == ND_VAR) {
    gen_var_address(node);
  } else if (node->kind == ND_CONTENT) {
    compile_node(node->lhs);
  } else {
    errorf(ER_COMPILE, "Cannot assign");
  }
}

void expand_assign(Node *node) {
  // The left node must be assignable.
  gen_assignable_address(node->lhs);
  switch (node->rhs->kind) {
    case ND_ASSIGN:
      expand_assign(node->rhs);
      break;
    default:
      compile_node(node->rhs);
      printf("  pop rdi\n");
  }
  printf("  pop rax\n");
  RegSizeKind type_size = convert_type_to_size(node->lhs->equation_type);

  switch (node->assign_type) {
    case ND_ADD: {
      gen_operation(REG_RDI, REG_MEM, type_size, OP_ADD);
      break;
    }
    case ND_SUB: {
      printf("  push rax\n");
      gen_operation(REG_RAX, REG_MEM, type_size, OP_MOV);
      gen_operation(REG_RAX, REG_RDI, type_size, OP_SUB);
      gen_operation(REG_RDI, REG_RAX, type_size, OP_MOV);
      printf("  pop rax\n");
      break;
    }
    case ND_MUL: {
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MUL);
      break;
    }
    case ND_DIV:
    case ND_REMAINDER: {
      printf("  push rax\n");
      if (node->assign_type == ND_DIV) {
        gen_operation(REG_MEM, REG_RDI, type_size, OP_DIV);
      } else {
        gen_operation(REG_MEM, REG_RDI, type_size, OP_REMAINDER);
      }
      printf("  pop rax\n");
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    }
    case ND_LEFTSHIFT:
      gen_operation(REG_MEM, REG_RDI, type_size, OP_LEFT_SHIFT);
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    case ND_RIGHTSHIFT:
      gen_operation(REG_MEM, REG_RDI, type_size, OP_RIGHT_SHIFT);
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    case ND_BITWISEAND:
      gen_operation(REG_MEM, REG_RDI, type_size, OP_BITWISE_AND);
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    case ND_BITWISEXOR:
      gen_operation(REG_MEM, REG_RDI, type_size, OP_BITWISE_XOR);
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    case ND_BITWISEOR:
      gen_operation(REG_MEM, REG_RDI, type_size, OP_BITWISE_OR);
      gen_operation(REG_RDI, REG_MEM, type_size, OP_MOV);
      break;
    default:
      break;
  }
  gen_operation(REG_MEM, REG_RDI, type_size, OP_MOV);
}

void expand_logical_and(Node *node, int label) {
  compile_node(node->lhs);
  printf("  pop rax\n");
  printf("  cmp rax, 0\n");
  printf("  je .Lfalse%d\n", label);

  if (node->rhs && node->rhs->kind == ND_LOGICALAND) {
    expand_logical_and(node->rhs, label);
  } else {
    compile_node(node->rhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  je .Lfalse%d\n", label);
  }
}

void expand_logical_or(Node *node, int label) {
  compile_node(node->lhs);
  printf("  pop rax\n");
  printf("  cmp rax, 0\n");
  printf("  jne .Ltrue%d\n", label);

  if (node->rhs && node->rhs->kind == ND_LOGICALAND) {
    expand_logical_or(node->rhs, label);
  } else {
    compile_node(node->rhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  jne .Ltrue%d\n", label);
  }
}

void expand_ternary(Node *node, int label) {
  compile_node(node->exec_if);
  printf("  pop rax\n");
  printf("  cmp rax, 0\n");
  printf("  je .Lfalse%d\n", label);

  compile_node(node->lhs);
  printf("  jmp .Lnext%d\n", label);
  printf(".Lfalse%d:\n", label);

  compile_node(node->rhs);
  printf(".Lnext%d:\n", label);
}

void compile_node(Node *node) {
  if (node->kind == ND_INT) {
    printf("  push %d\n", node->val);
    return;
  }

  if (node->kind == ND_BLOCK) {
    for (Node *now_stmt = node->next_block; now_stmt;
         now_stmt = now_stmt->next_stmt) {
      compile_node(now_stmt);
    }
    return;
  }

  switch (node->kind) {
    case ND_VAR:
      expand_variable(node);
      return;
    case ND_ADDR:
      if (node->lhs->kind == ND_VAR) {
        gen_var_address(node->lhs);
      } else if (node->lhs->kind == ND_CONTENT) {
        compile_node(node->lhs->lhs);
      }
      return;
    case ND_ASSIGN:
      expand_assign(node);
      return;
    case ND_RETURN:
      compile_node(node->lhs);
      printf("  pop rax\n");
      printf("  mov rsp, rbp\n");
      printf("  pop rbp\n");
      printf("  ret\n");
      return;
    case ND_IF: {
      int now_label = label++;
      compile_node(node->judge);
      printf("  pop rax\n");
      printf("  cmp rax, 0\n");
      printf("  je .Lelse%d\n", now_label);

      // "true"
      compile_node(node->exec_if);
      printf("  jmp .Lend%d\n", now_label);

      printf(".Lelse%d:\n", now_label);
      // "else" statement
      if (node->exec_else) {
        compile_node(node->exec_else);
      }

      // continue
      printf(".Lend%d:\n", now_label);
      return;
    }
    case ND_TERNARY: {
      int now_label = label++;
      expand_ternary(node, label);
      return;
    }
    case ND_WHILE:
    case ND_FOR: {
      int now_label = label++;
      node->label = now_label;
      if (node->init_for) {
        compile_node(node->init_for);
      }

      printf(".Lbegin%d:\n", now_label);

      // judege expr
      if (node->judge) {
        compile_node(node->judge);
        printf("  pop rax\n");
        printf("  cmp rax, 0\n");
        printf("  je .Lend%d\n", now_label);
      }

      compile_node(node->stmt_for);

      // repeat expr
      if (node->repeat_for) {
        compile_node(node->repeat_for);
      }

      // finally
      printf("  jmp .Lbegin%d\n", now_label);
      printf(".Lend%d:\n", now_label);
      return;
    }
    case ND_LOOPBREAK: {
      int now_label = node->lhs->label;
      printf("  jmp .Lend%d\n", now_label);
      return;
    }
    case ND_CONTINUE: {
      int now_label = node->lhs->label;
      printf("  jmp .Lbegin%d\n", now_label);
      return;
    }
    case ND_CONTENT: {
      compile_node(node->lhs);
      if (node->equation_type->kind != TY_ARRAY) {
        printf("  pop rax\n");
        printf("  mov rax, QWORD PTR [rax]\n");
        printf("  push rax\n");
      }
      return;
    }
    case ND_LOGICALAND: {
      int now_label = label++;
      expand_logical_and(node, now_label);
      printf("  mov rax, 1\n");
      printf("  jmp .Lnext%d\n", now_label);
      printf(".Lfalse%d:\n", now_label);
      printf("  mov rax, 0\n");
      printf(".Lnext%d:\n", now_label);
      printf("  push rax\n");
      return;
    }
    case ND_LOGICALOR: {
      int now_label = label++;
      expand_logical_or(node, now_label);
      printf("  mov rax, 0\n");
      printf("  jmp .Lnext%d\n", now_label);
      printf(".Ltrue%d:\n", now_label);
      printf("  mov rax, 1\n");
      printf(".Lnext%d:\n", now_label);
      printf("  push rax\n");
      return;
    }
    case ND_PREFIX_INC:
    case ND_PREFIX_DEC: {
      gen_assignable_address(node->lhs);
      printf("  mov rdi, 1\n");
      printf("  pop rax\n");
      if (node->kind == ND_PREFIX_INC) {
        gen_operation(REG_MEM, REG_RDI, convert_type_to_size(node->lhs->var->var_type), OP_ADD);
      } else {
        gen_operation(REG_MEM, REG_RDI, convert_type_to_size(node->lhs->var->var_type), OP_SUB);
      }
      compile_node(node->lhs);
      return;
    }
    case ND_SUFFIX_INC:
    case ND_SUFFIX_DEC: {
      compile_node(node->lhs);
      gen_assignable_address(node->lhs);
      printf("  mov rdi, 1\n");
      printf("  pop rax\n");
      if (node->kind == ND_SUFFIX_INC) {
        gen_operation(REG_MEM, REG_RDI, convert_type_to_size(node->lhs->var->var_type), OP_ADD);
      } else {
        gen_operation(REG_MEM, REG_RDI, convert_type_to_size(node->lhs->var->var_type), OP_SUB);
      }
      return;
    }
    case ND_BITWISENOT: {
      compile_node(node->lhs);
      printf("  pop rax\n");
      gen_operation(REG_RAX, REG_RAX, convert_type_to_size(node->lhs->var->var_type), OP_BITWISE_NOT);
      printf("  push rax\n");
      return;
    }
    case ND_LOGICALNOT: {
      compile_node(node->lhs);
      printf("  pop rax\n");
      printf("  mov rdi, 1\n");
      gen_operation(REG_RAX, REG_RDI, REG_SIZE_8, OP_BITWISE_XOR);
      printf("  push rax\n");
      return;
    }
    case ND_SIZEOF: {
      printf("  mov rax, %d\n", get_sizeof(node->var));
      printf("  push rax\n");
      return;
    }
    default:
      break;
  }

  if (node->kind == ND_FUNCCALL) {
    char *name = calloc(node->func_name_len + 1, sizeof(char));
    memcpy(name, node->func_name, node->func_name_len);
    int arg_count = 0;
    for (Node *now_arg = node->func_arg; now_arg; now_arg = now_arg->func_arg) {
      compile_node(now_arg);
      arg_count++;
    }

    for (int arg_idx = 0; arg_idx < arg_count && arg_idx < 6; arg_idx++) {
      printf("  pop %s\n", get_reg(args_reg[arg_idx], REG_SIZE_8));
    }

    printf("  call %s\n", name);
    printf("  push rax\n");
    return;
  }

  compile_node(node->lhs);
  compile_node(node->rhs);

  printf("  pop rdi\n");
  printf("  pop rax\n");

  Type *formula_type;
  if (node->lhs->var) {
    formula_type = node->lhs->var->var_type;
  } else {
    formula_type = calloc(1, sizeof(Type));
    formula_type->kind = TY_INT;
  }

  if (node->lhs->var && pointer_movement_size(node->lhs->var) != 1) {
    printf("  imul rdi, %d\n", pointer_movement_size(node->lhs->var));
  }

  if (node->rhs->var && pointer_movement_size(node->rhs->var) != 1) {
    printf("  imul rax, %d\n", pointer_movement_size(node->rhs->var));
  }

  RegSizeKind type_size = convert_type_to_size(formula_type);

  // calculation
  switch (node->kind) {
    case ND_ADD:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_ADD);
      break;
    case ND_SUB:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_SUB);
      break;
    case ND_MUL:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_MUL);
      break;
    case ND_DIV:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_DIV);
      break;
    case ND_REMAINDER:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_REMAINDER);
      break;
    case ND_LEFTSHIFT:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_LEFT_SHIFT);
      break;
    case ND_RIGHTSHIFT:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_RIGHT_SHIFT);
      break;
    case ND_BITWISEAND:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_BITWISE_AND);
      break;
    case ND_BITWISEXOR:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_BITWISE_XOR);
      break;
    case ND_BITWISEOR:
      gen_operation(REG_RAX, REG_RDI, type_size, OP_BITWISE_OR);
      break;
    case ND_EQ:
      gen_compare("sete", formula_type);
      break;
    case ND_NEQ:
      gen_compare("setne", formula_type);
      break;
    case ND_LC:
      gen_compare("setl", formula_type);
      break;
    case ND_LEC:
      gen_compare("setle", formula_type);
      break;
    case ND_RC:
      gen_compare("setg", formula_type);
      break;
    case ND_REC:
      gen_compare("setge", formula_type);
      break;
    default:
      break;
  }

  printf("  push rax\n");
}

void gen_global_var_define(Var *var) {
  char *global_var_name = calloc(var->len + 1, sizeof(char));
  memcpy(global_var_name, var->str, var->len);
  printf(".data\n");
  printf("%s:\n", global_var_name);
  switch (var->var_type->kind) {
    case TY_CHAR:
      printf("  .zero 1\n");
      break;
    case TY_INT:
      printf("  .zero 4\n");
      break;
    case TY_LONG:
    case TY_PTR:
      printf("  .zero %d\n", var->var_type->var_size);
      break;
    default:
      return;
  }
}

void codegen() {
  printf(".intel_syntax noprefix\n");

  for (Function *now_func = top_func; now_func; now_func = now_func->next) {
    if (now_func->global_var_define) {
      for (Node *define_node = now_func->stmt; define_node != NULL; define_node = define_node->next_stmt) {
        gen_global_var_define(define_node->var);
      }
      continue;
    }


    char *func_name = calloc(now_func->func_name_len + 1, sizeof(char));
    memcpy(func_name, now_func->func_name, now_func->func_name_len);
    printf(".global %s\n", func_name);
    printf(".text\n");
    printf("%s:\n", func_name);

    // Prologue
    // Allocate variable size.
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", now_func->vars_size);

    // Set arguments (use register)
    int arg_count = now_func->func_argc - 1;
    for (Node *arg = now_func->func_args; arg; arg = arg->lhs) {
      if (arg_count < 6) {
        gen_var_address(arg);
        printf("  pop rax\n");
        gen_operation(REG_MEM, args_reg[arg_count], convert_type_to_size(arg->var->var_type), OP_MOV);
      }
      arg_count--;
    }

    // Set arguements (use stack due more than 7 arguments)
    arg_count = now_func->func_argc - 1;
    for (Node *arg = now_func->func_args; arg; arg = arg->lhs) {
      if (arg_count >= 6) {
        gen_var_address(arg);
        printf("  mov rax, [rbp + %d]\n", 8 + (arg_count - 5) * 8);
        printf("  mov rdi, rax\n");
        printf("  pop rax\n");
        gen_operation(REG_MEM, REG_RDI, convert_type_to_size(arg->var->var_type), OP_MOV);
      }
      arg_count--;
    }

    compile_node(now_func->stmt);

    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");
    printf("  ret \n");
  }
}