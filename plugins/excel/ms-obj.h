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
	MS_OBJ_ATTR_FILL_COLOR,

    /* Ptrs */
	MS_OBJ_ATTR_NEEDS_FREE_MASK = 0x2000,
	MS_OBJ_ATTR_ANCHOR
} MSObjAttrID;

typedef struct {
	MSObjAttrID const id;
	union {
		gboolean v_boolean;
		guint32	 v_uint;
		gpointer v_ptr;
	} v;
} MSObjAttr;

MSObjAttr    *ms_object_attr_new_flag    (MSObjAttrID id);
MSObjAttr    *ms_object_attr_new_uint    (MSObjAttrID id, guint32 val);
MSObjAttr    *ms_object_attr_new_ptr     (MSObjAttrID id, gpointer val);
void	      ms_object_attr_destroy     (MSObjAttr *attr);

typedef GHashTable MSObjAttrBag;
MSObjAttrBag *ms_object_attr_bag_new     (void);
void          ms_object_attr_bag_destroy (MSObjAttrBag *attrs);
void	      ms_object_attr_bag_insert  (MSObjAttrBag *attrs,
					  MSObjAttr *attr);
MSObjAttr    *ms_object_attr_bag_lookup  (MSObjAttrBag *attrs,
					  MSObjAttrID id);

struct _MSObj
{
	int id;

	/* Type specific parameters */
	GtkObject	*gnum_obj;
	int		 excel_type;
	char const	*excel_type_name;

	GHashTable	*attrs;
};

void  ms_read_OBJ   (BiffQuery *q, MSContainer *container,
		     GHashTable *attrs);
void  ms_obj_delete (MSObj *obj);
char *ms_read_TXO   (BiffQuery *q);

#endif /* GNUMERIC_MS_OBJ_H */
