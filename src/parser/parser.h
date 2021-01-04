#include <stdlib.h>
#include <stdbool.h>

#include "../token/token.h"

//
// variable.c
// 

typedef enum {
    VR_INT  // int
} VarKind;

typedef struct Var Var;

struct Var {
    VarKind kind;
    Var *next;  // Next Var
    char *str;  // Variable name
    int len;    // Length of naem
    int size;   // Variable size
    int offset; // Offset
};

extern Var *vars;
extern int vars_size;

Var *add_var(VarKind kind, Var *target, char *str, int len);
Var *find_var(Token *target);
void init_offset();

//
// parse.c
//

typedef enum {
    ND_ADD,    // +
    ND_SUB,    // -
    ND_MUL,    // *
    ND_DIV,    // /
    ND_EQ,     // ==
    ND_NEQ,    // !=
    ND_LC,     // <  (Left Compare)
    ND_LEC,    // <= (Left Equal Compare)
    ND_RC,     // >  (Right Compare)
    ND_REC,    // >= (Right Equal Compare)
    ND_ASSIGN, // =
    ND_VAR,    // Variable
    ND_RETURN, // "return" statement
    ND_IF,     // "if" statement
    ND_ELSE,   // "else" statement
    ND_FOR,    // "for" statement
    ND_WHILE,  // "while" statement
    ND_BLOCK,  // Block statement
    ND_INT,    // Number (int)
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;  // Type of Node
    Node *lhs;      // Left side node
    Node *rhs;      // right side node

    Var *var;  // Variable type if kind is ND_VAR

    Node *judge;     // judge ("if" statement, "for" statement, "while" statement)
    Node *exec_if;   // exec ("if" statement)
    Node *exec_else; // exec ("else" statement)

    Node *init_for;   // init before exec "for"
    Node *repeat_for; // repeact in exec "for"
    Node *stmt_for;   // statement in exec "for"

    Node *next_stmt;  // Block statement

    int val;  // value if kind is ND_INT
};

extern Node *code[100];

void program();