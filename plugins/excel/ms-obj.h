#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
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
	/* In pixels */
	int anchor[4];
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

gboolean ms_parse_object_anchor (int pos[4],
				 Sheet const * sheet, guint8 const * data);

gboolean ms_obj_realize(MSObj * obj,
			ExcelWorkbook  *wb, ExcelSheet * sheet);

MSObj * ms_read_OBJ (BiffQuery *q,
		     ExcelWorkbook * wb, Sheet * sheet);

void ms_read_TXO (BiffQuery *q, ExcelWorkbook * wb);


#endif /* GNUMERIC_MS_OBJ_H */
