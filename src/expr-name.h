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
ExprName *expr_name_add        (Workbook *wb, Sheet *sheet,
				const char *name,
				ExprTree *expr, char **error_msg);

/* Convenience function to parse the name */
ExprName *expr_name_create     (Workbook *wb, Sheet *sheet, const char *name,
				const char *value, char **error_msg);

/* Lookup - use sparingly */
ExprName *expr_name_lookup     (Workbook *wb, Sheet *sheet, const char *name);

/* Remove a name from its parent workbook / sheet */
void      expr_name_remove     (ExprName *exprn);

/* Destroy the local scope's names */
void      expr_name_clean_workbook  (Workbook *wb);
void      expr_name_clean_sheet     (Sheet    *sheet);

/* Get all a workbooks names */
GList    *expr_name_list       (Workbook *wb, Sheet *sheet, gboolean builtins_too);

/* Evaluate the name's expression */
Value    *eval_expr_name       (EvalPosition const * const pos, const ExprName *exprn);

char     *expr_name_value      (const ExprName *expr_name);

#endif





