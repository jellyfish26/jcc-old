#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

//
// tokenize.c
//

typedef enum {
    TK_NUM_INT,   // Number (Int)
    TK_SYMBOL,    // Symbol
    TK_EOF,       // End of File
} TokenKind;

typedef struct Token Token;

struct Token {
    TokenKind kind; // Type of Token
    Token *next;     // Next token
    char *str;       // Token String
    int str_len;     // Token length
    int val;         // Value if kind is TK_NUM_INT
};

extern Token *source_token;  // Warn: Don't operate it directly.
void *tokenize();

//
// error.c
//

typedef enum {
    ER_COMPILE,  // Compiler Error
    ER_TOKENIZE, // Tokenize Error
    ER_OTHER,    // Other Error
} ERROR_TYPE;

void errorf(ERROR_TYPE type, char *format, ...);
void errorf_at(ERROR_TYPE type, Token *token, char *fmt, ...);

//
// operate.c
//

int use_expect_int();
bool use_symbol(char *op);
void use_expect_symbol(char *op);