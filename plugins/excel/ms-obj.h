#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks, Jody Goldberg
 **/

#include "ms-excel-read.h"

#define MS_ANCHOR_SIZE	18

typedef enum {
	MS_OBJ_ATTR_NONE = 0,

    /* Flags */
	MS_OBJ_ATTR_FLIP_H,
	MS_OBJ_ATTR_FLIP_V,
	MS_OBJ_ATTR_FILLED,

	/* will be enums when we support multiple arrow shapes */
	MS_OBJ_ATTR_ARROW_START,
	MS_OBJ_ATTR_ARROW_END,

    /* Integers & Enums */
	MS_OBJ_ATTR_IS_INT_MASK = 0x1000,
	MS_OBJ_ATTR_BLIP_ID,
	MS_OBJ_ATTR_FILL_TYPE,
	MS_OBJ_ATTR_FILL_COLOR,
	MS_OBJ_ATTR_FILL_ALPHA,
	MS_OBJ_ATTR_FILL_BACKGROUND,
	MS_OBJ_ATTR_FILL_BACKGROUND_ALPHA,
	MS_OBJ_ATTR_OUTLINE_COLOR,
	MS_OBJ_ATTR_SCROLLBAR_VALUE,
	MS_OBJ_ATTR_SCROLLBAR_MIN,
	MS_OBJ_ATTR_SCROLLBAR_MAX,
	MS_OBJ_ATTR_SCROLLBAR_INC,
	MS_OBJ_ATTR_SCROLLBAR_PAGE,
	MS_OBJ_ATTR_BLIP_CROP_TOP,
	MS_OBJ_ATTR_BLIP_CROP_BOTTOM,
	MS_OBJ_ATTR_BLIP_CROP_LEFT,
	MS_OBJ_ATTR_BLIP_CROP_RIGHT,

    /* Ptrs */
	MS_OBJ_ATTR_IS_PTR_MASK = 0x2000,
	MS_OBJ_ATTR_ANCHOR,
	MS_OBJ_ATTR_TEXT, /* the text including PangoMarkup tags */

    /* GArrays */
	MS_OBJ_ATTR_IS_GARRAY_MASK = 0x4000,
	MS_OBJ_ATTR_POLYGON_COORDS,

    /* Expressions */
	MS_OBJ_ATTR_IS_EXPR_MASK = 0x10000,
	MS_OBJ_ATTR_CHECKBOX_LINK,
	MS_OBJ_ATTR_SCROLLBAR_LINK,

    /* PangoAttrList */
	MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK = 0x20000,
	MS_OBJ_ATTR_MARKUP,

	MS_OBJ_ATTR_MASK = 0x37000
} MSObjAttrID;

typedef struct {
	MSObjAttrID const id;
	union {
		gboolean  v_boolean;
		guint32	  v_uint;
		guint32	  v_int;
		gpointer  v_ptr;
		GArray   *v_array;
		GnmExpr const *v_expr;
		PangoAttrList *v_markup;
	} v;
} MSObjAttr;

MSObjAttr    *ms_obj_attr_new_flag  (MSObjAttrID id);
MSObjAttr    *ms_obj_attr_new_uint  (MSObjAttrID id, guint32 val);
MSObjAttr    *ms_obj_attr_new_int   (MSObjAttrID id, gint32 val);
MSObjAttr    *ms_obj_attr_new_ptr   (MSObjAttrID id, gpointer val);
MSObjAttr    *ms_obj_attr_new_array (MSObjAttrID id, GArray *array);
MSObjAttr    *ms_obj_attr_new_expr  (MSObjAttrID id, GnmExpr const *expr);
MSObjAttr    *ms_obj_attr_new_markup (MSObjAttrID id, PangoAttrList *list);

guint32   ms_obj_attr_get_uint	    (MSObj *obj, MSObjAttrID id, guint32 default_value);
gint32    ms_obj_attr_get_int	    (MSObj *obj, MSObjAttrID id, gint32 default_value);
gpointer  ms_obj_attr_get_ptr	    (MSObj *obj, MSObjAttrID id, gpointer default_value);
GArray   *ms_obj_attr_get_array	    (MSObj *obj, MSObjAttrID id, GArray *default_value);
GnmExpr const *ms_obj_attr_get_expr (MSObj *obj, MSObjAttrID id, GnmExpr const *default_value);
PangoAttrList *ms_obj_attr_get_markup	(MSObj *obj, MSObjAttrID id, PangoAttrList *default_value);

typedef GHashTable MSObjAttrBag;
MSObjAttrBag *ms_obj_attr_bag_new     (void);
void          ms_obj_attr_bag_destroy (MSObjAttrBag *attrs);
void	      ms_obj_attr_bag_insert  (MSObjAttrBag *attrs,
				       MSObjAttr *attr);
MSObjAttr    *ms_obj_attr_bag_lookup  (MSObjAttrBag *attrs,
				       MSObjAttrID id);
MSObj        *ms_obj_new              (MSObjAttrBag *attrs);

struct _MSObj {
	int id;

	/* Type specific parameters */
	SheetObject	*gnum_obj;
	int		 excel_type;
	char const	*excel_type_name;

	/* a kludge for now until the indicator and the box have distinct objects */
	GnmCellPos	 comment_pos;
	gboolean	 combo_in_autofilter;
	gboolean	 is_linked;
	MSObjAttrBag	*attrs;
};

void  ms_read_OBJ   (BiffQuery *q, MSContainer *c, MSObjAttrBag *attrs);
void  ms_obj_delete (MSObj *obj);
char *ms_read_TXO   (BiffQuery *q);

/********************************************************/

void ms_objv8_write_common	(BiffPut *bp, int id, int type, gboolean sys);
void ms_objv8_write_scrollbar	(BiffPut *bp);
void ms_objv8_write_listbox	(BiffPut *bp, gboolean filtered);
void ms_objv8_write_chart	(BiffPut *bp, SheetObject *sog);

#endif /* GNUMERIC_MS_OBJ_H */
