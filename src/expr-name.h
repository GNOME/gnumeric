#ifndef GNUMERIC_EXPR_NAME_H
#define GNUMERIC_EXPR_NAME_H

#include "gnumeric.h"
#include "expr.h"
#include "parse-util.h"

/* Initialise builtins */
void      expr_name_init       (void);

/* Attach a name to a workbook */
NamedExpression *expr_name_add        (Workbook *wb, Sheet *sheet, const char *name,
				       ExprTree *expr, char **error_msg);

/* Convenience function to parse the name */
NamedExpression *expr_name_create     (Workbook *wb, Sheet *sheet, const char *name,
				       const char *value, ParseError *error);

/* Lookup - use sparingly */
NamedExpression *expr_name_lookup     (const ParsePos *pos, const char *name);

/* Remove a name from its parent workbook / sheet */
void      expr_name_remove     (NamedExpression *exprn);

/* Destroy the local scope's names */
GList	 *expr_name_list_destroy (GList *names);

/* Scan ALL names in the application, and invalidate any to the target */
void      expr_name_invalidate_refs_name (NamedExpression *exprn);
void      expr_name_invalidate_refs_sheet (const Sheet *sheet);
void      expr_name_invalidate_refs_wb (const Workbook *wb);

/* Get all a workbooks names */
GList    *expr_name_list       (Workbook *wb, Sheet *sheet, gboolean builtins_too);

/* Evaluate the name's expression */
Value    *eval_expr_name       (EvalPos const * const pos, const NamedExpression *exprn,
				ExprEvalFlags const flags);

char     *expr_name_value      (const NamedExpression *expr_name);

/* Change the scope of a NamedExpression */
gboolean expr_name_sheet2wb (NamedExpression *expression);
gboolean expr_name_wb2sheet (NamedExpression *expression, Sheet *sheet);

#endif /* GNUMERIC_EXPR_NAME_H */
