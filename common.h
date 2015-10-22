#ifndef COMMON_H
#define COMMON_H

#define DEBUG true

#include <stdio.h>
#include <stdlib.h>

// postupne doplnit
enum token_type {
    T_ID,// 0
    T_VAR,// 1
    T_STRING, // 3 ukladani stringu
    T_FUNCTION, // 4
    T_IF, // 5
    T_ELSE, // 6
    T_WHILE, // 7
    T_RETURN, // 8
    T_LPAR, // 9 (
    T_RPAR, // 10 )
    T_LBRACE, // 11 {
    T_RBRACE, // 12}
    T_NOT, // 13
    T_PLUS, // 14
    T_MINUS, // 15
    T_MULTIPLY, // 16
    T_DIVIDE, // 17
    T_LESS, // 18
    T_MORE, // 19
    T_LESSOREQ, // 20
    T_MOREOREQ, // 21
    T_EQUALS, // 22 ==
    T_NOTEQUAL, // 23 !=
    T_STRICTNOTEQ, // !25 ==

    T_ASSIGN, // 26 =
    T_TRUE, // 27
    T_FALSE, // 28
    T_NULL, //29
    T_COMMA, // 30
    T_SEMICOLON, // 31
    T_CONCATENATE, // 32
    T_THE_END // 33
};

enum literal_type
{
    LITERAL_STRING,
    LITERAL_DOUBLE,
    LITERAL_INT
};

// doplnim postupne
enum instruction_type {
    INST_ASSIGN,
    // odlisna od prirazeni!
    INST_VAR_CREATE,
    INST_WHILE,
    INST_IF,
    INST_FN_CALL,
    INST_RETURN,
    INST_COUT,
    INST_CIN,
    INST_FN_DEFINITION,
    INST_FOR,
    // binarni operace
    INST_BIN_OP,
};

typedef struct instruction {
    enum instruction_type type;
    struct symbol_table *source1;
    struct symbol_table *source2;
    void *destination;

    struct instruction *next;
} instruction;

struct data
{
    int error; // jsem se ulozi error, ten se dispatchne na nejvyssi urovni

    struct token* token; // aktualni token

    FILE* file; // soubor, ktery bude interpretovan
    struct symbol_table* functions;
    struct symbol_table* vars;
    struct ast_node* tree;
};

// TODO: nema tam byt jeste jeden radek?
// nema tam byt jeste jeden radek?
// chyba "neni mozne odvodit datovy typ promenne"
typedef enum ERROR_CODES {
    CODE_OK = 0,
    CODE_ERROR_LEX = 1,
    CODE_ERROR_SYNTAX = 2,
    CODE_ERROR_SEMANTIC = 3,
    CODE_ERROR_COMPATIBILITY = 4,
    CODE_ERROR_SEMANT_OTHER = 5,
    CODE_ERROR_NUMBER_IN = 6,
    CODE_ERROR_UNINITIALIZED_ID = 7,
    CODE_ERROR_RUNTIME_DIV_BY_0 = 8,
    CODE_ERROR_RUNTIME_OTHER = 9,
    CODE_ERROR_INTERNAL = 99,

    CODE_PARSE_END = 1000
} ERROR_CODE;

/*
 * Makro, ktere ukonci funkci a vrati false, kdyz nastal error
 */
#define EXPECT(e)\
		if(!e) return false;

// ocekava validitu
// a taky ocekava, ze se datova struktura jmenuje d
// kdyz se d nebude jmenovat, tak to nebude fungovat !
#define EXPECT_VALIDITY(d)\
        if(d->error != 0) return false;

#endif
