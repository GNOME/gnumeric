#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks, Jody Goldberg
 **/

#include "config.h"
#include "ms-excel-read.h"

#define MS_ANCHOR_SIZE	18

struct _MSObj
{
	/* In pixels */
	guint8	raw_anchor [MS_ANCHOR_SIZE];
	gboolean anchor_set;

	int id;

	/* Type specific parameters */
	GtkObject	*gnum_obj;
	unsigned	 excel_type;
	char const	*excel_type_name;
};

MSObj *ms_read_OBJ    (BiffQuery *q, MSContainer *container);
void   ms_destroy_OBJ (MSObj *obj);
char  *ms_read_TXO    (BiffQuery *q);

#endif /* GNUMERIC_MS_OBJ_H */
