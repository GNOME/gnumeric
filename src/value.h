#ifndef _GNM_VALUE_H_
# define _GNM_VALUE_H_

#include <gnumeric.h>
#include <position.h>
#include <numbers.h>
#include <parse-util.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	/* Use magic values to act as a signature
	 * DO NOT CHANGE THESE NUMBERS
	 * As of version 0.57 they are using as keys
	 * in the xml
	 */
	VALUE_EMPTY	= 10,
	VALUE_BOOLEAN	= 20,
	VALUE_FLOAT	= 40,
	VALUE_ERROR	= 50,
	VALUE_STRING	= 60,
	VALUE_CELLRANGE = 70,
	VALUE_ARRAY	= 80
} GnmValueType;

/*
 * This one lives only in old XML files and is understood by
 * value_new_from_string.
 */
#define	VALUE_INTEGER ((GnmValueType)30)


typedef struct {
	GnmValueType const type;
	GOFormat const *fmt;
} GnmValueAny;
struct _GnmValueBool {
	GnmValueType const type;
	GOFormat *fmt;
	gboolean val;
};
struct _GnmValueFloat {
	GnmValueType const type;
	GOFormat *fmt;
	gnm_float val;
};
struct _GnmValueErr {
	GnmValueType const type;
	GOFormat *fmt;
	GOString   *mesg;
};
struct _GnmValueStr {
	GnmValueType const type;
	GOFormat *fmt;
	GOString   *val;
};
struct _GnmValueRange {
	GnmValueType const type;
	GOFormat *fmt;
	GnmRangeRef cell;
};
struct _GnmValueArray {
	GnmValueType const type;
	GOFormat *fmt;
	int x, y;
	GnmValue ***vals;  /* Array [x][y] */
};

union _GnmValue {
	GnmValueAny	v_any;
	GnmValueBool	v_bool;
	GnmValueFloat	v_float;
	GnmValueErr	v_err;
	GnmValueStr	v_str;
	GnmValueRange	v_range;
	GnmValueArray	v_array;
};

#define	VALUE_FMT(v)			((v)->v_any.fmt)
#define VALUE_IS_EMPTY(v)		(((v) == NULL) || ((v)->v_any.type == VALUE_EMPTY))
#define VALUE_IS_EMPTY_OR_ERROR(v)	(VALUE_IS_EMPTY(v) || (v)->v_any.type == VALUE_ERROR)
#define VALUE_IS_STRING(v)		((v)->v_any.type == VALUE_STRING)
#define VALUE_IS_BOOLEAN(v)		((v)->v_any.type == VALUE_BOOLEAN)
#define VALUE_IS_ERROR(v)		((v)->v_any.type == VALUE_ERROR)
#define VALUE_IS_NUMBER(v)		(((v)->v_any.type == VALUE_FLOAT) || \
					 ((v)->v_any.type == VALUE_BOOLEAN))
#define VALUE_IS_FLOAT(v)		((v)->v_any.type == VALUE_FLOAT)
#define VALUE_IS_ARRAY(v)		((v)->v_any.type == VALUE_ARRAY)
#define VALUE_IS_CELLRANGE(v)		((v)->v_any.type == VALUE_CELLRANGE)

typedef enum {
	IS_EQUAL,
	IS_LESS,
	IS_GREATER,
	TYPE_MISMATCH
} GnmValDiff;

GType gnm_value_get_type (void); /* a boxed type */

GnmValue *value_new_empty            (void);
GnmValue *value_new_bool             (gboolean b);
GnmValue *value_new_int              (int i);
GnmValue *value_new_float            (gnm_float f);
GnmValue *value_new_error            (GnmEvalPos const *pos, char const *mesg);
GnmValue *value_new_error_str        (GnmEvalPos const *pos, GOString *mesg);
GnmValue *value_new_error_std        (GnmEvalPos const *pos, GnmStdError err);
GnmValue *value_new_error_NULL       (GnmEvalPos const *pos);
GnmValue *value_new_error_DIV0       (GnmEvalPos const *pos);
GnmValue *value_new_error_VALUE      (GnmEvalPos const *pos);
GnmValue *value_new_error_REF        (GnmEvalPos const *pos);
GnmValue *value_new_error_NAME       (GnmEvalPos const *pos);
GnmValue *value_new_error_NUM        (GnmEvalPos const *pos);
GnmValue *value_new_error_NA         (GnmEvalPos const *pos);
GnmValue *value_new_string           (char const *str);
GnmValue *value_new_string_nocopy    (char *str);
GnmValue *value_new_string_str       (GOString *str);
GnmValue *value_new_cellrange_unsafe (GnmCellRef const *a, GnmCellRef const *b);
GnmValue *value_new_cellrange        (GnmCellRef const *a, GnmCellRef const *b,
				      int eval_col, int eval_row);
GnmValue *value_new_cellrange_r      (Sheet *sheet, GnmRange const *r);
GnmValue *value_new_cellrange_str    (Sheet *sheet, char const *str);
GnmValue *value_new_cellrange_parsepos_str (GnmParsePos const *pp,
					    char const *str,
					    GnmExprParseFlags flags);
GnmValue *value_new_array            (guint cols, guint rows);
GnmValue *value_new_array_empty      (guint cols, guint rows);
GnmValue *value_new_array_non_init   (guint cols, guint rows);
GnmValue *value_new_from_string	     (GnmValueType t, char const *str,
				      GOFormat *sf, gboolean translated);

void        value_release	   (GnmValue *v);
void	    value_set_fmt	   (GnmValue *v, GOFormat const *fmt);
void        value_dump		   (GnmValue const *v);
GnmValue   *value_dup		   (GnmValue const *v);

gnm_float   value_diff		   (GnmValue const *a, GnmValue const *b);
GnmValDiff  value_compare	   (GnmValue const *a, GnmValue const *b,
				    gboolean case_sensitive);
GnmValDiff  value_compare_no_cache (GnmValue const *a, GnmValue const *b,
				    gboolean case_sensitive);
int	    value_cmp		   (void const *ptr_a, void const *ptr_b);
int	    value_cmp_reverse	   (void const *ptr_a, void const *ptr_b);
gboolean    value_equal		   (GnmValue const *a, GnmValue const *b);
guint       value_hash		   (GnmValue const *v);

char const *value_peek_string	   (GnmValue const *v);
char       *value_get_as_string	   (GnmValue const *v);
void        value_get_as_gstring   (GnmValue const *v, GString *target,
				    GnmConventions const *conv);
char       *value_stringify        (GnmValue const *v);

GnmValueType value_type_of         (GnmValue const *v);
int         value_get_as_int	   (GnmValue const *v);
gnm_float   value_get_as_float	   (GnmValue const *v);
gboolean    value_is_zero	   (GnmValue const *v);
GnmValue   *value_coerce_to_number (GnmValue *v, gboolean *valid,
				    GnmEvalPos const *ep);

GnmValue   *value_error_set_pos    (GnmValueErr *err, GnmEvalPos const *pos);
GnmStdError value_error_classify   (GnmValue const *v);
char const *value_error_name       (GnmStdError err, gboolean translated);

gboolean    value_get_as_bool	      (GnmValue const *v, gboolean *err);
gboolean    value_get_as_checked_bool (GnmValue const *v);
GnmRangeRef const *value_get_rangeref (GnmValue const *v);

typedef struct {
	GnmValue const *v;		/* value at position */
	int x, y;			/* coordinates within input region */
	GnmValue const *region;		/* input region */
	GnmEvalPos const *ep;		/* context for region */
	GnmCellIter const *cell_iter;	/* non-NULL for ranges */
} GnmValueIter;
typedef GnmValue *(*GnmValueIterFunc) (GnmValueIter const *iter, gpointer user_data);

/* Area functions ( for VALUE_RANGE or VALUE_ARRAY ) */
GnmValue       *value_area_foreach    (GnmValue const *v, GnmEvalPos const *ep,
				       CellIterFlags flags,
				       GnmValueIterFunc func, gpointer user_data);
int             value_area_get_width  (GnmValue const *v, GnmEvalPos const *ep);
int             value_area_get_height (GnmValue const *v, GnmEvalPos const *ep);
GnmValue const *value_area_fetch_x_y  (GnmValue const *v, int x, int y,
				       GnmEvalPos const *ep);
GnmValue const *value_area_get_x_y    (GnmValue const *v, int x, int y,
				       GnmEvalPos const *ep);

/* A zero integer, not to be freed or changed.  */
extern GnmValue const *value_zero;
extern GnmValueErr const value_terminate_err;
#define VALUE_TERMINATE ((GnmValue *)&value_terminate_err)

void value_array_set       (GnmValue *array, int col, int row, GnmValue *v);


/* Protected */
void value_init     (void);
void value_shutdown (void);

G_END_DECLS

#endif /* _GNM_VALUE_H_ */
