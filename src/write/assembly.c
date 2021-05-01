#include "write.h"
#include <stdio.h>

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

RegSizeKind convert_type_to_size(TypeKind var_type) {
  switch (var_type) {
    case TY_INT:
      return REG_SIZE_4;
    default:
      return REG_SIZE_8;
  };
}

void gen_compare(char *comp_op, TypeKind type_kind) {
  printf("  cmp %s, %s\n", 
      get_reg(REG_RAX, convert_type_to_size(type_kind)),
      get_reg(REG_RDI, convert_type_to_size(type_kind)));
  printf("  %s al\n", comp_op);
  printf("  movzx rax, al\n");
}

void gen_var_address(Node *node) {
  if (node->kind != ND_VAR && node->kind != ND_ADDR) {
    errorf(ER_COMPILE, "Not variable");
  }

  printf("  mov rax, rbp\n");
  printf("  sub rax, %d\n", node->var->offset);
  printf("  push rax\n");
}

// left_reg = right_reg
bool gen_instruction_mov(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size) {
  if (left_reg == REG_MEM && right_reg == REG_MEM) {
    return false;
  }
  printf("  mov %s, %s\n",
      get_reg(left_reg, reg_size),
      get_reg(right_reg, reg_size));
  return true;
}

// left_reg = left_reg + right_reg
bool gen_instruction_add(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size) {
  if (left_reg == REG_MEM && right_reg == REG_MEM) {
    return false;
  }
  printf("  add %s, %s\n",
      get_reg(left_reg, reg_size),
      get_reg(right_reg, reg_size));
  return true;
}

// left_reg = left_reg - right_reg
bool gen_instruction_sub(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size) {
  if (left_reg == REG_MEM && right_reg == REG_MEM) {
    return false;
  }

  printf("  sub %s, %s\n",
      get_reg(left_reg, reg_size),
      get_reg(right_reg, reg_size));
  return true;
}

// left_reg = left_reg * right_reg
bool gen_instruction_mul(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size) {
  if (left_reg == REG_MEM) {
    return false;
  }
  if (reg_size == REG_SIZE_1) {
    if (right_reg == REG_MEM) {
      return false;
    }
    reg_size = REG_SIZE_2;
  }

  printf("  imul %s, %s\n",
      get_reg(left_reg, reg_size),
      get_reg(right_reg, reg_size));
  return true;
}

// left_reg = left_reg / right_reg
// left_reg = left_reg % right_reg
// This function overwrites rax and rdx registers
bool gen_instruction_div(RegKind left_reg, RegKind right_reg, RegSizeKind reg_size, bool is_remainder) {
  if (left_reg == REG_MEM) {
    printf("  push %s\n", get_reg(REG_RAX, REG_SIZE_8));
  }
  if (left_reg != REG_RAX) {
    gen_instruction_mov(
        REG_RAX,
        left_reg,
        reg_size);
  }

  if (reg_size == REG_SIZE_8) {
    printf("  cqo\n");
  } else {
    printf("  cdq\n");
  }

  printf("  idiv %s\n", get_reg(right_reg, reg_size));
  if (!is_remainder) {
    gen_instruction_mov(REG_RDX, REG_RAX, reg_size);
  }

  if (left_reg == REG_MEM) {
    printf("  pop %s\n", get_reg(REG_RAX, REG_SIZE_8));
  }
  gen_instruction_mov(left_reg, REG_RDX, reg_size);
  return true;
}

// left_reg = left_reg << right_reg
// left_reg = left_reg >> right_reg
// This function overwrites rcx register
bool gen_instruction_bitwise_shift(RegKind left_reg,
                                   RegKind right_reg,
                                   RegSizeKind reg_size,
                                   bool shift_left) {
  if (right_reg != REG_RCX) {
    gen_instruction_mov(
        REG_RCX,
        right_reg,
        reg_size);
  }

  if (shift_left) {
    printf("  sal %s, %s\n", get_reg(left_reg, reg_size), get_reg(REG_RCX, REG_SIZE_1));
  } else {
    printf("  sar %s, %s\n", get_reg(left_reg, reg_size), get_reg(REG_RCX, REG_SIZE_1));
  }
  return true;
}

// left_reg = left_reg ("&" | "^" | "|") right_reg
// In the case of AND, operation is 001
// In the case of XOR, operation is 010
// In the case of OR, operation is 100
bool gen_instruction_bitwise_operation(RegKind left_reg,
                                       RegKind right_reg,
                                       RegSizeKind reg_size,
                                       int operation) {
  if (left_reg == REG_MEM && right_reg == REG_MEM) {
    return false;
  }

  if (operation == (1<<0)) {
    printf("  and %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
  } else if (operation == (1<<1)) {
    printf("  xor %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
  } else if (operation == (1<<2)) {
    printf("  or %s, %s\n", get_reg(left_reg, reg_size), get_reg(right_reg, reg_size));
  } else {
    return false;
  }
  return true;
}
