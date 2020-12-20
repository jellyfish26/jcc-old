#include "token.h"

int use_expect_int() {
    if (source_token->kind != TK_NUM_INT) {
        errorf_at(ER_COMPILE, source_token, "Not a number");
    }
    int val = source_token->val;
    source_token = source_token->next;
    return val;
}

bool use_symbol(char *op) {
    if (source_token->kind != TK_SYMBOL || 
        source_token->str_len != strlen(op) || 
        memcmp(source_token->str, op, source_token->str_len)) 
    {
        return false;
    }
    source_token = source_token->next;
    return true;
}

void use_expect_symbol(char *op) {
    if (source_token->kind != TK_SYMBOL || 
        source_token->str_len != strlen(op) || 
        memcmp(source_token->str, op, source_token->str_len)) 
    {
        errorf_at(ER_COMPILE, source_token, "Need \"%s\" operator", op);
    }
    source_token = source_token->next;
}