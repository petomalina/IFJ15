#ifndef _IAL_H_
#define _IAL_H_

#include <stdbool.h>
# include <limits.h>
#include "string.h"
# include <stdio.h>
#include "common.h"

/******************** HASH TABLE ********************/

#define MAX_HTSIZE 701
#define MAX_HTSTACK 107

/**
** Mozne typy.
*/
typedef enum {
	T_NIL = 0,
	T_NUM = 1,
	T_STR = 2,
	T_BOOL = 3,
	T_FUNC = 4
} Ttypes;

/**
** Pro zpracovani funkce.
*/
typedef struct FuncForm {
	bool def;			// Jestli byla funkce definovana.
	void *loc_hash;	// Lokalni hashovaci tabulka pro kazdou funkci.
	int param_count;	// Pocet parametru funkce.
	void *label;		// Navesti kde zacina funkce.
} FuncForm;

/**
** Union (kvuli setreni mista) pro vsechny typy.
*/
typedef union {
	double number;
	char *str;
	int boolean;
	FuncForm vlFunc;
} TValues;

/**
** Typ promenne nebo funkce a hodnota.
*/
typedef struct TData {
	TValues val;	// Hodnoty
	Ttypes type;	// Typy
} TData;

/**
** Struktura pro prvek v tabulce.
*/
typedef struct Ttitem {
	char *key;
	int len_key;	// Delka klice nebo retezce.
	bool def;
	TData *data;
	struct Ttitem *nextPtr;
} *TtitemPtr;

/**
** Hashovaci tabulka.
*/
typedef TtitemPtr Ttable_hash[MAX_HTSIZE];

// Hash table functions
unsigned long Hash(unsigned char* what);
void HashTableAddVariable(string* scope, string* var_name, enum ast_var_type type);
void HashTableFindVariable(string* scope, string* var_name);

int length(char* s);
char* substr(char* s, int i, int n);
char* concat(char* s1, char* s2);
int max(int a, int b);
char* sort(char* chars);
int find(char* s, char* search);

#endif
