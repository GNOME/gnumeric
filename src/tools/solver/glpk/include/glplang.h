/* glplang.h */

/*----------------------------------------------------------------------
-- Copyright (C) 2000, 2001, 2002 Andrew Makhorin <mao@mai2.rcnet.ru>,
--               Department for Applied Informatics, Moscow Aviation
--               Institute, Moscow, Russia. All rights reserved.
--
-- This file is a part of GLPK (GNU Linear Programming Kit).
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
-- 02111-1307, USA.
----------------------------------------------------------------------*/

#ifndef _GLPLANG_H
#define _GLPLANG_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include <setjmp.h>
#include "glpavl.h"
#include "glplib.h"
#include "glptext.h"

#define addition              glp_lang_addition
#define arith_expr            glp_lang_arith_expr
#define array_decl            glp_lang_array_decl
#define assign_stmt           glp_lang_assign_stmt
#define build_form            glp_lang_build_form
#define cmp_tuples            glp_lang_cmp_tuples
#define comparison            glp_lang_comparison
#define constant              glp_lang_constant
#define copy_expr             glp_lang_copy_expr
#define create_index          glp_lang_create_index
#define create_prob           glp_lang_create_prob
#define data_func             glp_lang_data_func
#define delete_index          glp_lang_delete_index
#define delete_prob           glp_lang_delete_prob
#define designator            glp_lang_designator
#define display_stmt          glp_lang_display_stmt
#define enclose_expr          glp_lang_enclose_expr
#define erase_expr            glp_lang_erase_expr
#define erase_form            glp_lang_erase_form
#define erase_spar            glp_lang_erase_spar
#define eval_const            glp_lang_eval_const
#define expand_spar           glp_lang_expand_spar
#define expression            glp_lang_expression
#define fatal                 glp_lang_fatal
#define find_memb             glp_lang_find_memb
#define find_mute             glp_lang_find_mute
#define gener_lp              glp_lang_gener_lp
#define gener_name            glp_lang_gener_name
#define get_token             glp_lang_get_token
#define index_memb            glp_lang_index_memb
#define infix_expr            glp_lang_infix_expr
#define initialize            glp_lang_initialize
#define load_model            glp_lang_load_model
#define logical_and           glp_lang_logical_and
#define logical_not           glp_lang_logical_not
#define logical_or            glp_lang_logical_or
#define log_primary           glp_lang_log_primary
#define log_secondary         glp_lang_log_secondary
#define make_const            glp_lang_make_const
#define make_expr             glp_lang_make_expr
#define make_refer            glp_lang_make_refer
#define multiplication        glp_lang_multiplication
#define objective             glp_lang_objective
#define outstr                glp_lang_outstr
#define parse_model           glp_lang_parse_model
#define pdb                   glp_lang_pdb
#define primary               glp_lang_primary
#define print_expr            glp_lang_print_expr
#define print_spar            glp_lang_print_spar
#define relation              glp_lang_relation
#define secondary             glp_lang_secondary
#define selection             glp_lang_selection
#define set_decl              glp_lang_set_decl
#define simple_expr           glp_lang_simple_expr
#define stack_size            glp_lang_stack_size
#define sum_func              glp_lang_sum_func
#define summation             glp_lang_summation
#define table_func            glp_lang_table_func
#define terminate             glp_lang_terminate
#define transpose             glp_lang_transpose
#define unary_op              glp_lang_unary_op
#define var_decl              glp_lang_var_decl

#define MAX_NAME 31
/* maximal length of symbolic name (except '\0') */

#define MAX_DIM 6
/* maximal dimension of sparse array */

typedef struct PDB PDB;
typedef struct SPAR SPAR;
typedef struct ITEM ITEM;
typedef struct MEMB MEMB;
typedef struct EXPR EXPR;
typedef struct CODE CODE;
typedef struct VAR VAR;
typedef struct CONS CONS;

struct PDB
{     /* primary data block */
      TEXT *text;
      /* input text stream */
      int flag;
      /* if this flag is not set, the jump address (see below) is NOT
         valid */
      jmp_buf jump;
      /* jump address for non-local goto in case of error */
      POOL *spar_pool;
      /* pool for objects of SPAR type */
      POOL *item_pool;
      /* pool for objects of ITEM type */
      POOL *memb_pool;
      /* pool for objects of MEMB type */
      POOL *expr_pool;
      /* pool for objects of EXPR type */
      POOL *code_pool;
      /* pool for objects of CODE type */
      POOL *var_pool;
      /* pool for objects of VAR type */
      POOL *cons_pool;
      /* pool for objects of CONS type */
      AVLTREE *tree;
      /* symbol table; the type field of the AVLNODE structure defines
         the type of a named object:
         'S' - index set
         'P' - predicate
         'X' - array of model expressions
         'V' - array of model variables
         'C' - array of model constraints
         'I' - element of index set
         in all cases (except type = 'I') the link field of the AVLNODE
         structure points to the object of SPAR type; in case type = 'I'
         the link field points to the object of ITEM type */
      char model_name[MAX_NAME+1];
      /* model name (informative) */
      int obj_dir;
      /* optimization direction:
         '-' - minimization
         '+' - maximization */
      SPAR *obj_spar;
      /* pointer to the array of model constraints which defines the
         objective function; if this field is NULL, objective function
         is identically equal to zero */
      MEMB *obj_memb;
      /* pointer to the member of the array of model constraints which
         defines the objective function; if the obj_spar field is NULL,
         this field is not used */
      AVLTREE *index;
      /* search tree used by array indexing routines */
      SPAR *array;
      /* pointer to the corresponding indexed array */
};

#define token (pdb->text->token)
#define image (pdb->text->image)
#define t_name(str) (token == T_NAME && strcmp(image, str) == 0)
#define t_spec(str) (token == T_SPEC && strcmp(image, str) == 0)

struct SPAR
{     /* sparse array */
      char name[MAX_NAME+1];
      /* symbolic name; if the array is an intermediate result and
         therefore not in the symbol table, this name may be used for
         arbitrary purposes */
      int type;
      /* array type:
         'S' - index set (domain)
         'P' - predicate
         'X' - array of model expressions
         'V' - array of model variables
         'C' - array of model constraints */
      int dim;
      /* dimension (0 to MAX_DIM; 0 means scalar) */
      SPAR *set[MAX_DIM];
      /* set[k] is a pointer to the corresponding index set on which
         the array is defined (k = 0, 1, ..., dim-1); in case of index
         set dim = 1 and set[0] points to the index set itself */
      int mute[MAX_DIM];
      /* mute[k] is the corresponding mute letter which is a lower-case
         letter (k = 0, 1, ..., dim-1); all mute letters are different;
         if the array is *not* an intermediate result, mute letters are
         not used */
      MEMB *first;
      /* pointer to the first array element; NULL means empty array */
      MEMB *last;
      /* pointer to the last array element; NULL means empty array */
};

struct ITEM
{     /* element of index set */
      char name[MAX_NAME+1];
      /* symbolic name */
      SPAR *set;
      /* pointer to the corresponding index set */
      ITEM *prev;
      /* pointer to the previous element of the same set */
      ITEM *next;
      /* pointer to the next element of the same set */
};

struct MEMB
{     /* element of sparse array */
      ITEM *item[MAX_DIM];
      /* item[0,...,dim-1] is a subscript list (tuple), where dim is
         dimension of the corresponding array */
      void *link;
      /* pointer to element value (depends on array type):
         'S' - not used (NULL)
         'P' - not used (NULL)
         'X' - to EXPR (can't be NULL)
         'V' - to VAR (can't be NULL)
         'C' - to CONS (can't be NULL) */
      MEMB *next;
      /* pointer to next array element */
};

struct EXPR
{     /* model expression */
      CODE *head;
      /* pointer to the first expression element (can't be NULL) */
      CODE *tail;
      /* pointer to the last expression element (can't be NULL) */
};

struct CODE
{     /* element of model expression */
      int op;
      /* operation code: */
#define C_NOP  0x00  /* no operation */
#define C_CON  0x01  /* model constant */
#define C_VAR  0x02  /* model variable */
#define C_NEG  0x03  /* unary minus */
#define C_ADD  0x04  /* addition */
#define C_SUB  0x05  /* subtraction */
#define C_MUL  0x06  /* multiplication */
#define C_DIV  0x07  /* division */
      union
      {  gnum_float con;
         /* value of model constant (op = C_CON) */
         struct
         {  SPAR *spar;
            /* pointer to array of model variables; can't be NULL */
            MEMB *memb;
            /* pointer to a particular array member; can't be NULL */
         } var;
         /* reference to model variable (op = C_VAR) */
      } arg;
      /* argument */
      CODE *next;
      /* pointer to the next expression element (the order corresponds
         to the postfix notation) */
};

/* auxiliary operation codes (never met in expressions): */
#define C_POS  0x80  /* unary plus */
#define C_LT   0x81  /* less than */
#define C_LE   0x82  /* less than or equal to */
#define C_EQ   0x83  /* equal to */
#define C_GE   0x84  /* greater than or equal to */
#define C_GT   0x85  /* greater than */
#define C_NE   0x86  /* not equal to */
#define C_LPN  0xFE  /* left parenthesis */
#define C_RPN  0xFF  /* right parenthesis */

struct VAR
{     /* model variable */
      int kind;
      /* this field specifies the kind of model variable:
         0 - continuous
         1 - integer (discrete) */
      int type;
      /* this field specifies the type of model variable:
         'F' - free variable:    -inf <  x < +inf
         'L' - lower bound:      l[k] <= x < +inf
         'U' - upper bound:      -inf <  x <= u[k]
         'D' - gnum_float bound:     l[k] <= x <= u[k]
         'S' - fixed variable:   l[k]  = x  = u[k] */
      gnum_float lb;
      /* lower bound */
      gnum_float ub;
      /* upper bound */
      int seqn;
      /* sequential number (used on problem generating phase) */
};

struct CONS
{     /* model constraint */
      EXPR *expr;
      /* expression that specifies the constraint function */
      int type;
      /* this field specifies the type of an auxiliary variable that
         is associated with the corresponding constraint; the field has
         the same meaning as in the case of model variable */
      gnum_float lb;
      /* lower bound */
      gnum_float ub;
      /* upper bound */
      int seqn;
      /* sequential number (used on problem generating phase) */
};

struct prob;
struct elem;

struct prob
{     /* data structure used by LP/MIP problem generator */
      int m;
      /* number of rows (constraints) */
      int n;
      /* number of columns (variables) */
      int size;
      /* required stack size */
      SPAR **spar; /* SPAR *spar[1+m+n]; */
      /* spar[0] is not used; spar[1,...,m] point to objects of SPAR
         type for the corresponding rows; spar[m+1,...,m+n] point to
         objects of SPAR type for the corresponding columns */
      MEMB **memb; /* MEMB *memb[1+m+n]; */
      /* memb[0] is not used; memb[1,...,m] point to objects of MEMB
         type for the corresponding rows; memb[m+1,...,m+n] point to
         objects of MEMB type for the corresponding columns */
      int obj_dir;
      /* optimization direction flag:
         '-' - minimization
         '+' - maximization */
      int obj_row;
      /* number of the objective function row (1 to m); zero indicates
         that the objective function is identically equal to zero */
      POOL *pool;
      /* memory pool for struct elem instances */
      struct elem **stack; /* struct elem *stack[1+size]; */
      /* stack used for symbolic computation */
      gnum_float *work; /* gnum_float work[1+n]; */
      /* working array used for symbolic computation */
};

struct elem
{     /* linear (affine) form element */
      int j;
      /* column number (1 to n); 0 means constant term */
      gnum_float val;
      /* value of coefficient or constant term */
      struct elem *next;
      /* pointer to the next element */
};

extern SPAR *addition(int op, SPAR *x, SPAR *y);
/* perform additive operation on sparse arrays */

extern SPAR *arith_expr(void);
/* parse arithmetic expression */

extern void array_decl(int type);
/* parse predicate, parameter, or constraint declaration */

extern void assign_stmt(void);
/* parse assignment statement */

extern int cmp_tuples(ITEM *item1[MAX_DIM], ITEM *item2[MAX_DIM]);
/* compare tuples */

extern struct elem *build_form(struct prob *prob, int i);
/* build linear from for given row (constraint) */

extern SPAR *comparison(int op, SPAR *x, SPAR *y);
/* perform arithmetic comparison of two sparse arrays */

extern SPAR *constant(void);
/* parse constant literal */

extern EXPR *copy_expr(EXPR *expr);
/* copy model expression */

extern void create_index(SPAR *spar);
/* create array index */

extern struct prob *create_prob(void);
/* create data structure for LP/MIP problem generator */

extern SPAR *data_func(void);
/* parse data() built-in function call */

extern void delete_index(void);
/* delete array index */

extern void delete_prob(struct prob *prob);
/* delete data structure for LP/MIP problem generator */

extern SPAR *designator(char *name);
/* parse array designator */

extern void display_stmt(void);
/* parse display statement */

extern EXPR *enclose_expr(EXPR *expr);
/* enclose expression in parentheses */

extern void erase_expr(EXPR *expr);
/* delete model expression */

extern void erase_form(struct prob *prob, struct elem *row);
/* delete linear form */

extern void erase_spar(SPAR *spar);
/* delete sparse array */

extern gnum_float eval_const(int op, gnum_float x, gnum_float y);
/* compute constant model expression */

extern SPAR *expand_spar(SPAR *x, SPAR *set, int i);
/* expand dimension of sparse array over given set */

extern SPAR *expression(void);
/* parse expression of general kind */

extern void fatal(char *fmt, ...);
/* print error message and terminate processing */

extern MEMB *find_memb(ITEM *item[MAX_DIM]);
/* find array member */

extern int find_mute(int dim, int mute[], int i);
/* find mute letter */

extern int gener_lp(char *fname);
/* generate LP/MIP problem in plain text format */

extern char *gener_name(struct prob *prob, int k);
/* generate plain row/column name */

extern void get_token(void);
/* scan the next token */

extern void index_memb(MEMB *memb);
/* index array member */

extern EXPR *infix_expr(EXPR *expr);
/* convert expression to infix notation */

extern int initialize(char *fname);
/* initialize the language processor environment */

extern void load_model(void);
/* load math programming model description */

extern SPAR *logical_and(SPAR *x, SPAR *y);
/* perform operation "and" on two sparse predicates */

extern SPAR *logical_not(SPAR *x);
/* perform operation "not" on sparse predicate */

extern SPAR *logical_or(SPAR *x, SPAR *y);
/* perform operation "or" on two sparse predicates */

extern SPAR *log_primary(void);
/* parse logical primary expression */

extern SPAR *log_secondary(void);
/* parse logical secondary expression */

extern EXPR *make_const(gnum_float con);
/* create model expression <constant> */

extern EXPR *make_expr(int op, EXPR *x, EXPR *y);
/* perform symbolic operation on model expressions */

extern EXPR *make_refer(SPAR *spar, MEMB *memb);
/* create model expression <variable> */

extern SPAR *multiplication(int op, SPAR *x, SPAR *y);
/* perform multiplicative operation on sparse arrays */

extern void objective(void);
/* parse objective statement */

extern void outstr(char *str);
/* wrapped buffered printing */

extern void parse_model(void);
/* parse model description */

extern PDB *pdb;
/* pointer to the primary data block */

extern SPAR *primary(void);
/* parse primary expression */

extern void print_expr(EXPR *expr);
/* format and print expression */

extern void print_spar(SPAR *spar, int temp);
/* print sparse array */

extern SPAR *relation(void);
/* parse relation expression */

extern SPAR *secondary(void);
/* parse secondary expression */

extern SPAR *selection(SPAR *x, SPAR *y);
/* perform predicate-controlled selection operation */

extern void set_decl(void);
/* parse set declaration */

extern SPAR *simple_expr(void);
/* parse simple expression */

extern int stack_size(EXPR *expr);
/* determine stack size for symbolic computation */

extern SPAR *sum_func(void);
/* parse sum() built-in function call */

extern SPAR *summation(SPAR *x, int n, int mute[MAX_DIM]);
/* perform aggregate summation over given sets */

extern SPAR *table_func(void);
/* parse table() built-in function call */

extern void terminate(void);
/* terminate the language processor environment */

extern void transpose(SPAR *spar, int mute[MAX_DIM]);
/* transpose sparse array */

extern SPAR *unary_op(int op, SPAR *x);
/* perform unary arithmetic operation on sparse array */

extern void var_decl(int kind);
/* parse variable declaration */

#endif

/* eof */
