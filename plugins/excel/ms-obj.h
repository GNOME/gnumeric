#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1998, 1999 Michael Meeks, Jody Goldberg
 **/

#include "config.h"
#ifdef ENABLE_BONOBO
#	include <bonobo/gnome-stream.h>
#	include <bonobo/gnome-stream-memory.h>
#	include "sheet-object-container.h"
#else
#	include "sheet-object.h"
#endif

#include "ms-excel-read.h"

typedef struct
{
	int pos;		/* Cell or row number */
	int nths;		/* No of 1/1024th, 1/256th */
} anchor_point;

typedef struct
{
	/* In pixels */
	anchor_point anchor[4];
	gboolean anchor_set;

	int id;

	/* Type specific parameters */
	SheetObjectType	gnumeric_type;
	unsigned	excel_type;
	union {
		struct {
			int blip_id;
		} picture;
	} v;
} MSObj;

gboolean ms_parse_object_anchor (anchor_point pos[4],
				 Sheet  const * sheet,
				 guint8 const * data);

gboolean ms_obj_realize(MSObj * obj,
			ExcelWorkbook  *wb, ExcelSheet * sheet);

void     ms_excel_sheet_realize_objs (ExcelSheet *sheet);
void     ms_excel_sheet_destroy_objs (ExcelSheet *sheet);

MSObj   *ms_read_OBJ (BiffQuery *q,
		      ExcelWorkbook * wb, Sheet * sheet);

char    *ms_read_TXO (BiffQuery *q, ExcelWorkbook * wb);

#endif /* GNUMERIC_MS_OBJ_H */
