#ifndef GNUMERIC_VALUE_H
#define GNUMERIC_VALUE_H

#include <glib.h>
#include "gnumeric.h"
#include "sheet.h"
#include "numbers.h"
#include "str.h"
#include "position.h"

typedef enum {
	/* Use magic values to act as a signature
	 * DO NOT CHANGE THESE NUMBERS
	 * As of version 0.57 they are using as keys
	 * in the xml
	 */
	VALUE_EMPTY	= 10,
	VALUE_BOOLEAN	= 20, /* Keep bool < int < float */
	VALUE_INTEGER	= 30,
	VALUE_FLOAT	= 40,
	VALUE_ERROR	= 50,
	VALUE_STRING	= 60,
	VALUE_CELLRANGE = 70,
	VALUE_ARRAY	= 80
} ValueType;

struct _ValueBool {
	ValueType const type;
	gboolean val;
};
struct _ValueInt {
	ValueType const type;
	gnum_int val;
};
struct _ValueFloat {
	ValueType const type;
	gnum_float val;
};
struct _ValueErr {
	ValueType const type;
	String       *mesg;
	/* Currently unused.  Intended to support audit functions */
	EvalPos  src;
};
struct _ValueStr {
	ValueType const type;
	String *val;
};
struct _ValueRange {
	ValueType const type;
	RangeRef cell;
};
struct _ValueArray {
	ValueType const type;
	int x, y;
	Value ***vals;  /* Array [x][y] */
};

union _Value {
	ValueType const type;
	ValueBool	v_bool;
	ValueInt	v_int;
	ValueFloat	v_float;
	ValueErr	v_err;
	ValueStr	v_str;
	ValueRange	v_range;
	ValueArray	v_array;
};

#define VALUE_IS_EMPTY(v)		(((v) == NULL) || ((v)->type == VALUE_EMPTY))
#define VALUE_IS_EMPTY_OR_ERROR(v)	(VALUE_IS_EMPTY(v) || (v)->type == VALUE_ERROR)
#define VALUE_IS_NUMBER(v)		(((v)->type == VALUE_INTEGER) || \
					 ((v)->type == VALUE_FLOAT) || \
					 ((v)->type == VALUE_BOOLEAN))

typedef enum {
	IS_EQUAL,
	IS_LESS,
	IS_GREATER,
	TYPE_MISMATCH
} ValueCompare;

Value       *value_new_empty            (void);
Value       *value_new_bool             (gboolean b);
Value       *value_new_int              (int i);
Value       *value_new_float            (gnum_float f);
Value       *value_new_error            (EvalPos const *pos, char const *mesg);
Value       *value_new_error_str        (EvalPos const *pos, String *mesg);
Value       *value_new_error_err        (EvalPos const *pos, ValueErr *err);
Value       *value_new_string           (const char *str);
Value       *value_new_string_str       (String *str);
Value       *value_new_cellrange_unsafe (const CellRef *a, const CellRef *b);
Value       *value_new_cellrange        (const CellRef *a, const CellRef *b,
				         int const eval_col, int const eval_row);
Value       *value_new_cellrange_r      (Sheet *sheet, const Range *r);
Value       *value_new_array            (guint cols, guint rows);
Value       *value_new_array_empty      (guint cols, guint rows);
Value 	    *value_new_array_non_init   (guint cols, guint rows);
Value 	    *value_new_from_string	(ValueType t, const char *str);

void         value_release         (Value *value);
void         value_dump            (Value const *value);
Value       *value_duplicate       (Value const *value);
ValueCompare value_compare         (const Value *a, const Value *b,
				    gboolean case_sensitive);

gboolean     value_get_as_bool         (Value const *v, gboolean *err);
gboolean     value_get_as_checked_bool (Value const *v);
char        *value_get_as_string       (const Value *value);
const char  *value_peek_string         (const Value *v);
int          value_get_as_int          (const Value *v);
gnum_float      value_get_as_float        (const Value *v);
char        *value_cellrange_get_as_string (const Value *value,
					    gboolean use_relative_syntax);

/* Return a Special error value indicating that the iteration should stop */
Value       *value_terminate (void);

/* Area functions ( works on VALUE_RANGE or VALUE_ARRAY */
/* The EvalPos provides a Sheet context; this allows
   calculation of relative references. 'x','y' give the position */
int          value_area_get_width  (const EvalPos *ep, Value const *v);
int          value_area_get_height (const EvalPos *ep, Value const *v);

/* Return Value(int 0) if non-existant */
const Value *value_area_fetch_x_y  (const EvalPos *ep, Value const * v,
				    int x, int y);

/* Return NULL if non-existant */
const Value * value_area_get_x_y (const EvalPos *ep, Value const * v,
				  int x, int y);

typedef  Value * (*value_area_foreach_callback)(EvalPos const *ep,
						Value const *v, void *user_data);

Value * value_area_foreach (EvalPos const *ep, Value const *v,
			    value_area_foreach_callback callback,
			    void *closure);

void value_array_set       (Value *array, int col, int row, Value *v);
void value_array_resize    (Value *v, int width, int height);

/* Some utility constants to make sure we all spell correctly */
extern char const *gnumeric_err_NULL;
extern char const *gnumeric_err_DIV0;
extern char const *gnumeric_err_VALUE;
extern char const *gnumeric_err_REF;
extern char const *gnumeric_err_NAME;
extern char const *gnumeric_err_NUM;
extern char const *gnumeric_err_NA;
extern char const *gnumeric_err_RECALC;

#endif /* GNUMERIC_VALUE_H */
