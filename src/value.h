#ifndef GNUMERIC_VALUE_H
#define GNUMERIC_VALUE_H

#include <glib.h>
#include "gnumeric.h"
#include "sheet.h"
#include "numbers.h"
#include "str.h"

typedef enum {
	/* Use magic values to act as a signature */
	VALUE_EMPTY	= 10,
	VALUE_BOOLEAN	= 20, /* Keep bool < int < float */
	VALUE_INTEGER	= 30,
	VALUE_FLOAT	= 40,
	VALUE_ERROR	= 50,
	VALUE_STRING	= 60,
	VALUE_CELLRANGE = 70,
	VALUE_ARRAY	= 80
} ValueType;

struct _ErrorMessage {
	String       *mesg;
	EvalPosition  src; /* Will be used for audit functions */
};

struct _Value {
	ValueType type;
	union {
		/*
		 * This element is used as a short hand for cell_range.cell_a
		 */
		CellRef cell;
		struct {
			CellRef cell_a;
			CellRef cell_b;
		} cell_range;

		struct {
			int x, y;
			Value ***vals;  /* Array [x][y] */
		} array;
		String      *str;
		float_t      v_float;	/* floating point */
		int_t        v_int;
		gboolean     v_bool;
		ErrorMessage error;
	} v;
};

#define VALUE_IS_NUMBER(x) (((x)->type == VALUE_INTEGER) || \
			    ((x)->type == VALUE_FLOAT) || \
			    ((x)->type == VALUE_BOOLEAN))

#define VALUE_IS_EMPTY_OR_ERROR(x) (value_is_empty_cell (x) || \
				    ((x)->type == VALUE_ERROR))

Value       *value_new_empty       (void);
Value       *value_new_bool        (gboolean b);
Value       *value_new_error       (EvalPosition const *pos, char const *mesg);
Value       *value_new_string      (const char *str);
Value       *value_new_int         (int i);
Value       *value_new_float       (float_t f);
Value       *value_new_cellrange   (const CellRef *a, const CellRef *b,
				    int const eval_col, int const eval_row);
Value       *value_new_cellrange_r (Sheet *sheet, const Range *r);
Value       *value_new_array       (guint cols, guint rows);
Value       *value_new_array_empty (guint cols, guint rows);

void         value_release         (Value *value);
void         value_dump            (Value const *value);
Value       *value_duplicate       (Value const *value);
void         value_copy_to         (Value *dest, Value const *source);
gboolean     value_equal           (const Value *a, const Value *b);

gboolean     value_get_as_bool     (Value const *v, gboolean *err);
char        *value_get_as_string   (const Value *value);
int          value_get_as_int      (const Value *v);
float_t      value_get_as_float    (const Value *v);
char        *value_cellrange_get_as_string (const Value *value,
					    gboolean use_relative_syntax);

/* Does the value correspond to an empty cell ? */
gboolean     value_is_empty_cell (Value const *v);

/* Return a Special error value indicating that the iteration should stop */
Value       *value_terminate (void);

/* Area functions ( works on VALUE_RANGE or VALUE_ARRAY */
/* The EvalPosition provides a Sheet context; this allows
   calculation of relative references. 'x','y' give the position */
guint        value_area_get_width  (const EvalPosition *ep, Value const *v);
guint        value_area_get_height (const EvalPosition *ep, Value const *v);

/* Return Value(int 0) if non-existant */
const Value *value_area_fetch_x_y  (const EvalPosition *ep, Value const * v,
				    guint x, guint y);

/* Return NULL if non-existant */
const Value * value_area_get_x_y (const EvalPosition *ep, Value const * v,
				  guint x, guint y);

typedef  Value * (*value_area_foreach_callback)(EvalPosition const *ep,
						Value const *v, void *user_data);

Value * value_area_foreach (EvalPosition const *ep, Value const *v,
			    value_area_foreach_callback callback,
			    void *closure);

void value_array_set       (Value *array, guint col, guint row, Value *v);
void value_array_resize    (Value *v, guint width, guint height);
void value_array_copy_to   (Value *dest, const Value *src);

Value * value_is_error (char const * const str, int *offset);
StyleHAlignFlags value_get_default_halign (Value const *v, MStyle const *mstyle);

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
