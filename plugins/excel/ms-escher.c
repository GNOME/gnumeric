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
	guint    ver:4;
	guint    instance:12;
	guint16  type;   /* fbt */
	guint32  length;
	guint8  *data;
	guint32  length_left;
	gboolean first;
} ESH_HEADER;
#define ESH_HEADER_LEN 8

static ESH_HEADER *
esh_header_new (guint8 *data, gint32 length)
{
	ESH_HEADER *h = g_new (ESH_HEADER,1);
	h->length=0;
	h->type=0;
	h->instance=0;
	h->data=data;
	h->length_left=length;
	h->first = TRUE;
	return h;
}

static int
esh_header_next (ESH_HEADER *h)
{
	guint16 split;
	g_return_val_if_fail(h, 0);
	g_return_val_if_fail(h->data, 0);

	if (h->length_left < h->length + ESH_HEADER_LEN*2)
		return 0;

	if (h->first==TRUE)
		h->first = FALSE;
	else {
		h->data+=h->length+ESH_HEADER_LEN;
		h->length_left-=h->length+ESH_HEADER_LEN;
	}

	h->length   = BIFF_GETLONG(h->data+4);
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

typedef struct {
	guint32 max_spid;
	guint32 num_id_clust;
	guint32 num_shapes_saved; /* Includes deleted shapes if undo saved */
	guint32 num_drawings_saved;
	GArray *id_clusts;
} FDGG;
typedef struct {
	guint32 DG_owning_spids;
	guint32 spids_used_so_far;
} ID_CLUST;

static FDGG *
Dgg_new (ESH_HEADER *h) /* See: S59FDE.HTM */
{
	FDGG *fd = g_new (FDGG, 1);
	guint8 *data;
	guint32 lp;

	data = h->data + ESH_HEADER_LEN;
	fd->id_clusts = g_array_new (1,1,sizeof(ID_CLUST));
	fd->max_spid           = BIFF_GETLONG(data+ 0);
	fd->num_id_clust       = BIFF_GETLONG(data+ 4);
	fd->num_shapes_saved   = BIFF_GETLONG(data+ 8);
	fd->num_drawings_saved = BIFF_GETLONG(data+12);

	printf ("Dgg: maxspid 0x%x clusts 0x%x shapes 0x%x drawings x%x\n",
		fd->max_spid, fd->num_id_clust, fd->num_shapes_saved,
		fd->num_drawings_saved);

	data+=16;
	for (lp=0;lp<fd->num_id_clust;lp++) {
		ID_CLUST cl;
		cl.DG_owning_spids   = BIFF_GETLONG(data+0);
		cl.spids_used_so_far = BIFF_GETLONG(data+4);
		g_array_append_val (fd->id_clusts, cl);
	}
	return fd;
}

static void
Dgg_destroy (FDGG *fd)
{
	if (!fd)
		return;
	g_array_free (fd->id_clusts, FALSE);
	g_free (fd);
}

typedef struct {
	guint8   win_type;
	guint8   mac_type;
	guint8   rbg_uid[16];
	guint32  size;
	guint32  ref_count;
	guint32  delay_off; /* File offset into delay stream */
	guint8   usage;
	guint8   name_len;
	char *name;
} FILE_BLIP_STORE_ENTRY;

static FILE_BLIP_STORE_ENTRY *
BSE_new (ESH_HEADER *h)
{
	FILE_BLIP_STORE_ENTRY *fbse = g_new (FILE_BLIP_STORE_ENTRY, 1);
	guint8 *data = h->data + ESH_HEADER_LEN;
	int lp;

	fbse->win_type   = BIFF_GETBYTE(data+ 0);
	fbse->mac_type   = BIFF_GETBYTE(data+ 1);
	for (lp=0;lp<16;lp++)
		fbse->rbg_uid[lp] = BIFF_GETBYTE(data+2+lp);
	fbse->size       = BIFF_GETLONG(data+20);
	fbse->ref_count  = BIFF_GETLONG(data+24);
	fbse->delay_off  = BIFF_GETLONG(data+28);
	fbse->usage      = BIFF_GETBYTE(data+32);
	fbse->name_len   = BIFF_GETBYTE(data+33);
	if (fbse->name_len)
		fbse->name = biff_get_text (data+36, fbse->name_len, 0);
	else
		fbse->name = g_strdup("NoName");

	printf ("FBSE: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x '%s'\n",
		fbse->win_type, fbse->mac_type, fbse->size, fbse->ref_count,
		fbse->delay_off, fbse->usage, fbse->name);

	dump (data, h->length-ESH_HEADER_LEN);
	return fbse;
}
static void
BSE_destroy (FILE_BLIP_STORE_ENTRY *fbse)
{
	if (!fbse)
		return;
	g_free (fbse->name);
	g_free (fbse);
}

typedef GPtrArray BLIP_STORAGE_CONTAINER;

static BLIP_STORAGE_CONTAINER *
BStoreContainer_new (ESH_HEADER *h) /* See: S59FE3.HTM */
{
	GPtrArray *bsc = g_ptr_array_new();
	ESH_HEADER *c = esh_header_new (h->data+ESH_HEADER_LEN,
					h->length-ESH_HEADER_LEN);
	while (esh_header_next(c)) {
		switch (c->type) {
		case BSE:
			g_ptr_array_add (bsc, BSE_new(c));
			break;
		default:
			printf ("Unknown Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
	return bsc;
}
static void
BStoreContainer_destroy (BLIP_STORAGE_CONTAINER *bsc)
{
	int lp ;
	if (!bsc) return;
	for (lp=0;lp<bsc->len;lp++)
		BSE_destroy (g_ptr_array_index (bsc, lp));
	g_ptr_array_free (bsc, 0);
}

static void
read_DggContainer (ESH_HEADER *h)
{
	ESH_HEADER *c = esh_header_new (h->data+ESH_HEADER_LEN,
					h->length-ESH_HEADER_LEN);
	printf ("Container\n");
	while (esh_header_next(c)) {
		switch (c->type) {
		case Dgg:
			printf ("Dgg:\n");
			Dgg_new (c);
			break;
		case BStoreContainer:
			BStoreContainer_new (c);
			break;
		default:
			printf ("Unknown Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
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
		switch (h->type) {
		case DggContainer:
			read_DggContainer (h);
			break;
		default:
			printf ("Unknown Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				h->type, h->instance, h->ver, h->length);
			break;
		}
	}
	esh_header_destroy (h); 
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

	disseminate_stream (data, len);
	g_assert (q->streamPos==str_pos);
}
