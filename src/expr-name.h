/*
 * expr-name.h:  Workbook name table support
 *
 * Author:
 *    Michael Meeks <michael@imaginator.com>
 *
 */

#ifndef GNUMERIC_EXPR_NAME_H
#define GNUMERIC_EXPR_NAME_H

/* Initialise builtins */
void      expr_name_init       (void);

/* Attach a name to a workbook */
ExprName *expr_name_add        (Workbook *wb, const char *name,
				ExprTree *expr, char **error_msg);

/* Can only be used when we have a current sheet */
ExprName *expr_name_create     (Workbook *wb, const char *name,
				const char *value, char **error_msg);

/* Lookup - use sparingly */
ExprName *expr_name_lookup     (Workbook *wb, const char *name);

/* Remove a name from a workbook */
void      expr_name_remove     (ExprName *exprn);

/* Remove a workbook's names */
void      expr_name_clean      (Workbook *wb);

/* Get all a workbooks names */
GList    *expr_name_list       (Workbook *wb, gboolean builtins_too);

/* Evaluate the name's expression */
Value    *eval_expr_name       (FunctionEvalInfo *ei, const ExprName *exprn);

char     *expr_name_value      (const ExprName *expr_name);

#endif





