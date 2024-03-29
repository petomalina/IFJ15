#include <stdio.h>
#include <assert.h>
#include "interpret.h"
#include "errors.h"
#include "symbol_table.h"
#include "gc.h"
#include "ial.h"
#include "string.h"

#define ASTNode struct ast_node // definition of ast node for definition file
#define ASTList struct ast_list

struct symbol_table* scopes;
Stack functions;

const int kBuiltinsCount = 5;
string* kBuiltins[5];

// PrepareFunctions will populate the stack of
// functions, checking for redefinitions.
void PrepareFunctions(ASTList* fcns) {
	if (fcns == NULL || fcns->elem == NULL) {
		throw_error(CODE_ERROR_SEMANTIC, "No function was defined");
	}

	do {
		ASTNode* func = fcns->elem;
		// check for function redefinitions
		if (FindFunction(func->d.string_data) != NULL || IsBuiltin(func->d.string_data)) {
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret][Redefinition] Function redefinition");
		}

		StackPush(&functions, func);
		fcns = fcns->next;
	} while(fcns != NULL);
}

ASTNode *FindFunction(string *name) {
	Element* el = StackTopElement(&functions);
	if (el == NULL) {
		return NULL;
	}

	do {
		ASTNode* func = el->value;
		if (equals(func->d.string_data, name)) {
			return func;
		}

		el = el->next;
	} while(el != NULL);

	return NULL;
}

void InterpretInit(ASTList* fcns) {
	kBuiltins[0] = new_str("concat");
	kBuiltins[1] = new_str("length");
	kBuiltins[2] = new_str("substr");
	kBuiltins[3] = new_str("find");
	kBuiltins[4] = new_str("sort");

	scopes = init_table();
	StackInit(&functions);
	PrepareFunctions(fcns);
}

void InterpretRun() {
	ASTNode* func = FindFunction(new_str("main"));
	if (func == NULL) {
		throw_error(CODE_ERROR_SEMANTIC, "Main function could not be found");
	}

	scope_start(scopes, SCOPE_BLOCK);
	Variable *return_val = gc_malloc(sizeof(Variable));
	InterpretList(func->right->d.list, return_val);

	scope_end(scopes);
}

void InterpretNode(ASTNode* node, Variable* return_val) {

	switch(node->type) {
		// retrieve the node from the list
		case AST_ASSIGN:
			InterpretAssign(node);
			break;
		case AST_CALL:
			InterpretFunctionCall(node);
			break;
		case AST_EXPRESSION:
			// first expression node is in the left leaf of the expression (see expression parser)
			EvaluateExpression(node->left);
			break;
		case AST_IF:
			InterpretIf(node, return_val);
			break;
		case AST_COUT:
			InterpretCout(node);
			break;
		case AST_CIN:
			InterpretCin(node);
			break;
		case AST_VAR_CREATION:
			InterpretVarCreation(node);
			break;
		case AST_FOR:
			InterpretFor(node, return_val);
			break;
		case AST_BLOCK:
			scope_start(scopes, SCOPE_BLOCK);
			InterpretList(node->d.list, return_val);
			scope_end(scopes);
		case AST_NONE:
			// Empty Statement can happen from trailing semicolons after the expressions.
			// Warn: This is hotfix
			return;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}
}

void InterpretVarCreation(ASTNode *var) {
	if (!is_creatable(scopes, var->right->d.string_data)) {
		throw_error(CODE_ERROR_SEMANTIC, "Variable redefinition");
	}

	Variable *variable = gc_malloc(sizeof(Variable));
	variable->data_type = var->left->var_type;
	variable->data.numeric_data = 0; // null the data
	variable->initialized = false;

	set_symbol(scopes, var->right->d.string_data, variable);
}

void InterpretList(ASTList* list, Variable* return_val) {
	return_val->data_type = AST_VAR_NULL;

	// no interpretation for empty list needed
	if (list->elem == NULL) {
		return;
	}

	do {
		if (list->elem->type != AST_RETURN) {
			// interpret node on current leaf
			InterpretNode(list->elem, return_val);
		} else {
			// handle return
			Variable *ret = EvaluateExpression(list->elem->left);
			return_val->data_type = ret->data_type;
			return_val->data = ret->data;
			return_val->initialized = ret->initialized;
		}
		// change leaf to next
		list = list->next;
	} while (list != NULL && return_val->data_type == AST_VAR_NULL);
}

void InterpretAssign(ASTNode *statement) {
	// left is id with name and type
	// right is expression assigned
	Variable *result = EvaluateExpression(statement->right);
	if (result == NULL) {
		throw_error(CODE_ERROR_SEMANTIC, "[Interpret][Expression] Expression could not be evaluated");
	}

	Variable* current = NULL;
	string* var_name = NULL;
	switch (statement->left->type) {
		case AST_VAR_CREATION:
			InterpretVarCreation(statement->left);
			var_name = statement->left->right->d.string_data;
			break;
		case AST_VAR:
			var_name = statement->left->d.string_data;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}
	current = get_symbol(scopes, var_name);

	if (current == NULL) {
		throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Variable assigning failed due to missing variable");
	}
	// handle auto keyword
	if (current->data_type == AST_VAR_AUTO) {
		current->data_type = result->data_type;
		current->data = result->data;
	}
	if (!AreCompatibleTypes(current->data_type, result->data_type)) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Assigning bad value to the variable");
	}

	// variable was assigned a value
	current->initialized = true;

	current->data = result->data;
}

void InterpretIf(ASTNode *ifstatement, Variable* return_val) {
	scope_start(scopes, SCOPE_BLOCK);
	Variable* condition_result = EvaluateExpression(ifstatement->d.condition);
	if (!AreCompatibleTypes(condition_result->data_type, AST_VAR_BOOL)) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret][If] Expression not bool");
	}

	ASTNode *block = condition_result->data.bool_data? ifstatement->left: ifstatement->right;

	InterpretList(block->d.list, return_val);

	scope_end(scopes);
}

bool IsBuiltin(string *name) {
	for (int i = 0; i < kBuiltinsCount; i++) {
		if (equals(name, kBuiltins[i])) {
			return true;
		}
	}

	return false;
}

Variable* InterpretFunctionCall(ASTNode *call) {
	if (call->d.list == NULL || call->d.list->elem == NULL) {
		// TODO: if function should return value, semantic error
		return NULL; // function is empty
	}

	if (IsBuiltin(call->d.string_data)) {
		return InterpretBuiltinCall(call);
	}

	ASTNode* func = FindFunction(call->d.string_data);
	if (func == NULL) {
		throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Calling function that was not defined");
	}

	// first set this to block, so we can add variables that are in the outer block
	scope_start(scopes, SCOPE_BLOCK);

	ASTList* arg = func->left->d.list;
	for(ASTList* it = call->left->d.list; it != NULL && it->elem != NULL; it = it->next, arg = arg->next) {
		// this is the symbol that is bein passed to the function
		Variable* symbol = EvaluateExpression(it->elem);
		// we need to copy this symbol to the current scope with name provided by function
		Variable* this_symbol = gc_malloc(sizeof(Variable));
		this_symbol->data = symbol->data;
		this_symbol->data_type = symbol->data_type;
		this_symbol->initialized = true;

		set_symbol(scopes, arg->elem->d.string_data, this_symbol);
	}

	// correct the scope type to function
	((struct hash_table*)StackTop(scopes->stack))->scope_type = SCOPE_FUNCTION;

	Variable* return_val = gc_malloc(sizeof(Variable));
	// list of statements that should be interpreted
	// is in the right leaf of the function
	ASTList* list = func->right->d.list;
	InterpretList(list, return_val);

	if (!AreCompatibleTypes(return_val->data_type, func->var_type)) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret][Return] Cannot return non-compatible values");
	}

	scope_end(scopes);

	return return_val;
}

Variable *InterpretBuiltinCall(ASTNode *call) {
	string * func_name = call->d.string_data;
	ASTList* it = call->left->d.list;

	if(equals(func_name, new_str("concat")) ) {
		return BuiltInConcat(it);
	}
	else if(equals(func_name, new_str("length"))) {
		return  BuiltInLength(it);
	}
	else if(equals(func_name, new_str("substr"))) {
		return BuiltInSubstr(it);
	}
	else if(equals(func_name, new_str("sort"))) {
		return BuiltInSort(it);
	}
	else if(equals(func_name, new_str("find"))) {
		return  BuiltInFind(it);
	}
	return NULL;
}

void InterpretFor(ASTNode *node, Variable* return_val) {
	scope_start(scopes, SCOPE_BLOCK);

	ASTNode* first_block = node->d.list->elem; // first block
	ASTNode* second_block = node->d.list->next->elem; // second block
	ASTNode* third_block = node->d.list->next->next->elem; // third block

	InterpretNode(first_block, return_val);

	Variable *condition = EvaluateExpression(second_block);
	if (condition->data_type != AST_VAR_BOOL) {
		throw_error(CODE_ERROR_SEMANTIC, "[Interpret][For] Second field expects boolean result");
	}

	while(condition->data.bool_data && return_val->data_type == AST_VAR_NULL) {
		scope_start(scopes, SCOPE_BLOCK);
		// block is in the left node
		InterpretList(node->left->d.list, return_val);

		// interpret third block
		InterpretNode(third_block, return_val);

		// get the condition result
		condition = EvaluateExpression(second_block);
		if (condition->data_type != AST_VAR_BOOL) {
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret][For] Second field expects boolean result");
		}
		scope_end(scopes);
	};

	scope_end(scopes);
}

enum ast_var_type GetVarTypeFromLiteral(enum ast_literal_type type) {
	switch (type) {
		case AST_LITERAL_INT:
			return AST_VAR_INT;
		case AST_LITERAL_STRING:
			return AST_VAR_STRING;
		case AST_LITERAL_REAL:
			return AST_VAR_DOUBLE;
		case AST_LITERAL_NULL:
			return AST_VAR_NULL;
		case AST_LITERAL_TRUE:
		case AST_LITERAL_FALSE:
			return AST_VAR_BOOL;
	}

	// this should not be necessary, but compilers ...
	return AST_VAR_NULL;
}

bool AreCompatibleTypes(enum ast_var_type t1, enum ast_var_type t2) {
	if (t1 == AST_VAR_DOUBLE && t2 == AST_VAR_INT) {
		return true;
	}

	if ((t1 == AST_VAR_INT && t2 == AST_VAR_BOOL) || (t1 == AST_VAR_BOOL && t2 == AST_VAR_INT)) {
		return true;
	}

	return t1 == t2;
}

Variable* EvaluateExpression(ASTNode *expr) {
	// empty epxression
	if (expr == NULL) {
		return NULL;
	}

	// unpack if the expression is packed
	if (expr->type == AST_EXPRESSION) {
		expr = expr->left;
	}

	Variable* result = NULL;

	// if the left expression is literal, all we do
	// is copy the literal to the top node as a start
	if (expr->type == AST_LITERAL) {
		// expression is literal. Just get the literal type and return value
		result = gc_malloc(sizeof(Variable));
		result->data_type = GetVarTypeFromLiteral(expr->literal);
		result->data = expr->d;
		result->initialized = true;
	} else if (expr->type == AST_BINARY_OP) {
		// evaluate expressions on both sides
		Variable *left = EvaluateExpression(expr->left);
		Variable *right = EvaluateExpression(expr->right);

		if (!AreCompatibleTypes(left->data_type, right->data_type)) {
			throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret][Expression] Provided values are of different types");
		}

		if (!(left->initialized && right->initialized)) {
			throw_error(CODE_ERROR_UNINITIALIZED_ID, "[Interpret][Expression] Trying to use uninitialized variable");
		}

		// expression is binary operation, calculate based on the operator
		switch (expr->d.binary) {
			case AST_BINARY_PLUS:
				result = EvaluateBinaryPlus(left, right);
				break;
			case AST_BINARY_MINUS:
				result = EvaluateBinaryMinus(left, right);
				break;
			case AST_BINARY_TIMES:
				result = EvaluateBinaryMult(left, right);
				break;
			case AST_BINARY_DIVIDE:
				result = EvaluateBinaryDivide(left, right);
				break;
			case AST_BINARY_LESS:
				result = EvaluateBinaryLess(left, right);
				break;
			case AST_BINARY_MORE:
				result = EvaluateBinaryMore(left, right);
				break;
			case AST_BINARY_LESS_EQUALS:
				result = EvaluateBinaryLessEqual(left, right);
				break;
			case AST_BINARY_MORE_EQUALS:
				result = EvaluateBinaryMoreEqual(left, right);
				break;
			case AST_BINARY_EQUALS:
				result = EvaluateBinaryEqual(left,right);
				break;
			case AST_BINARY_NOT_EQUALS:
				result = EvaluateBinaryNotEqual(left, right);
				break;
		}

		result->initialized = true;
	} else if (expr->type == AST_VAR) {
		// the expression is variable, return the variable value
		result = get_symbol(scopes, expr->d.string_data);
		if (result == NULL) {
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret][Var] Variable in the expression was not found");
		}
	} else if (expr->type == AST_CALL) {
		result = InterpretFunctionCall(expr);
	}

	return result;
}

Variable *EvaluateBinaryPlus(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));

	switch (left->data_type) {
		case AST_VAR_INT:
			if (right->data_type == AST_VAR_DOUBLE) {
				result->data_type = AST_VAR_DOUBLE;
			}
			else {
				result->data_type = AST_VAR_INT;
			}
			result->data.numeric_data = (int)(left->data.numeric_data + right->data.numeric_data);
			break;

		case AST_VAR_DOUBLE:
			if (right->data_type == AST_VAR_INT) {
				right->data.numeric_data = (double)(right->data.numeric_data);
			}
			result->data.numeric_data = (left->data.numeric_data + right->data.numeric_data);
			result->data_type = AST_VAR_DOUBLE;
			break;
		case AST_VAR_STRING:
			result->data.string_data = cat_str(left->data.string_data, right->data.string_data);
			result->data_type = AST_VAR_STRING;
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari plus operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryMinus(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));

	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				result->data_type = AST_VAR_DOUBLE;
			}
			else {
				result->data_type = AST_VAR_INT;
			}
			result->data.numeric_data = (int)(left->data.numeric_data - right->data.numeric_data);
			break;
		case AST_VAR_DOUBLE:
			if(right->data_type == AST_VAR_INT) {
				right->data.numeric_data = (double)(right->data.numeric_data);
			}
			result->data.numeric_data = (left->data.numeric_data - right->data.numeric_data);
			result->data_type = AST_VAR_DOUBLE;
			break;
		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari minus operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari minus operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryMult(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));

	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				result->data_type = AST_VAR_DOUBLE;
			}
			else {
				result->data_type = AST_VAR_INT;
			}
			result->data.numeric_data = (int)(left->data.numeric_data * right->data.numeric_data);
			break;

		case AST_VAR_DOUBLE:
			result->data.numeric_data = (left->data.numeric_data * right->data.numeric_data);
			result->data_type = AST_VAR_DOUBLE;
			break;
		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari multiple operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari multiple operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable* EvaluateBinaryDivide(Variable* left, Variable* right) {
	Variable *result = gc_malloc(sizeof(Variable));

	switch (left->data_type) {
		case AST_VAR_INT:
			if((int)right->data.numeric_data == 0) {
				throw_error(CODE_ERROR_RUNTIME_DIV_BY_0, "[Interpret] Can't divide by zero");
			}
			if(right->data_type == AST_VAR_DOUBLE){
				result->data_type = AST_VAR_DOUBLE;
			}
			else {
				result->data_type = AST_VAR_INT;
			}
			result->data.numeric_data = (int)(left->data.numeric_data / right->data.numeric_data);
			break;

		case AST_VAR_DOUBLE:
			if(right->data.numeric_data == 0) {
				throw_error(CODE_ERROR_RUNTIME_DIV_BY_0, "[Interpret] Can't divide by zero");
			}
			result->data.numeric_data = (left->data.numeric_data / right->data.numeric_data);
			result->data_type = AST_VAR_DOUBLE;
			break;
		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari divide operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari divide operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryLess(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;

	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data < right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
			if(right->data_type == AST_VAR_INT) {
				right->data.numeric_data = (double)(right->data.numeric_data);
			}
			if(left->data.numeric_data < right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryMore(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;
	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data > right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
		if(right->data_type == AST_VAR_INT) {
			right->data.numeric_data = (double)(right->data.numeric_data);
		}
		if(left->data.numeric_data > right->data.numeric_data) {
			result->data.bool_data = true;
		}
		else {
			result->data.bool_data = false;
		}
			break;

		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari more operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari more operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryLessEqual(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;
	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data <= right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
		if(right->data_type == AST_VAR_INT) {
			right->data.numeric_data = (double)(right->data.numeric_data);
		}
		if(left->data.numeric_data <= right->data.numeric_data) {
			result->data.bool_data = true;
		}
		else {
			result->data.bool_data = false;
		}
			break;

		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less or equal operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less or equal operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryMoreEqual(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;
	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data >= right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
		if(right->data_type == AST_VAR_INT) {
			right->data.numeric_data = (double)(right->data.numeric_data);
		}
		if(left->data.numeric_data >= right->data.numeric_data) {
			result->data.bool_data = true;
		}
		else {
			result->data.bool_data = false;
		}
			break;

		case AST_VAR_STRING:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less or equal operation on string");
			break;
		case AST_VAR_BOOL:
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot perform binari less or equal operation on bool");
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryEqual(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;
	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data == right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
		if(right->data_type == AST_VAR_INT) {
			right->data.numeric_data = (double)(right->data.numeric_data);
		}
		if(left->data.numeric_data == right->data.numeric_data) {
			result->data.bool_data = true;
		}
		else {
			result->data.bool_data = false;
		}
			break;

		case AST_VAR_STRING:
			result->data.numeric_data = equals(left->data.string_data, right->data.string_data);
			result->data_type = AST_VAR_INT;
			break;
		case AST_VAR_BOOL:
			if(left->data.bool_data == right->data.bool_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable *EvaluateBinaryNotEqual(Variable *left, Variable *right) {
	Variable *result = gc_malloc(sizeof(Variable));
	result->data_type = AST_VAR_BOOL;
	switch (left->data_type) {
		case AST_VAR_INT:
			if(right->data_type == AST_VAR_DOUBLE) {
				left->data.numeric_data = (double)(left->data.numeric_data);
			}
			if(left->data.numeric_data != right->data.numeric_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;

		case AST_VAR_DOUBLE:
		if(right->data_type == AST_VAR_INT) {
			right->data.numeric_data = (double)(right->data.numeric_data);
		}
		if(left->data.numeric_data != right->data.numeric_data) {
			result->data.bool_data = true;
		}
		else {
			result->data.bool_data = false;
		}
			break;

		case AST_VAR_STRING:
			result->data.numeric_data = equals(left->data.string_data, right->data.string_data);
			result->data_type = AST_VAR_INT;
			break;

		case AST_VAR_BOOL:
			if(left->data.bool_data == right->data.bool_data) {
				result->data.bool_data = true;
			}
			else {
				result->data.bool_data = false;
			}
			break;
		case AST_VAR_NULL:
			result->data_type = AST_VAR_NULL;
			break;
		default:
			throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided ASTNode type not recognized");
	}

	return result;
}

Variable * EvaluateArgument(ASTNode* arg) {
	if(arg->type == AST_CALL) {
		return  InterpretFunctionCall(arg);
	}
	else if(arg->type == AST_EXPRESSION) {
		return  EvaluateExpression(arg);
	}

	return NULL;
}

Variable * BuiltInConcat(ASTList * args) {
	Variable* result = gc_malloc(sizeof(Variable));
	Variable * str1 = NULL;
	Variable * str2 = NULL;

	str1 = EvaluateArgument(args->elem);
	str2 = EvaluateArgument(args->next->elem);

	if(str1->data_type !=  AST_VAR_STRING || str2->data_type != AST_VAR_STRING ) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Invalid parameter type.");
	}

	result->data_type= AST_VAR_STRING;
	result->data.string_data =  new_str(concat(str1->data.string_data->str, str2->data.string_data->str));
	result->initialized = true;
	return result;
}


Variable * BuiltInLength(ASTList * args) {
	Variable * result = gc_malloc(sizeof(Variable));
	Variable * arg = NULL;

	arg = EvaluateArgument(args->elem);

	if(arg->data_type !=  AST_VAR_STRING) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Invalid parameter type.");
	}

	result->data_type = AST_VAR_INT;
	result->data.numeric_data = length(arg->data.string_data->str);
	result->initialized = true;
	return result;
}

Variable * BuiltInSubstr(ASTList * args) {
	Variable * result = gc_malloc(sizeof(Variable));
	Variable * arg1 = NULL;
	Variable * arg2 = NULL;
	Variable * arg3 = NULL;

	arg1 = EvaluateArgument(args->elem);
	arg2 = EvaluateArgument(args->next->elem);
	arg3 = EvaluateArgument(args->next->next->elem);

	if (arg1->data_type != AST_VAR_STRING || arg2->data_type != AST_VAR_INT || arg3->data_type != AST_VAR_INT) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Invalid parameter type. ");
	}

	result->data_type = AST_VAR_STRING;
	result->data.string_data = new_str( substr( arg1->data.string_data->str, (int)arg2->data.numeric_data, (int)arg3->data.numeric_data ));
	result->initialized = true;
	return result;
}

Variable * BuiltInSort(ASTList * args) {
	Variable * result = gc_malloc(sizeof(Variable));
	Variable * arg = NULL;

	arg = EvaluateArgument(args->elem);

	if(arg->data_type !=  AST_VAR_STRING) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Invalid parameter type.");
	}

	result->data_type = AST_VAR_STRING;
	result->data.string_data = new_str(sort(arg->data.string_data->str));
	result->initialized = true;
	return result;
}

Variable * BuiltInFind(ASTList * args) {
	Variable* result = gc_malloc(sizeof(Variable));
	Variable * str1 = NULL;
	Variable * str2 = NULL;

	str1 = EvaluateArgument(args->elem);
	str2 = EvaluateArgument(args->next->elem);

	if(str1->data_type !=  AST_VAR_STRING || str2->data_type != AST_VAR_STRING ) {
		throw_error(CODE_ERROR_COMPATIBILITY, "[Interpret] Invalid parameter type.");
	}

	result->data_type= AST_VAR_INT;
	result->data.numeric_data =  find(str1->data.string_data->str, str2->data.string_data->str);
	result->initialized = true;

	return result;
}

// cout node is a list of expressions that should be
// printed to the screen
void InterpretCout(ASTNode *cout) {
	ASTList* list = cout->d.list;
	do {
		// this is the list of expressions
		ASTNode* elem = list->elem;
		Variable* result = EvaluateExpression(elem);
		if (result == NULL) {
			list = list->next;
			continue; // empty expression
		}

		if (!result->initialized) {
			throw_error(CODE_ERROR_UNINITIALIZED_ID, "[Interpret][Cout] Uninitialized variabled used");
		}

		switch (result->data_type) {
			case AST_VAR_INT:
				printf("%d", (int)result->data.numeric_data);
				break;
			case AST_VAR_DOUBLE:
				printf("%g", result->data.numeric_data);
				break;
			case AST_VAR_NULL:
				printf("NULL");
				break;
			case AST_VAR_STRING:
				printf("%s", result->data.string_data->str);
				break;
			case AST_VAR_BOOL:
				printf(result->data.bool_data ? "true" : "false");
				break;
			default:
				throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided var in cout not supported");
		}

		list = list->next;
	} while (list != NULL);
}

void InterpretCin(ASTNode *cin) {
	ASTList* list = cin->d.list; // id list

	do {
		ASTNode* elem = list->elem;
		// find the variable that should get the input
		Variable *variable = get_symbol(scopes, elem->d.string_data);
		if (variable == NULL) {
			throw_error(CODE_ERROR_SEMANTIC, "[Interpret] Cannot assign input to non existing variable");
		}

		switch (variable->data_type) {
			case AST_VAR_INT: {
				int data;
				scanf("%d", &data);
				variable->data.numeric_data = data;
				break;
			}
			case AST_VAR_DOUBLE: {
				double data;
				scanf("%lf", &data);
				variable->data.numeric_data = data;
				break;
			}
			case AST_VAR_STRING: {
				string *input = new_str("");
				char char_input[10];
				while (gets(char_input) != NULL) {
					for (int i = 0; i < 10 && char_input[i] != '\0'; i++) {
						add_char(input, char_input[i]);
					}
				}
				variable->data.string_data = input;
			case AST_VAR_BOOL:
				break;
			case AST_VAR_NULL:
				 break;
			}
			default:
				throw_error(CODE_ERROR_RUNTIME_OTHER, "[Interpret] Provided var in cin not supported");
		}

		// inputed variables are always initialized if no error happened
		variable->initialized = true;

		list = list->next;
	} while(list != NULL);
}
