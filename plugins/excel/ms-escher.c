/**
 * ms-escher.c: MS Office drawing layer support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "gnumeric-sheet.h"
#include "format.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula.h"
#include "ms-excel.h"
#include "ms-excel-biff.h"
#include "ms-obj.h"
#include "ms-escher.h"
#include "escher-types.h"

typedef struct { /* See: S59FDA.HTM */
	guint   ver:4;
	guint   instance:12;
	guint16 type;   /* fbt */
	gint32  length; /* Misleading really 16bits */
	guint8 *data;
	gint32  length_left;
} ESH_HEADER;

static ESH_HEADER *
esh_header_new (guint8 *data, gint32 length)
{
	ESH_HEADER *h = g_new (ESH_HEADER,1);
	h->length=-6;
	h->type=0;
	h->instance=0;
	h->data=data;
	h->length_left=length;
	return h;
}

static int
esh_header_next (ESH_HEADER *h)
{
	guint16 split;
	g_return_val_if_fail(h, 0);
	g_return_val_if_fail(h->data, 0);

	h->data+=h->length+6;
	h->length_left-=h->length+6;

	if (h->length_left<=5)
		return 0;
	h->length   = BIFF_GETWORD(h->data+4);
	h->type     = BIFF_GETWORD(h->data+2);
	split       = BIFF_GETWORD(h->data+0);
	h->ver      = (split&0x0f);
	h->instance = (split>>4);
	return 1;
}
static void
esh_header_destroy (ESH_HEADER *h)
{
	if (h)
		g_free(h);
}

/**
 * General points:
 * For docs. on pointer conversions see: S59FDC.HTM
 * BLIP = Big Large Image / Picture see: S59FE3.HTM
 **/

static void
disseminate_stream (guint8 *data, gint32 length)
{
	ESH_HEADER *h =	esh_header_new (data, length);
	while (esh_header_next(h)) {
		printf ("Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
			h->type, h->instance, h->ver, h->length);
	}
	esh_header_destroy (h); 
}

/**
 *  Builds a flat record by merging CONTINUE records,
 *  Have to do until we move this into ms_ole.c
 *  pass pointers to your length & data variables.
 *  This is dead sluggish.
 **/
static void
biff_to_flat_data (const BIFF_QUERY *q, guint8 **data, guint32 *length)
{
	BIFF_QUERY *nq = ms_biff_query_copy (q);
	guint8 *ptr;

	*length=0;
	do {
		*length+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE);

	(*data) = g_malloc (*length);
	ptr=(*data);
	nq = ms_biff_query_copy (q);
	do {
		memcpy (ptr, nq->data, nq->length);
		ptr+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE);
}

/**
 * FIXME: See S59FDA.HTM / S59FDB.HTM
 * essentialy the MS_OLE_STREAM needs to be sub-classed by excel, and
 * forced to store its data inside BIFF records inside the excel stream.
 * For now we'll assume the data is small and doesn't have any CONTINUE
 * records !!!.
 **/
void
ms_escher_hack_get_drawing (const BIFF_QUERY *q)
{
	/* Convert the query to a sort of streeam */
	guint8 *data;
	guint32 len;
	guint32 str_pos=q->streamPos;

	biff_to_flat_data (q, &data, &len);
	printf ("Drawing data\n");
	dump (data, len);

	disseminate_stream (data, len);
	g_assert (q->streamPos==str_pos);
}
