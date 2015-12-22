#ifndef GNM_MS_OBJ_H
#define GNM_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/

#include "ms-excel-read.h"
#include "ms-excel-write.h"

#define MS_ANCHOR_SIZE	18

enum {
	MSOT_GROUP00		= 0x00,  /* ??? */
	MSOT_LINE		= 0x01,
	MSOT_RECTANGLE		= 0x02,
	MSOT_OVAL		= 0x03,
	MSOT_ARC		= 0x04,
	MSOT_CHART		= 0x05,
	MSOT_TEXTBOX		= 0x06,
	MSOT_BUTTON		= 0x07,
	MSOT_PICTURE		= 0x08,
	MSOT_POLYGON		= 0x09,
	MSOT_CHECKBOX		= 0x0b,
	MSOT_OPTION		= 0x0c,
	MSOT_EDIT		= 0x0d,
	MSOT_LABEL		= 0x0e,
	MSOT_DIALOG		= 0x0f,
	MSOT_SPINNER		= 0x10,
	MSOT_SCROLLBAR		= 0x11,
	MSOT_LIST		= 0x12,
	MSOT_GROUP		= 0x13,
	MSOT_COMBO		= 0x14,
	MSOT_COMMENT		= 0x19,
	MSOT_DRAWING		= 0x1e,
	MSOT_TOGGLE             = 0x70
};


typedef enum {
	MS_OBJ_ATTR_NONE = 0,

    /* Flags */
	MS_OBJ_ATTR_FLIP_H,
	MS_OBJ_ATTR_FLIP_V,
	MS_OBJ_ATTR_UNFILLED,
	MS_OBJ_ATTR_OUTLINE_HIDE,	/* true of style == 0 hides a line */

    /* Integers & Enums */
	MS_OBJ_ATTR_IS_INT_MASK = 0x1000,
	MS_OBJ_ATTR_BLIP_ID,
	MS_OBJ_ATTR_FILL_TYPE,
	MS_OBJ_ATTR_FILL_SHADE_TYPE,
	MS_OBJ_ATTR_FILL_ANGLE,
	MS_OBJ_ATTR_FILL_FOCUS,
	MS_OBJ_ATTR_FILL_COLOR,
	MS_OBJ_ATTR_FILL_ALPHA,
	MS_OBJ_ATTR_FILL_PRESET,
	MS_OBJ_ATTR_FILL_BACKGROUND,
	MS_OBJ_ATTR_FILL_BACKGROUND_ALPHA,
	MS_OBJ_ATTR_OUTLINE_COLOR,
	MS_OBJ_ATTR_OUTLINE_WIDTH,
	MS_OBJ_ATTR_OUTLINE_STYLE,
	MS_OBJ_ATTR_SCROLLBAR_VALUE,
	MS_OBJ_ATTR_SCROLLBAR_MIN,
	MS_OBJ_ATTR_SCROLLBAR_MAX,
	MS_OBJ_ATTR_SCROLLBAR_INC,
	MS_OBJ_ATTR_SCROLLBAR_PAGE,
	MS_OBJ_ATTR_SCROLLBAR_HORIZ,
	MS_OBJ_ATTR_BLIP_CROP_TOP,
	MS_OBJ_ATTR_BLIP_CROP_BOTTOM,
	MS_OBJ_ATTR_BLIP_CROP_LEFT,
	MS_OBJ_ATTR_BLIP_CROP_RIGHT,
	MS_OBJ_ATTR_ARROW_START,
	MS_OBJ_ATTR_ARROW_START_WIDTH,
	MS_OBJ_ATTR_ARROW_START_LENGTH,
	MS_OBJ_ATTR_ARROW_END,
	MS_OBJ_ATTR_ARROW_END_WIDTH,
	MS_OBJ_ATTR_ARROW_END_LENGTH,

    /* Ptrs */
	MS_OBJ_ATTR_IS_PTR_MASK = 0x2000,
	MS_OBJ_ATTR_ANCHOR,
	MS_OBJ_ATTR_TEXT,	/* just the text, no markup */
	MS_OBJ_ATTR_OBJ_NAME,
	MS_OBJ_ATTR_OBJ_ALT_TEXT,

    /* GArrays */
	MS_OBJ_ATTR_IS_GARRAY_MASK = 0x4000,
	MS_OBJ_ATTR_POLYGON_COORDS,

    /* PangoAttrList */
	MS_OBJ_ATTR_IS_PANGO_ATTR_LIST_MASK = 0x10000,
	MS_OBJ_ATTR_MARKUP,

    /* Expressions */
	MS_OBJ_ATTR_IS_EXPR_MASK = 0x20000,
	MS_OBJ_ATTR_LINKED_TO_CELL,
	MS_OBJ_ATTR_INPUT_FROM,
	MS_OBJ_ATTR_MACRO_EXPR,

    /* GObjects */
	MS_OBJ_ATTR_IS_GOBJECT_MASK = 0x40000,
	MS_OBJ_ATTR_IMDATA,

	MS_OBJ_ATTR_MASK = 0x77000
} MSObjAttrID;

typedef struct {
	MSObjAttrID const id;
	union {
		gboolean  v_boolean;
		guint32	  v_uint;
		guint32	  v_int;
		gpointer  v_ptr;
		GArray   *v_array;
		GnmExprTop const *v_texpr;
		PangoAttrList *v_markup;
		GObject  *v_object;
	} v;
} MSObjAttr;

MSObjAttr    *ms_obj_attr_new_flag  (MSObjAttrID id);
MSObjAttr    *ms_obj_attr_new_uint  (MSObjAttrID id, guint32 val);
MSObjAttr    *ms_obj_attr_new_int   (MSObjAttrID id, gint32 val);
MSObjAttr    *ms_obj_attr_new_ptr   (MSObjAttrID id, gpointer val);
MSObjAttr    *ms_obj_attr_new_array (MSObjAttrID id, GArray *array);
MSObjAttr    *ms_obj_attr_new_expr  (MSObjAttrID id, GnmExprTop const *texpr);
MSObjAttr    *ms_obj_attr_new_markup (MSObjAttrID id, PangoAttrList *list);
MSObjAttr    *ms_obj_attr_new_gobject (MSObjAttrID id, GObject *object);

typedef GHashTable MSObjAttrBag;
MSObjAttrBag  *ms_obj_attr_bag_new     (void);
void           ms_obj_attr_bag_destroy (MSObjAttrBag *ab);
void	       ms_obj_attr_bag_insert  (MSObjAttrBag *ab,
				       MSObjAttr *attr);
MSObjAttr     *ms_obj_attr_bag_lookup  (MSObjAttrBag *ab,
				       MSObjAttrID id);
guint32	       ms_obj_attr_get_uint    (MSObjAttrBag *ab, MSObjAttrID id,
				       guint32 default_value);
gint32	       ms_obj_attr_get_int     (MSObjAttrBag *ab, MSObjAttrID id,
				       gint32 default_value);
gboolean       ms_obj_attr_get_ptr     (MSObjAttrBag *ab, MSObjAttrID id,
				       gpointer *res, gboolean steal);
GArray	      *ms_obj_attr_get_array  (MSObjAttrBag *ab, MSObjAttrID id,
				       GArray *default_value, gboolean steal);
GnmExprTop const *ms_obj_attr_get_expr (MSObjAttrBag *ab, MSObjAttrID id,
					GnmExprTop const *default_value,
					gboolean steal);
PangoAttrList *ms_obj_attr_get_markup (MSObjAttrBag *ab, MSObjAttrID id,
				       PangoAttrList *default_value, gboolean steal);
GObject      *ms_obj_attr_get_gobject (MSObjAttrBag *attrs, MSObjAttrID id);


struct _MSObj {
	int id;

	/* Type specific parameters */
	SheetObject	*gnum_obj;
	int		 excel_type;
	char const	*excel_type_name;

	/* a kludge for now until the indicator and the box have distinct objects */
	GnmCellPos	 comment_pos;
	gboolean	 auto_combo;  /* Filter, Validation, ... */
	gboolean	 is_linked;
	MSObjAttrBag	*attrs;
};
MSObj *ms_obj_new    (MSObjAttrBag *ab);
gboolean ms_read_OBJ   (BiffQuery *q, MSContainer *c, MSObjAttrBag *ab);
void   ms_obj_delete (MSObj *obj);
char  *ms_read_TXO   (BiffQuery *q, MSContainer *c, PangoAttrList **markup);

/********************************************************/

void ms_objv8_write_common	(BiffPut *bp, int id, int type, guint16 flags);
void ms_objv8_write_scrollbar_old (BiffPut *bp);
void ms_objv8_write_listbox	(BiffPut *bp, guint8 lct, gboolean filtered);

void ms_objv8_write_note	(BiffPut *bp);

void ms_objv8_write_checkbox (BiffPut *bp,
			      gboolean active,
			      ExcelWriteSheet *esheet,
			      GnmExprTop const *link_texpr,
			      GnmNamedExpr *macro_nexpr);
void ms_objv8_write_radiobutton (BiffPut *bp,
				 gboolean active,
				 ExcelWriteSheet *esheet,
				 GnmExprTop const *link_texpr,
				 GnmNamedExpr *macro_nexpr);
void ms_objv8_write_spinbutton (BiffPut *bp,
				ExcelWriteSheet *esheet,
				GtkAdjustment *adj, gboolean horiz,
				GnmExprTop const *link_texpr,
				GnmNamedExpr *macro_nexpr);
void ms_objv8_write_scrollbar (BiffPut *bp,
			       ExcelWriteSheet *esheet,
			       GtkAdjustment *adj, gboolean horiz,
			       GnmExprTop const *link_texpr,
			       GnmNamedExpr *macro_nexpr);
void ms_objv8_write_list (BiffPut *bp,
			  ExcelWriteSheet *esheet,
			  GtkAdjustment *adj,
			  GnmExprTop const *res_texpr,
			  GnmExprTop const *data_texpr,
			  GnmNamedExpr *macro_nexpr);
void ms_objv8_write_button (BiffPut *bp,
			    ExcelWriteSheet *esheet,
			    GnmNamedExpr *macro_nexpr);


#endif /* GNM_MS_OBJ_H */
