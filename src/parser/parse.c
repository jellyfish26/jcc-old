#include "parser.h"

Node *new_node(NodeKind kind, Node *lhs, Node *rhs) {
    Node *ret = calloc(1, sizeof(Node));
    ret->kind = kind;
    ret->lhs = lhs;
    ret->rhs = rhs;
    return ret;
}

Node *new_node_int(int val) {
    Node *ret = calloc(1, sizeof(Node));
    ret->kind = ND_INT;
    ret->val = val;
    return ret;
}

// Prototype
void program();
Node *statement();
Node *assign();
Node *same_comp();
Node *size_comp();
Node *add();
Node *mul();
Node *unary();
Node *priority();
Node *num();

Node *code[100];

void program() {
    int i = 0;
    while (!is_eof()) {
        code[i++] = statement();
    }
    code[i] = NULL;
}

// statement = ("return")? assign ";" |
//           "if" "(" assign ")" statement ("else" statement)?
Node *statement() {
    Node *ret;

    if (use_any_kind(TK_IF)) {
        ret = new_node(ND_IF, NULL, NULL);
        use_expect_symbol("(");
        ret->judge = assign();
        use_expect_symbol(")");
        ret->exec_if = statement();
        if (use_any_kind(TK_ELSE)) {
            ret->exec_else = statement();
        }
        return ret;
    }

    if (use_any_kind(TK_RETURN)) {
        ret = new_node(ND_RETURN, assign(), NULL);
        use_expect_symbol(";");
        return ret;
    }

    ret = assign();
    use_expect_symbol(";");

    return ret;
}

// assign = same_comp | 
//          ident "=" same_comp
Node *assign() {
    Node *ret = same_comp();

    if (use_symbol("=")) {
        ret = new_node(ND_ASSIGN, ret, same_comp());
    }
    return ret;
}

// same_comp = size_comp ("==" size_comp | "!=" size_comp)*
Node *same_comp() {
    Node *ret = size_comp();
    while (true) {
        if (use_symbol("==")) {
            ret = new_node(ND_EQ, ret, size_comp());
        } else if (use_symbol("!=")) {
            ret = new_node(ND_NEQ, ret, size_comp());
        } else {
            break;
        }
    }
    return ret;
}

// size_comp = add ("<" add | ">" add | "<=" add | ">=" add)*
Node *size_comp() {
    Node *ret = add();
    while (true) {
        if (use_symbol("<")) {
            ret = new_node(ND_LC, ret, add());
        } else if (use_symbol(">")) {
            ret = new_node(ND_RC, ret, add());
        } else if (use_symbol("<=")) {
            ret = new_node(ND_LEC, ret, add());
        } else if (use_symbol(">=")) {
            ret = new_node(ND_REC, ret, add());
        } else {
            break;
        }
    }
    return ret;
}

// add = mul ("+" mul | "-" mul)*
Node *add() {
    Node *ret = mul();
    while (true) {
        if (use_symbol("+")) {
            ret = new_node(ND_ADD, ret, mul());
        } else if (use_symbol("-")) {
            ret = new_node(ND_SUB, ret, mul());
        } else {
            return ret;
        }
    }
    return ret;
}

// mul = unary ("*" unary | "/" unary)*
Node *mul() {
    Node *ret = unary();
    while (true) {
        if (use_symbol("*")) {
            ret = new_node(ND_MUL, ret, unary());
        } else if (use_symbol("/")) {
            ret = new_node(ND_DIV, ret, unary());
        } else {
            return ret;
        }
    }
    return ret;
}

// unary = ("+" | "-")? primariy
Node *unary() {
    if (use_symbol("+")) {
        Node *ret = new_node(ND_ADD, new_node_int(0), priority());
        return ret;
    } else if (use_symbol("-")) {
        Node *ret = new_node(ND_SUB, new_node_int(0), priority());
        return ret;
    }
    return priority();
}

// priority = num | 
//            "(" assign ")" |
//            ident 
Node *priority() {
    if (use_symbol("(")) {
        Node *ret = assign();
        use_expect_symbol(")");
        return ret;
    }

    Token *tmp = use_any_kind(TK_IDENT);

    if (tmp) {
        Node *ret = new_node(ND_VAR, NULL, NULL);
        Var *result = find_var(tmp);
        if (result) {
            ret->var = result;
        } else {
            ret->var = add_var(VR_INT, vars, tmp->str, tmp->str_len);
            vars = ret->var;
        }
        return ret;
    }

    return num();
}


Node *num() {
    return new_node_int(use_expect_int());
}