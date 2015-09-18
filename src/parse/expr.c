#include "parse.h"



static void parse_expr__cleanup(
	parse_expr_t expr)
{
	switch (expr.type)
	{
		case PARSE_EXPR_CONSTANT:
			parse_literal_cleanup(expr.literal);
			break;

		case PARSE_EXPR_VARIABLE:
			parse_lhs_cleanup(expr.variable);
			break;

		case PARSE_EXPR_BRACKETS:
			parse_expr_delete(expr.brackets.expr);
			break;

		case PARSE_EXPR_UNARY:
			parse_expr_delete(expr.unary.a);
			break;

		case PARSE_EXPR_BINARY:
			parse_expr_delete(expr.binary.a);
			parse_expr_delete(expr.binary.b);
			break;

		default:
			break;
	}
}

static bool parse_expr__clone(
	parse_expr_t* dst, const parse_expr_t* src)
{
	if (!src || !dst)
		return false;

	parse_expr_t clone = *src;
	switch (clone.type)
	{
		case PARSE_EXPR_CONSTANT:
			if (!parse_literal_clone(
				&clone.literal, &src->literal))
				return false;
			break;

		case PARSE_EXPR_VARIABLE:
			if (!parse_lhs_clone(
				&clone.variable, &src->variable))
				return false;
			break;

		case PARSE_EXPR_BRACKETS:
			clone.brackets.expr
				= parse_expr_copy(src->brackets.expr);
			if (!clone.brackets.expr)
				return false;
			break;

		case PARSE_EXPR_UNARY:
			clone.unary.a
				= parse_expr_copy(src->unary.a);
			if (!clone.unary.a)
				return false;
			break;

		case PARSE_EXPR_BINARY:
			clone.binary.a
				= parse_expr_copy(src->binary.a);
			if (!clone.binary.a)
				return false;

			clone.binary.b
				= parse_expr_copy(src->binary.b);
			if (!clone.binary.b)
			{
				parse_expr_delete(clone.binary.a);
				return false;
			}
			break;

		default:
			return false;
	}

	*dst = clone;
	return true;
}

static parse_expr_t* parse_expr__alloc(
	parse_expr_t expr)
{
	parse_expr_t* aexpr
		= (parse_expr_t*)malloc(
			sizeof(parse_expr_t));
	if (!aexpr) return NULL;
	*aexpr = expr;
	return aexpr;
}



static unsigned parse_expr__level(
	parse_expr_t expr)
{
	switch (expr.type)
	{
		case PARSE_EXPR_UNARY:
			return parse_operator_precedence(expr.unary.operator);
		case PARSE_EXPR_BINARY:
			return parse_operator_precedence(expr.binary.operator);
		default:
			break;
	}

	return 0;
}

static bool parse_expr__term(char c)
{
	if (is_end_statement(c, NULL))
		return true;

	switch (c)
	{
		case ',':
		case ')':
			return true;
		default:
			break;
	}
	return false;
}



static unsigned parse_expr__at_or_below(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr, unsigned level);

static unsigned parse__expr(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr)
{
	return parse_expr__at_or_below(
		src, ptr, expr, OPERATOR_PRECEDENCE_MAX);
}

static unsigned parse_expr__literal(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr)
{
	unsigned len = parse_literal(
		src, ptr, &expr->literal);
	if (len == 0) return 0;

	expr->type = PARSE_EXPR_CONSTANT;
	return len;
}

static unsigned parse_expr__primary(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr)
{
	unsigned len = parse_expr__literal(
		src, ptr, expr);
	if (len > 0) return len;

	len = parse_expr__literal(
			src, ptr, expr);
	if (len > 0) return len;

	/* TODO - Check for intrinsics first. */

	len = parse_lhs(
		src, ptr, &expr->variable);
	if (len > 0)
	{
		expr->type = PARSE_EXPR_VARIABLE;
		return len;
	}

	/* TODO - Parse constant-subobject. */

	/* TODO - Parse array constructor. */

	/* TODO - Parse structure constructor. */

	/* TODO - Parse function-reference. */

	if (ptr[0] == '(')
	{
		parse_expr_t expr_brackets;
		unsigned len = parse__expr(
			src, &ptr[1], &expr_brackets);
		if (len > 0)
		{
			if  (ptr[1 + len] != ')')
			{
				parse_expr__cleanup(expr_brackets);
			}
			else
			{
				expr->brackets.expr
					= parse_expr__alloc(expr_brackets);
				if (!expr->brackets.expr)
				{
					parse_expr__cleanup(expr_brackets);
					return 0;
				}
				expr->type = PARSE_EXPR_BRACKETS;
				return (len + 2);
			}
		}
	}

	return 0;
}

static unsigned parse_expr__unary(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr, unsigned level)
{
	/* TODO - Defined unary operators. */

	parse_operator_e op;
	unsigned op_len = parse_operator(
		src, ptr, &op);
	if ((op_len == 0)
		|| !parse_operator_unary(op))
		return 0;

	unsigned op_level = parse_operator_precedence(op);
	if (op_level > level)
		return 0;

	parse_expr_t a;
	unsigned a_len = parse_expr__at_or_below(
		src, &ptr[op_len], &a, op_level);
	if (a_len == 0) return 0;

	expr->type = PARSE_EXPR_UNARY;
	expr->unary.operator = op;

	expr->unary.a = parse_expr__alloc(a);
	if (!expr->unary.a)
	{
		parse_expr__cleanup(a);
		return 0;
	}

	return (op_len + a_len);
}

static bool parse_expr__has_right_ambig_point(
	parse_expr_t* expr)
{
	if (!expr)
		return false;

	switch (expr->type)
	{
		case PARSE_EXPR_CONSTANT:
			return ((expr->literal.type == PARSE_LITERAL_NUMBER)
				&& (expr->literal.number.base[expr->literal.number.size - 1] == '.'));
		case PARSE_EXPR_UNARY:
			return parse_expr__has_right_ambig_point(expr->unary.a);
		case PARSE_EXPR_BINARY:
			return parse_expr__has_right_ambig_point(expr->binary.b);
		default:
			break;
	}
	return false;
}

static void parse_expr__cull_right_ambig_point(
	parse_expr_t* expr)
{
	if (!expr)
		return;

	switch (expr->type)
	{
		case PARSE_EXPR_CONSTANT:
			if (expr->literal.type == PARSE_LITERAL_NUMBER)
				expr->literal.number.size -= 1;
			break;
		case PARSE_EXPR_UNARY:
			parse_expr__cull_right_ambig_point(expr->unary.a);
			break;
		case PARSE_EXPR_BINARY:
			parse_expr__cull_right_ambig_point(expr->binary.b);
			break;
		default:
			break;
	}
}

static unsigned parse_expr__binary_at_or_below(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr, unsigned level)
{
	parse_expr_t a;
	unsigned a_len = parse_expr__at_or_below(
		src, ptr, &a, (level - 1));
	if (a_len == 0) return 0;

	/* Optimize by returning a if we see end of statement or close bracket. */
	if (parse_expr__term(ptr[a_len]))
	{
		*expr = a;
		return a_len;
	}

	/* Handle case where we have something like:
	   ( 3 ** 3 .EQ. 76 ) */
	if (parse_expr__has_right_ambig_point(&a))
	{
		parse_operator_e op;
		unsigned op_len = parse_operator(src, &ptr[a_len - 1], &op);
		if ((op_len > 0)
			&& parse_operator_binary(op)
			&& (parse_operator_precedence(op) <= level))
		{
			parse_expr__cull_right_ambig_point(&a);
			a_len -= 1;
		}
	}

	/* TODO - Defined binary operators. */

	parse_operator_e op;
	unsigned op_len = parse_operator(
		src, &ptr[a_len], &op);
	if ((op_len == 0) || !parse_operator_binary(op))
	{
		parse_expr__cleanup(a);
		return 0;
	}

	unsigned op_level = parse_operator_precedence(op);
	if ((op_level > level)
		|| (parse_expr__level(a) >= op_level))
	{
		parse_expr__cleanup(a);
		return 0;
	}

	parse_expr_t b;
	unsigned b_len = parse_expr__at_or_below(
		src, &ptr[a_len + op_len], &b, op_level);
	if (b_len == 0)
	{
		parse_expr__cleanup(a);
		return 0;
	}

	expr->type = PARSE_EXPR_BINARY;
	expr->binary.operator = op;

	expr->binary.a = parse_expr__alloc(a);
	if (!expr->binary.a)
	{
		parse_expr__cleanup(a);
		parse_expr__cleanup(b);
		return 0;
	}

	expr->binary.b = parse_expr__alloc(b);
	if (!expr->binary.b)
	{
		parse_expr_delete(expr->binary.a);
		parse_expr__cleanup(b);
		return 0;
	}

	return (a_len + op_len + b_len);
}

static unsigned parse_expr__binary(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr, unsigned level)
{
	unsigned i;
	for (i = level; i > 0; i--)
	{
		unsigned len = parse_expr__binary_at_or_below(
			src, ptr, expr, i);
		if (len > 0) return len;
	}
	return 0;
}

static unsigned parse_expr__at_or_below(
	const sparse_t* src, const char* ptr,
	parse_expr_t* expr, unsigned level)
{
	unsigned len = parse_expr__binary(
		src, ptr, expr, level);
	if (len > 0) return len;

	len = parse_expr__unary(
		src, ptr, expr, level);
	if (len > 0) return len;

	return parse_expr__primary(
		src, ptr, expr);
}



parse_expr_t* parse_expr_literal(
	const sparse_t* src, const char* ptr,
	unsigned* len)
{
	parse_expr_t e;
	unsigned i = parse_expr__literal(
		src, ptr, &e);
	if (i == 0) return NULL;

	parse_expr_t* expr
		= parse_expr__alloc(e);
	if (!expr)
	{
		parse_expr__cleanup(e);
		return NULL;
	}

	if (len) *len = i;
	return expr;
}

parse_expr_t* parse_expr(
	const sparse_t* src, const char* ptr,
	unsigned* len)
{
	parse_expr_t e;
	unsigned i = parse__expr(
		src, ptr, &e);
	if (i == 0) return NULL;

	parse_expr_t* expr
		= parse_expr__alloc(e);
	if (!expr)
	{
		parse_expr__cleanup(e);
		return NULL;
	}

	if (len) *len = i;
	return expr;
}

void parse_expr_delete(
	parse_expr_t* expr)
{
	if (!expr)
		return;

	parse_expr__cleanup(*expr);
	free(expr);
}

parse_expr_t* parse_expr_copy(
	const parse_expr_t* expr)
{
	parse_expr_t copy;
	if (!parse_expr__clone(&copy, expr))
		return NULL;

	parse_expr_t* acopy
		= parse_expr__alloc(copy);
	if (!acopy)
		parse_expr__cleanup(copy);
	return acopy;
}



parse_expr_list_t* parse_expr_list(
	const sparse_t* src, const char* ptr,
	unsigned* len)
{
	parse_expr_list_t* list
		= (parse_expr_list_t*)malloc(
			sizeof(parse_expr_list_t));
	if (!list) return NULL;

	list->count = 0;
	list->expr  = NULL;

	unsigned i = parse_list(src, ptr,
		&list->count, (void***)&list->expr,
		(void*)parse_expr,
		(void*)parse_expr_delete);
	if (i == 0)
	{
		free(list);
		return NULL;
	}

	if (len) *len = i;
	return list;
}

void parse_expr_list_delete(
	parse_expr_list_t* list)
{
	if (!list)
		return;

	parse_list_delete(
		list->count, (void**)list->expr,
		(void*)parse_expr_delete);
	free(list);
}