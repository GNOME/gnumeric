#ifndef GNUMERIC_VALUE_H
#define GNUMERIC_VALUE_H

typedef struct _ErrorMessage	ErrorMessage;
typedef struct _Value		Value;

#include "sheet.h"
#include "numbers.h"
#include "str.h"
#include <glib.h>

typedef enum {
	/* Use magic values to act as a signature */
	VALUE_EMPTY	= 10,
	VALUE_BOOLEAN	= 20,
	VALUE_ERROR	= 30,
	VALUE_STRING	= 40,
	VALUE_INTEGER	= 50,
	VALUE_FLOAT	= 60,
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
		CellRef cell;
		struct {
			CellRef cell_a;
			CellRef cell_b;
		} cell_range;

		struct {
			int x, y ;
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

#define VALUE_IS_PROBLEM(x) ((x == NULL) || \
			    ((x)->type == VALUE_ERROR))

Value       *value_new_empty       (void);
Value       *value_new_bool        (gboolean b);
Value       *value_new_error       (EvalPosition const *pos, char const *mesg);
Value       *value_new_string      (const char *str);
Value       *value_new_int         (int i);
Value       *value_new_float       (float_t f);
Value       *value_new_cellrange   (const CellRef *a, const CellRef *b);
Value       *value_new_array       (guint width, guint height);

void         value_release         (Value *value);
void         value_dump            (Value const *value);
Value       *value_duplicate       (Value const *value);
void         value_copy_to         (Value *dest, Value const *source);
Value       *value_cast_to_float   (Value *v);

gboolean     value_get_as_bool     (Value const *v, gboolean *err);
char        *value_get_as_string   (const Value *value);
int          value_get_as_int      (const Value *v);
float_t      value_get_as_float    (const Value *v);
char        *value_cellrange_get_as_string (const Value *value,
					    gboolean use_relative_syntax);


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

void value_array_set       (Value *array, guint col, guint row, Value *v);
void value_array_resize    (Value *v, guint width, guint height);
void value_array_copy_to   (Value *dest, const Value *src);

/* Some utility constants to make sure we all spell correctly */
extern char const *gnumeric_err_NULL;
extern char const *gnumeric_err_DIV0;
extern char const *gnumeric_err_VALUE;
extern char const *gnumeric_err_REF;
extern char const *gnumeric_err_NAME;
extern char const *gnumeric_err_NUM;
extern char const *gnumeric_err_NA;

#endif /* GNUMERIC_VALUE_H */
