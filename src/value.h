#ifndef GNUMERIC_VALUE_H
#define GNUMERIC_VALUE_H

#include <glib.h>
#include "gnumeric.h"
#include "position.h"
#include "numbers.h"

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

typedef struct {
	ValueType const type;
	StyleFormat *fmt;
} ValueAny;
struct _ValueBool {
	ValueType const type;
	StyleFormat *fmt;
	gboolean val;
};
struct _ValueInt {
	ValueType const type;
	StyleFormat *fmt;
	int val;
};
struct _ValueFloat {
	ValueType const type;
	StyleFormat *fmt;
	gnum_float val;
};
struct _ValueErr {
	ValueType const type;
	StyleFormat *fmt;
	String       *mesg;
	/* Currently unused.  Intended to support audit functions */
	EvalPos  src;
};
struct _ValueStr {
	ValueType const type;
	StyleFormat *fmt;
	String *val;
};
struct _ValueRange {
	ValueType const type;
	StyleFormat *fmt;
	RangeRef cell;
};
struct _ValueArray {
	ValueType const type;
	StyleFormat *fmt;
	int x, y;
	Value ***vals;  /* Array [x][y] */
};

/* FIXME */
union _Value {
	ValueType const type;
	ValueAny	v_any;
	ValueBool	v_bool;
	ValueInt	v_int;
	ValueFloat	v_float;
	ValueErr	v_err;
	ValueStr	v_str;
	ValueRange	v_range;
	ValueArray	v_array;
};

#define	VALUE_TYPE(v)			((v)->v_any.type)
#define	VALUE_FMT(v)			((v)->v_any.fmt)
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
Value       *value_new_string           (char const *str);
Value       *value_new_string_nocopy    (char *str);
Value       *value_new_string_str       (String *str);
Value       *value_new_cellrange_unsafe (CellRef const *a, CellRef const *b);
Value       *value_new_cellrange        (CellRef const *a, CellRef const *b,
				         int eval_col, int eval_row);
Value       *value_new_cellrange_r      (Sheet *sheet, Range const *r);
Value       *value_new_array            (guint cols, guint rows);
Value       *value_new_array_empty      (guint cols, guint rows);
Value 	    *value_new_array_non_init   (guint cols, guint rows);
Value 	    *value_new_from_string	(ValueType t, char const *str, StyleFormat *sf);

void         value_release         (Value *v);
void	     value_set_fmt	   (Value *v, StyleFormat const *fmt);
void         value_dump            (Value const *v);
Value       *value_duplicate       (Value const *v);
gnum_float   value_diff		   (Value const *a, Value const *b);
ValueCompare value_compare         (Value const *a, Value const *b,
				    gboolean case_sensitive);
guint        value_hash	           (Value const *v);

gboolean    value_get_as_bool         (Value const *v, gboolean *err);
gboolean    value_get_as_checked_bool (Value const *v);
char       *value_get_as_string       (Value const *v);
char const *value_peek_string         (Value const *v);
int         value_get_as_int          (Value const *v);
gnum_float  value_get_as_float        (Value const *v);
Value	   *value_coerce_to_number    (Value *v, gboolean *valid,
				       EvalPos const *ep);

Value       *value_error_set_pos      (ValueErr *err, EvalPos const *pos);

/* Area functions ( works on VALUE_RANGE or VALUE_ARRAY */
/* The EvalPos provides a Sheet context; this allows
   calculation of relative references. 'x','y' give the position */
typedef Value *(*ValueAreaFunc) (Value const *v, EvalPos const *ep, void *user);
Value 	    *value_area_foreach	   (Value const *v, EvalPos const *ep,
				    ValueAreaFunc func, void *user);
int          value_area_get_width  (Value const *v, EvalPos const *ep);
int          value_area_get_height (Value const *v, EvalPos const *ep);
Value const *value_area_fetch_x_y  (Value const *v, int x, int y,
				    EvalPos const *ep);
Value const *value_area_get_x_y	   (Value const *v, int x, int y,
				    EvalPos const *ep);


extern ValueErr const value_terminate_err;
#define VALUE_TERMINATE ((Value *)&value_terminate_err)

/* A zero integer, not to be freed or changed.  */
extern Value const *value_zero;

void value_array_set       (Value *array, int col, int row, Value *v);
void value_array_resize    (Value *v, int width, int height);

/* Type definitions and function prototypes for criteria functions.
 * This includes the database functions and some mathematical functions
 * like COUNTIF, SUMIF...
 */
gboolean criteria_test_equal            (Value const *x, Value const *y);
gboolean criteria_test_unequal          (Value const *x, Value const *y);
gboolean criteria_test_greater          (Value const *x, Value const *y);
gboolean criteria_test_less             (Value const *x, Value const *y);
gboolean criteria_test_greater_or_equal (Value const *x, Value const *y);
gboolean criteria_test_less_or_equal    (Value const *x, Value const *y);

/* FIXME: this stuff below ought to go elsewhere.  */
typedef struct {
        int    row;
        GSList *conditions;
} database_criteria_t;
typedef gboolean (*criteria_test_fun_t) (Value const *x, Value const *y);
typedef struct {
        criteria_test_fun_t fun;
        Value               *x;
        int                 column;
} func_criteria_t;
void parse_criteria                 (char const *criteria,
				     criteria_test_fun_t *fun,
				     Value **test_value);
GSList *parse_criteria_range        (Sheet *sheet, int b_col, int b_row,
				     int e_col, int e_row,
				     int   *field_ind);
void free_criterias                 (GSList *criterias);
GSList *find_rows_that_match        (Sheet *sheet, int first_col,
				     int first_row, int last_col, int last_row,
				     GSList *criterias, gboolean unique_only);
GSList *parse_database_criteria     (const EvalPos *ep, Value *database, Value *criteria);
int find_column_of_field (const EvalPos *ep, Value *database, Value *field);


/* Some utility constants to make sure we all spell correctly */
extern char const *gnumeric_err_NULL;
extern char const *gnumeric_err_DIV0;
extern char const *gnumeric_err_VALUE;
extern char const *gnumeric_err_REF;
extern char const *gnumeric_err_NAME;
extern char const *gnumeric_err_NUM;
extern char const *gnumeric_err_NA;
extern char const *gnumeric_err_RECALC;

void value_init (void);
void value_shutdown (void);

#endif /* GNUMERIC_VALUE_H */
