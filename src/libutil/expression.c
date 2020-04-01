/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"
#include "expression.h"
#include "printf.h"
#include "regexp.h"
#include "util.h"
#include "utlist.h"
#include "ottery.h"
#include <math.h>

#define RSPAMD_EXPR_FLAG_NEGATE (1 << 0)
#define RSPAMD_EXPR_FLAG_PROCESSED (1 << 1)

#define MIN_RESORT_EVALS 50
#define MAX_RESORT_EVALS 150
#define DOUBLE_EPSILON 1e-9

enum rspamd_expression_elt_type {
	ELT_OP = 0,
	ELT_ATOM,
	ELT_LIMIT
};

struct rspamd_expression_elt {
	enum rspamd_expression_elt_type type;
	union {
		rspamd_expression_atom_t *atom;
		enum rspamd_expression_op op;
		gdouble lim;
	} p;

	gint flags;
	gint priority;
	gdouble value;
};

struct rspamd_expression {
	const struct rspamd_atom_subr *subr;
	GArray *expressions;
	GPtrArray *expression_stack;
	GNode *ast;
	guint next_resort;
	guint evals;
};

struct rspamd_expr_process_data {
	gpointer *ud;
	gint flags;
	/* != NULL if trace is collected */
	GPtrArray *trace;
	rspamd_expression_process_cb process_closure;
};

static GQuark
rspamd_expr_quark (void)
{
	return g_quark_from_static_string ("rspamd-expression");
}

static const gchar *
rspamd_expr_op_to_str (enum rspamd_expression_op op)
{
	const gchar *op_str = NULL;

	switch (op) {
	case OP_AND:
		op_str = "&";
		break;
	case OP_OR:
		op_str = "|";
		break;
	case OP_MULT:
		op_str = "*";
		break;
	case OP_PLUS:
		op_str = "+";
		break;
	case OP_NOT:
		op_str = "!";
		break;
	case OP_GE:
		op_str = ">=";
		break;
	case OP_GT:
		op_str = ">";
		break;
	case OP_LE:
		op_str = "<=";
		break;
	case OP_LT:
		op_str = "<";
		break;
	default:
		op_str = "???";
		break;
	}

	return op_str;
}

#define G_ARRAY_LAST(ar, type) (&g_array_index((ar), type, (ar)->len - 1))

static void
rspamd_expr_stack_elt_push (GPtrArray *stack,
		gpointer elt)
{
	g_ptr_array_add (stack, elt);
}


static gpointer
rspamd_expr_stack_elt_pop (GPtrArray *stack)
{
	gpointer e;
	gint idx;

	if (stack->len == 0) {
		return NULL;
	}

	idx = stack->len - 1;
	e = g_ptr_array_index (stack, idx);
	g_ptr_array_remove_index_fast (stack, idx);

	return e;
}


static void
rspamd_expr_stack_push (struct rspamd_expression *expr,
		gpointer elt)
{
	rspamd_expr_stack_elt_push (expr->expression_stack, elt);
}

static gpointer
rspamd_expr_stack_pop (struct rspamd_expression *expr)
{
	return rspamd_expr_stack_elt_pop (expr->expression_stack);
}

static gpointer
rspamd_expr_stack_peek (struct rspamd_expression *expr)
{
	gpointer e;
	gint idx;
	GPtrArray *stack = expr->expression_stack;

	if (stack->len == 0) {
		return NULL;
	}

	idx = stack->len - 1;
	e = g_ptr_array_index (stack, idx);

	return e;
}

/*
 * Return operation priority
 */
static gint
rspamd_expr_logic_priority (enum rspamd_expression_op op)
{
	gint ret = 0;

	switch (op) {
	case OP_NOT:
		ret = 6;
		break;
	case OP_PLUS:
		ret = 5;
		break;
	case OP_GE:
	case OP_GT:
	case OP_LE:
	case OP_LT:
		ret = 4;
		break;
	case OP_MULT:
	case OP_AND:
		ret = 3;
		break;
	case OP_OR:
		ret = 2;
		break;
	case OP_OBRACE:
	case OP_CBRACE:
		ret = 1;
		break;
	case OP_INVALID:
		ret = -1;
		break;
	}

	return ret;
}

/*
 * Return FALSE if symbol is not operation symbol (operand)
 * Return TRUE if symbol is operation symbol
 */
static gboolean
rspamd_expr_is_operation_symbol (gchar a)
{
	switch (a) {
	case '!':
	case '&':
	case '|':
	case '(':
	case ')':
	case '>':
	case '<':
	case '+':
	case '*':
		return TRUE;
	}

	return FALSE;
}

/* Return character representation of operation */
static enum rspamd_expression_op
rspamd_expr_str_to_op (const gchar *a, const gchar *end, const gchar **next)
{
	enum rspamd_expression_op op = OP_INVALID;

	g_assert (a < end);

	switch (*a) {
	case '!':
	case '&':
	case '|':
	case '+':
	case '*':
	case '(':
	case ')': {
		if (a < end - 1) {
			if ((a[0] == '&' && a[1] == '&') ||
					(a[0] == '|' && a[1] == '|')) {
				*next = a + 2;
			}
			else {
				*next = a + 1;
			}
		}
		else {
			*next = end;
		}
		/* XXX: not especially effective */
		switch (*a) {
		case '!':
			op = OP_NOT;
			break;
		case '&':
			op = OP_AND;
			break;
		case '*':
			op = OP_MULT;
			break;
		case '|':
			op = OP_OR;
			break;
		case '+':
			op = OP_PLUS;
			break;
		case ')':
			op = OP_CBRACE;
			break;
		case '(':
			op = OP_OBRACE;
			break;
		default:
			op = OP_INVALID;
			break;
		}
		break;
	}
	case 'O':
	case 'o':
		if ((gulong)(end - a) >= sizeof ("or") &&
				g_ascii_strncasecmp (a, "or", sizeof ("or") - 1) == 0) {
			*next = a + sizeof ("or") - 1;
			op = OP_OR;
		}
		break;
	case 'A':
	case 'a':
		if ((gulong)(end - a) >= sizeof ("and") &&
				g_ascii_strncasecmp (a, "and", sizeof ("and") - 1) == 0) {
			*next = a + sizeof ("and") - 1;
			op = OP_AND;
		}
		break;
	case 'N':
	case 'n':
		if ((gulong)(end - a) >= sizeof ("not") &&
				g_ascii_strncasecmp (a, "not", sizeof ("not") - 1) == 0) {
			*next = a + sizeof ("not") - 1;
			op = OP_NOT;
		}
		break;
	case '>':
		if (a < end - 1 && a[1] == '=') {
			*next = a + 2;
			op = OP_GE;
		}
		else {
			*next = a + 1;
			op = OP_GT;
		}
		break;
	case '<':
		if (a < end - 1 && a[1] == '=') {
			*next = a + 2;
			op = OP_LE;
		}
		else {
			*next = a + 1;
			op = OP_LT;
		}
		break;
	default:
		op = OP_INVALID;
		break;
	}

	return op;
}

static void
rspamd_expression_destroy (struct rspamd_expression *expr)
{
	guint i;
	struct rspamd_expression_elt *elt;

	if (expr != NULL) {

		if (expr->subr->destroy) {
			/* Free atoms */
			for (i = 0; i < expr->expressions->len; i ++) {
				elt = &g_array_index (expr->expressions,
						struct rspamd_expression_elt, i);

				if (elt->type == ELT_ATOM) {
					expr->subr->destroy (elt->p.atom);
				}
			}
		}

		if (expr->expressions) {
			g_array_free (expr->expressions, TRUE);
		}
		if (expr->expression_stack) {
			g_ptr_array_free (expr->expression_stack, TRUE);
		}
		if (expr->ast) {
			g_node_destroy (expr->ast);
		}

		g_free (expr);
	}
}

static gboolean
rspamd_ast_add_node (GPtrArray *operands, struct rspamd_expression_elt *op,
		GError **err)
{

	GNode *res, *a1, *a2, *test;
	struct rspamd_expression_elt *test_elt;

	g_assert (op->type == ELT_OP);

	if (op->p.op == OP_NOT) {
		/* Unary operator */
		res = g_node_new (op);
		a1 = rspamd_expr_stack_elt_pop (operands);

		if (a1 == NULL) {
			g_set_error (err, rspamd_expr_quark(), EINVAL, "no operand to "
					"unary '%s' operation", rspamd_expr_op_to_str (op->p.op));
			g_node_destroy (res);

			return FALSE;
		}

		g_node_append (res, a1);
		test_elt = a1->data;

		if (test_elt->type == ELT_ATOM) {
			test_elt->p.atom->parent = res;
		}
	}
	else {
		/* For binary operators we might want to examine chains */
		a2 = rspamd_expr_stack_elt_pop (operands);
		a1 = rspamd_expr_stack_elt_pop (operands);

		if (a2 == NULL) {
			g_set_error (err, rspamd_expr_quark(), EINVAL, "no left operand to "
					"'%s' operation", rspamd_expr_op_to_str (op->p.op));
			return FALSE;
		}
		if (a1 == NULL) {
			g_set_error (err, rspamd_expr_quark(), EINVAL, "no right operand to "
					"'%s' operation", rspamd_expr_op_to_str (op->p.op));
			return FALSE;
		}

		/* First try with a1 */
		test = a1;
		test_elt = test->data;

		if (test_elt->type == ELT_OP && test_elt->p.op == op->p.op) {
			/* Add children */
			g_node_append (test, a2);
			rspamd_expr_stack_elt_push (operands, a1);
			return TRUE;
		}

		/* Now test a2 */
		test = a2;
		test_elt = test->data;

		if (test_elt->type == ELT_OP && test_elt->p.op == op->p.op) {
			/* Add children */
			g_node_prepend (test, a1);
			rspamd_expr_stack_elt_push (operands, a2);
			return TRUE;
		}

		/* No optimizations possible, so create new level */
		res = g_node_new (op);
		g_node_append (res, a1);
		g_node_append (res, a2);

		test_elt = a1->data;
		if (test_elt->type == ELT_ATOM) {
			test_elt->p.atom->parent = res;
		}

		test_elt = a2->data;
		if (test_elt->type == ELT_ATOM) {
			test_elt->p.atom->parent = res;
		}
	}

	/* Push back resulting node to the stack */
	rspamd_expr_stack_elt_push (operands, res);

	return TRUE;
}

static gboolean
rspamd_ast_priority_traverse (GNode *node, gpointer d)
{
	struct rspamd_expression_elt *elt = node->data, *cur_elt;
	struct rspamd_expression *expr = d;
	gint cnt = 0;
	GNode *cur;

	if (node->children) {
		cur = node->children;
		while (cur) {
			cur_elt = cur->data;
			cnt += cur_elt->priority;
			cur = cur->next;
		}
		elt->priority = cnt;
	}
	else {
		/* It is atom or limit */
		g_assert (elt->type != ELT_OP);

		if (elt->type == ELT_LIMIT) {
			/* Always push limit first */
			elt->priority = 0;
		}
		else {
			elt->priority = RSPAMD_EXPRESSION_MAX_PRIORITY;

			if (expr->subr->priority != NULL) {
				elt->priority = RSPAMD_EXPRESSION_MAX_PRIORITY -
						expr->subr->priority (elt->p.atom);
			}
			elt->p.atom->hits = 0;
			elt->p.atom->avg_ticks = 0.0;
		}
	}

	return FALSE;
}

#define ATOM_PRIORITY(a) ((a)->p.atom->hits / ((a)->p.atom->avg_ticks > 0 ?	\
				(a)->p.atom->avg_ticks * 10000000 : 1.0))

static gint
rspamd_ast_priority_cmp (GNode *a, GNode *b)
{
	struct rspamd_expression_elt *ea = a->data, *eb = b->data;
	gdouble w1, w2;

	if (ea->type == ELT_LIMIT) {
		return -1;
	}
	else if (eb->type == ELT_LIMIT) {
		return 1;
	}

	/* Special logic for atoms */
	if (ea->type == ELT_ATOM && eb->type == ELT_ATOM &&
			ea->priority == eb->priority) {
		w1 = ATOM_PRIORITY (ea);
		w2 = ATOM_PRIORITY (eb);

		ea->p.atom->hits = 0;
		ea->p.atom->avg_ticks = 0.0;

		return w1 - w2;
	}
	else {
		return ea->priority - eb->priority;
	}
}

static gboolean
rspamd_ast_resort_traverse (GNode *node, gpointer unused)
{
	GNode *children, *last;

	if (node->children) {

		children = node->children;
		last = g_node_last_sibling (children);
		/* Needed for utlist compatibility */
		children->prev = last;
		DL_SORT (node->children, rspamd_ast_priority_cmp);
		/* Restore GLIB compatibility */
		children = node->children;
		children->prev = NULL;
	}

	return FALSE;
}

static struct rspamd_expression_elt *
rspamd_expr_dup_elt (rspamd_mempool_t *pool, struct rspamd_expression_elt *elt)
{
	struct rspamd_expression_elt *n;

	n = rspamd_mempool_alloc (pool, sizeof (*n));
	memcpy (n, elt, sizeof (*n));

	return n;
}

gboolean
rspamd_parse_expression (const gchar *line, gsize len,
		const struct rspamd_atom_subr *subr, gpointer subr_data,
		rspamd_mempool_t *pool, GError **err,
		struct rspamd_expression **target)
{
	struct rspamd_expression *e;
	struct rspamd_expression_elt elt;
	rspamd_expression_atom_t *atom;
	rspamd_regexp_t *num_re;
	enum rspamd_expression_op op, op_stack;
	const gchar *p, *c, *end;
	GPtrArray *operand_stack;
	GNode *tmp;

	enum {
		PARSE_ATOM = 0,
		PARSE_OP,
		PARSE_LIM,
		SKIP_SPACES
	} state = PARSE_ATOM;

	g_assert (line != NULL);
	g_assert (subr != NULL && subr->parse != NULL);

	if (len == 0) {
		len = strlen (line);
	}

	memset (&elt, 0, sizeof (elt));
	num_re = rspamd_regexp_cache_create (NULL,
			"/^(?:[+-]?([0-9]*[.])?[0-9]+)(?:\\s+|[)]|$)/", NULL, NULL);

	p = line;
	c = line;
	end = line + len;
	e = g_malloc0 (sizeof (*e));
	e->expressions = g_array_new (FALSE, FALSE,
			sizeof (struct rspamd_expression_elt));
	operand_stack = g_ptr_array_sized_new (32);
	e->ast = NULL;
	e->expression_stack = g_ptr_array_sized_new (32);
	e->subr = subr;
	e->evals = 0;
	e->next_resort = ottery_rand_range (MAX_RESORT_EVALS) + MIN_RESORT_EVALS;

	/* Shunting-yard algorithm */
	while (p < end) {
		switch (state) {
		case PARSE_ATOM:
			if (g_ascii_isspace (*p)) {
				state = SKIP_SPACES;
				continue;
			}
			else if (rspamd_expr_is_operation_symbol (*p)) {
				if (p + 1 < end) {
					gchar t = *(p + 1);

					if (t != ':') {
						state = PARSE_OP;
						continue;
					}
				}
				else {
					state = PARSE_OP;
					continue;
				}
			}

			/*
			 * First of all, we check some pre-conditions:
			 * 1) if we have 'and ' or 'or ' or 'not ' strings, they are op
			 * 2) if we have full numeric string, then we check for
			 * the following expression:
			 *  ^\d+\s*[><]$
			 */
			if ((gulong)(end - p) > sizeof ("and ") &&
				(g_ascii_strncasecmp (p, "and ", sizeof ("and ") - 1) == 0 ||
				 g_ascii_strncasecmp (p, "not ", sizeof ("not ") - 1) == 0 )) {
				state = PARSE_OP;
			}
			else if ((gulong)(end - p) > sizeof ("or ") &&
					 g_ascii_strncasecmp (p, "or ", sizeof ("or ") - 1) == 0) {
				state = PARSE_OP;
			}
			else {
				/*
				 * If we have any comparison operator in the stack, then try
				 * to parse limit
				 */
				op = GPOINTER_TO_INT (rspamd_expr_stack_peek (e));

				if (op >= OP_LT && op <= OP_GE) {
					if (rspamd_regexp_search (num_re,
							p,
							end - p,
							NULL,
							NULL,
							FALSE,
							NULL)) {
						c = p;
						state = PARSE_LIM;
						continue;
					}
				}

				/* Try to parse atom */
				atom = subr->parse (p, end - p, pool, subr_data, err);
				if (atom == NULL || atom->len == 0) {
					/* We couldn't parse the atom, so go out */
					if (err != NULL && *err == NULL) {
						g_set_error (err,
								rspamd_expr_quark (),
								500,
								"Cannot parse atom: callback function failed"
								" to parse '%.*s'",
								(int) (end - p),
								p);
					}
					goto err;
				}

				if (atom->str == NULL) {
					atom->str = p;
				}

				p = p + atom->len;

				/* Push to output */
				elt.type = ELT_ATOM;
				elt.p.atom = atom;
				g_array_append_val (e->expressions, elt);
				rspamd_expr_stack_elt_push (operand_stack,
						g_node_new (rspamd_expr_dup_elt (pool, &elt)));

			}
			break;
		case PARSE_LIM:
			if ((g_ascii_isdigit (*p) || *p == '-' || *p == '.')
					&& p < end - 1) {
				p ++;
			}
			else {
				if (p == end - 1 && g_ascii_isdigit (*p)) {
					p ++;
				}

				if (p - c > 0) {
					elt.type = ELT_LIMIT;
					elt.p.lim = strtod (c, NULL);
					g_array_append_val (e->expressions, elt);
					rspamd_expr_stack_elt_push (operand_stack,
							g_node_new (rspamd_expr_dup_elt (pool, &elt)));
					c = p;
					state = SKIP_SPACES;
				}
				else {
					g_set_error (err, rspamd_expr_quark(), 400, "Empty number");
					goto err;
				}
			}
			break;
		case PARSE_OP:
			op = rspamd_expr_str_to_op (p, end, &p);
			if (op == OP_INVALID) {
				g_set_error (err, rspamd_expr_quark(), 500, "Bad operator %c",
						*p);
				goto err;
			}
			else if (op == OP_OBRACE) {
				/*
				 * If the token is a left parenthesis, then push it onto
				 * the stack.
				 */
				rspamd_expr_stack_push (e, GINT_TO_POINTER (op));
			}
			else if (op == OP_CBRACE) {
				/*
				 * Until the token at the top of the stack is a left
				 * parenthesis, pop operators off the stack onto the
				 * output queue.
				 *
				 * Pop the left parenthesis from the stack,
				 * but not onto the output queue.
				 *
				 * If the stack runs out without finding a left parenthesis,
				 * then there are mismatched parentheses.
				 */
				do {
					op = GPOINTER_TO_INT (rspamd_expr_stack_pop (e));

					if (op == OP_INVALID) {
						g_set_error (err, rspamd_expr_quark(), 600,
								"Braces mismatch");
						goto err;
					}

					if (op != OP_OBRACE) {
						elt.type = ELT_OP;
						elt.p.op = op;
						g_array_append_val (e->expressions, elt);
						if (!rspamd_ast_add_node (operand_stack,
								rspamd_expr_dup_elt (pool, &elt), err)) {
							goto err;
						}
					}

				} while (op != OP_OBRACE);
			}
			else {
				/*
				 * While there is an operator token, o2, at the top of
				 * the operator stack, and either:
				 *
				 * - o1 is left-associative and its precedence is less than
				 * or equal to that of o2, or
				 * - o1 is right associative, and has precedence less than
				 * that of o2,
				 *
				 * then pop o2 off the operator stack, onto the output queue;
				 *
				 * push o1 onto the operator stack.
				 */

				for (;;) {
					op_stack = GPOINTER_TO_INT (rspamd_expr_stack_pop (e));

					if (op_stack == OP_INVALID) {
						/* Stack is empty */
						break;
					}

					/* We ignore associativity for now */
					if (op_stack != OP_OBRACE &&
							rspamd_expr_logic_priority (op) <
							rspamd_expr_logic_priority (op_stack)) {
						elt.type = ELT_OP;
						elt.p.op = op_stack;
						g_array_append_val (e->expressions, elt);
						if (!rspamd_ast_add_node (operand_stack,
								rspamd_expr_dup_elt (pool, &elt), err)) {
							goto err;
						}
					}
					else {
						/* Push op_stack back */
						rspamd_expr_stack_push (e, GINT_TO_POINTER (op_stack));
						break;
					}
				}

				/* Push new operator itself */
				rspamd_expr_stack_push (e, GINT_TO_POINTER (op));
			}

			state = SKIP_SPACES;
			break;
		case SKIP_SPACES:
			if (g_ascii_isspace (*p)) {
				p ++;
			}
			else if (rspamd_expr_is_operation_symbol (*p)) {
				state = PARSE_OP;
			}
			else {
				state = PARSE_ATOM;
			}
			break;
		}
	}

	/* Now we process the stack and push operators to the output */
	while ((op_stack = GPOINTER_TO_INT (rspamd_expr_stack_pop (e)))
			!= OP_INVALID) {
		if (op_stack != OP_OBRACE) {
			elt.type = ELT_OP;
			elt.p.op = op_stack;
			g_array_append_val (e->expressions, elt);
			if (!rspamd_ast_add_node (operand_stack,
					rspamd_expr_dup_elt (pool, &elt), err)) {
				goto err;
			}
		}
		else {
			g_set_error (err, rspamd_expr_quark(), 600,
					"Braces mismatch");
			goto err;
		}
	}

	if (operand_stack->len != 1) {
		g_set_error (err, rspamd_expr_quark(), 601,
			"Operators mismatch");
		goto err;
	}

	e->ast = rspamd_expr_stack_elt_pop (operand_stack);
	g_ptr_array_free (operand_stack, TRUE);

	/* Set priorities for branches */
	g_node_traverse (e->ast, G_POST_ORDER, G_TRAVERSE_ALL, -1,
			rspamd_ast_priority_traverse, e);

	/* Now set less expensive branches to be evaluated first */
	g_node_traverse (e->ast, G_POST_ORDER, G_TRAVERSE_NON_LEAVES, -1,
			rspamd_ast_resort_traverse, NULL);

	if (target) {
		*target = e;
		rspamd_mempool_add_destructor (pool,
			(rspamd_mempool_destruct_t)rspamd_expression_destroy, e);
	}
	else {
		rspamd_expression_destroy (e);
	}

	return TRUE;

err:
	while ((tmp = rspamd_expr_stack_elt_pop (operand_stack)) != NULL) {
		g_node_destroy (tmp);
	}

	g_ptr_array_free (operand_stack, TRUE);
	rspamd_expression_destroy (e);

	return FALSE;
}

static gboolean
rspamd_ast_node_done (struct rspamd_expression_elt *elt,
		struct rspamd_expression_elt *parelt, gdouble acc, gdouble lim)
{
	gboolean ret = FALSE;

	g_assert (elt->type == ELT_OP);

	switch (elt->p.op) {
	case OP_NOT:
		ret = TRUE;
		break;
	case OP_PLUS:
		if (parelt && lim > 0) {
			g_assert (parelt->type == ELT_OP);

			switch (parelt->p.op) {
			case OP_GE:
				ret = acc >= lim;
				break;
			case OP_GT:
				ret = acc > lim;
				break;
			case OP_LE:
				ret = acc <= lim;
				break;
			case OP_LT:
				ret = acc < lim;
				break;
			default:
				ret = FALSE;
				break;
			}
		}
		break;
	case OP_GE:
		ret = acc >= lim;
		break;
	case OP_GT:
		ret = acc > lim;
		break;
	case OP_LE:
		ret = acc <= lim;
		break;
	case OP_LT:
		ret = acc < lim;
		break;
	case OP_MULT:
	case OP_AND:
		ret = !acc;
		break;
	case OP_OR:
		ret = !!acc;
		break;
	default:
		g_assert (0);
		break;
	}

	return ret;
}

static gdouble
rspamd_ast_do_op (struct rspamd_expression_elt *elt, gdouble val,
		gdouble acc, gdouble lim, gboolean first_elt)
{
	gdouble ret = val;

	g_assert (elt->type == ELT_OP);

	switch (elt->p.op) {
	case OP_NOT:
		ret = fabs (val) > DOUBLE_EPSILON ? 0.0 : 1.0;
		break;
	case OP_PLUS:
		ret = acc + val;
		break;
	case OP_GE:
		ret = first_elt ? (val >= lim) : (acc >= lim);
		break;
	case OP_GT:
		ret = first_elt ? (val > lim) : (acc > lim);
		break;
	case OP_LE:
		ret = first_elt ? (val <= lim) : (acc <= lim);
		break;
	case OP_LT:
		ret = first_elt ? (val < lim) : (acc < lim);
		break;
	case OP_MULT:
	case OP_AND:
		ret = first_elt ? (val) : (acc * val);
		break;
	case OP_OR:
		ret = first_elt ? (val) : (acc + val);
		break;
	default:
		g_assert (0);
		break;
	}

	return ret;
}

static gdouble
rspamd_ast_process_node (struct rspamd_expression *expr, GNode *node,
						 struct rspamd_expr_process_data *process_data)
{
	struct rspamd_expression_elt *elt, *celt, *parelt = NULL;
	GNode *cld;
	gdouble acc = NAN, lim = 0;
	gdouble t1, t2, val;
	gboolean calc_ticks = FALSE;

	elt = node->data;

	switch (elt->type) {
	case ELT_ATOM:
		if (!(elt->flags & RSPAMD_EXPR_FLAG_PROCESSED)) {

			/*
			 * Sometimes get ticks for this expression. 'Sometimes' here means
			 * that we get lowest 5 bits of the counter `evals` and 5 bits
			 * of some shifted address to provide some sort of jittering for
			 * ticks evaluation
			 */
			if ((expr->evals & 0x1F) == (GPOINTER_TO_UINT (node) >> 4 & 0x1F)) {
				calc_ticks = TRUE;
				t1 = rspamd_get_ticks (TRUE);
			}

			elt->value = process_data->process_closure (process_data->ud, elt->p.atom);

			if (fabs (elt->value) > 1e-9) {
				elt->p.atom->hits ++;

				if (process_data->trace) {
					g_ptr_array_add (process_data->trace, elt->p.atom);
				}
			}

			if (calc_ticks) {
				t2 = rspamd_get_ticks (TRUE);
				elt->p.atom->avg_ticks += ((t2 - t1) - elt->p.atom->avg_ticks) /
						(expr->evals);
			}

			elt->flags |= RSPAMD_EXPR_FLAG_PROCESSED;
		}

		acc = elt->value;
		break;
	case ELT_LIMIT:

		acc = elt->p.lim;
		break;
	case ELT_OP:
		g_assert (node->children != NULL);

		/* Try to find limit at the parent node */
		if (node->parent) {
			parelt = node->parent->data;
			celt = node->parent->children->data;

			if (celt->type == ELT_LIMIT) {
				lim = celt->p.lim;
			}
		}

		DL_FOREACH (node->children, cld) {
			celt = cld->data;

			/* Save limit if we've found it */
			if (celt->type == ELT_LIMIT) {
				lim = celt->p.lim;
				continue;
			}

			val = rspamd_ast_process_node (expr, cld, process_data);

			if (isnan (acc)) {
				acc = rspamd_ast_do_op (elt, val, 0, lim, TRUE);
			}
			else {
				acc = rspamd_ast_do_op (elt, val, acc, lim, FALSE);
			}

			if (!(process_data->flags & RSPAMD_EXPRESSION_FLAG_NOOPT)) {
				if (rspamd_ast_node_done (elt, parelt, acc, lim)) {
					return acc;
				}
			}
		}
		break;
	}

	return acc;
}

static gboolean
rspamd_ast_cleanup_traverse (GNode *n, gpointer d)
{
	struct rspamd_expression_elt *elt = n->data;

	elt->value = 0;
	elt->flags = 0;

	return FALSE;
}

gdouble
rspamd_process_expression_closure (struct rspamd_expression *expr,
								   rspamd_expression_process_cb cb,
								   gint flags,
								   gpointer runtime_ud,
								   GPtrArray **track)
{
	struct rspamd_expr_process_data pd;
	gdouble ret = 0;

	g_assert (expr != NULL);
	/* Ensure that stack is empty at this point */
	g_assert (expr->expression_stack->len == 0);

	expr->evals ++;

	memset (&pd, 0, sizeof (pd));
	pd.process_closure = cb;
	pd.flags = flags;
	pd.ud = runtime_ud;

	if (track) {
		pd.trace = g_ptr_array_sized_new (32);
		*track = pd.trace;
	}

	ret = rspamd_ast_process_node (expr, expr->ast, &pd);

	/* Cleanup */
	g_node_traverse (expr->ast, G_IN_ORDER, G_TRAVERSE_ALL, -1,
			rspamd_ast_cleanup_traverse, NULL);

	/* Check if we need to resort */
	if (expr->evals % expr->next_resort == 0) {
		expr->next_resort = ottery_rand_range (MAX_RESORT_EVALS) +
				MIN_RESORT_EVALS;
		/* Set priorities for branches */
		g_node_traverse (expr->ast, G_POST_ORDER, G_TRAVERSE_ALL, -1,
				rspamd_ast_priority_traverse, expr);

		/* Now set less expensive branches to be evaluated first */
		g_node_traverse (expr->ast, G_POST_ORDER, G_TRAVERSE_NON_LEAVES, -1,
				rspamd_ast_resort_traverse, NULL);
	}

	return ret;
}

gdouble
rspamd_process_expression_track (struct rspamd_expression *expr,
								 gint flags,
								 gpointer runtime_ud,
								 GPtrArray **track)
{
	return rspamd_process_expression_closure (expr,
			expr->subr->process, flags, runtime_ud, track);
}

gdouble
rspamd_process_expression (struct rspamd_expression *expr,
						   gint flags,
						   gpointer runtime_ud)
{
	return rspamd_process_expression_closure (expr,
			expr->subr->process, flags, runtime_ud, NULL);
}

static gboolean
rspamd_ast_string_traverse (GNode *n, gpointer d)
{
	GString *res = d;
	gint cnt;
	GNode *cur;
	struct rspamd_expression_elt *elt = n->data;
	const char *op_str = NULL;

	if (elt->type == ELT_ATOM) {
		rspamd_printf_gstring (res, "(%*s)",
				(int)elt->p.atom->len, elt->p.atom->str);
	}
	else if (elt->type == ELT_LIMIT) {
		if (elt->p.lim == (double)(gint64)elt->p.lim) {
			rspamd_printf_gstring (res, "%L", (gint64)elt->p.lim);
		}
		else {
			rspamd_printf_gstring (res, "%f", elt->p.lim);
		}
	}
	else {
		op_str = rspamd_expr_op_to_str (elt->p.op);
		g_string_append (res, op_str);

		if (n->children) {
			LL_COUNT(n->children, cur, cnt);

			if (cnt > 2) {
				/* Print n-ary of the operator */
				g_string_append_printf (res, "(%d)", cnt);
			}
		}
	}

	g_string_append_c (res, ' ');

	return FALSE;
}

GString *
rspamd_expression_tostring (struct rspamd_expression *expr)
{
	GString *res;

	g_assert (expr != NULL);

	res = g_string_new (NULL);
	g_node_traverse (expr->ast, G_POST_ORDER, G_TRAVERSE_ALL, -1,
			rspamd_ast_string_traverse, res);

	/* Last space */
	if (res->len > 0) {
		g_string_erase (res, res->len - 1, 1);
	}

	return res;
}

struct atom_foreach_cbdata {
	rspamd_expression_atom_foreach_cb cb;
	gpointer cbdata;
};

static gboolean
rspamd_ast_atom_traverse (GNode *n, gpointer d)
{
	struct atom_foreach_cbdata *data = d;
	struct rspamd_expression_elt *elt = n->data;
	rspamd_ftok_t tok;

	if (elt->type == ELT_ATOM) {
		tok.begin = elt->p.atom->str;
		tok.len = elt->p.atom->len;

		data->cb (&tok, data->cbdata);
	}

	return FALSE;
}

void
rspamd_expression_atom_foreach (struct rspamd_expression *expr,
		rspamd_expression_atom_foreach_cb cb, gpointer cbdata)
{
	struct atom_foreach_cbdata data;

	g_assert (expr != NULL);

	data.cb = cb;
	data.cbdata = cbdata;
	g_node_traverse (expr->ast, G_POST_ORDER, G_TRAVERSE_ALL, -1,
			rspamd_ast_atom_traverse, &data);
}

gboolean
rspamd_expression_node_is_op (GNode *node, enum rspamd_expression_op op)
{
	struct rspamd_expression_elt *elt;

	g_assert (node != NULL);

	elt = node->data;

	if (elt->type == ELT_OP && elt->p.op == op) {
		return TRUE;
	}

	return FALSE;
}
