/**
 * ms-escher.c: MS Office drawing layer support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * See S59FD6.HTM for an overview...
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

#include "excel.h"
#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula-read.h"
#include "ms-excel-biff.h"
#include "ms-obj.h"
#include "ms-escher.h"
#include "escher-types.h"

#define ESH_BITMAP_DUMP 0
#define ESH_OPT_DUMP 0
#define ESH_HEADER_DEBUG 3

/**
 * NB. SP = ShaPe
 *     GR = GRoup
 *
 *     sizeof (MSOSPID) = 32 bits.
 **/

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

/* To be passed around as a parameter */
enum { eWindows, eMac } saved_os = eWindows;

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

	h->length   = BIFF_GET_GUINT32(h->data+4);
	h->type     = BIFF_GET_GUINT16(h->data+2);
	split       = BIFF_GET_GUINT16(h->data+0);
	h->ver      = (split&0x0f);
	h->instance = (split>>4);
#if ESH_HEADER_DEBUG > 0
	printf ("Next header length 0x%x(=%d), type 0x%x, ver 0x%x, instance 0x%x\n",
		h->length, h->length, h->type, h->ver, h->instance);
#endif
	return 1;
}

static ESH_HEADER *
esh_header_contained (ESH_HEADER *h)
{
	if (h->length_left<ESH_HEADER_LEN)
		return NULL;
	g_assert (h->data[h->length_left-1] == /* Check that pointer */
		  h->data[h->length_left-1]);
	return esh_header_new (h->data+ESH_HEADER_LEN,
			       h->length-ESH_HEADER_LEN);
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
biff_to_flat_data (const BiffQuery *q, guint8 **data, guint32 *length)
{
	BiffQuery *nq = ms_biff_query_copy (q);
	guint8 *ptr;
	int cnt=0;

	*length=0;
	do {
		*length+=nq->length;
		ms_biff_query_next(nq);
		cnt++;
	} while (nq->opcode == BIFF_CONTINUE ||
		 nq->opcode == BIFF_MS_O_DRAWING ||
		 nq->opcode == BIFF_MS_O_DRAWING_GROUP);

	printf ("MERGING %d continues\n", cnt);
	(*data) = g_malloc (*length);
	ptr=(*data);
	nq = ms_biff_query_copy (q);
	do {
		memcpy (ptr, nq->data, nq->length);
		ptr+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE ||
		 nq->opcode == BIFF_MS_O_DRAWING ||
		 nq->opcode == BIFF_MS_O_DRAWING_GROUP);
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
	fd->max_spid           = BIFF_GET_GUINT32(data+ 0);
	fd->num_id_clust       = BIFF_GET_GUINT32(data+ 4);
	fd->num_shapes_saved   = BIFF_GET_GUINT32(data+ 8);
	fd->num_drawings_saved = BIFF_GET_GUINT32(data+12);

	printf ("Dgg: maxspid 0x%x clusts 0x%x shapes 0x%x drawings x%x\n",
		fd->max_spid, fd->num_id_clust, fd->num_shapes_saved,
		fd->num_drawings_saved);

	data+=16;
	for (lp=0;lp<fd->num_id_clust;lp++) {
		ID_CLUST cl;
		cl.DG_owning_spids   = BIFF_GET_GUINT32(data+0);
		cl.spids_used_so_far = BIFF_GET_GUINT32(data+4);
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

typedef enum  { eERROR = 0, eUNKNOWN = 1, eEMF = 2, eWMF = 3,
		ePICT = 4, eJPEG = 5, ePNG = 6, eDIB = 7 } blip_type;

typedef struct {

	blip_type stored_type;
	guint8    rbg_uid[16]; /* md4 sum of the data ... */
	guint32   size;
	guint32   ref_count;
	guint32   delay_off; /* File offset into delay stream */
	enum { eUsageDefault, eUsageTexture } usage;
	guint8    name_len;
	char     *name;
} FILE_BLIP_STORE_ENTRY;

static char *
bse_type_to_name (blip_type type)
{
	switch (type) {
	case eWMF:
		return "Compressed WMF";
	case eEMF:
		return "Compressed EMF";
	case ePICT:
		return "Compressed PICT";
	case ePNG:
		return "PNG";
	case eJPEG:
		return "JPEG";
	case eDIB:
		return "DIB";
	default:
		return "Unknown";
	}
	return "Unknown";
}

static void
write_file (char *name, guint8 *data, guint32 len, blip_type type)
{
	FILE *f;
	GString *nme = g_string_new (name);
	static int num=0;
	g_string_sprintfa (nme, "-%d", num++);
	switch (type) {
	case eJPEG:
		g_string_append (nme, ".jpg");
		break;
	case ePNG:
		g_string_append (nme, ".png");
		break;
	case eDIB:
		g_string_append (nme, ".bmp");
		break;
	default:
		g_string_append (nme, ".duff");
		break;
	}
	f = fopen (nme->str, "w");
	if (f) {
		fwrite (data, len, 1, f);
		fclose (f);
		printf ("written 0x%x bytes to '%s'\n", len, nme->str);
	} else
		printf ("Can't open '%s'\n", nme->str);
	g_string_free (nme, 1);
}

static FILE_BLIP_STORE_ENTRY *
BSE_new (ESH_HEADER *h) /* S59FE3.HTM */
{
	FILE_BLIP_STORE_ENTRY *fbse = g_new (FILE_BLIP_STORE_ENTRY, 1);
	guint8 *data = h->data + ESH_HEADER_LEN;
	guint8 type;
	guint32 tmp,txt_byte_len,data_len;
	int lp;

	if (saved_os == eMac)
		type   = BIFF_GET_GUINT8(data+ 1);
	else
		type   = BIFF_GET_GUINT8(data+ 0);
	if (type<8)
		fbse->stored_type = h->instance;
	else
		fbse->stored_type = eERROR;
	printf ("Stored type : 0x%x type 0x%x\n", h->instance, type);

	for (lp=0;lp<16;lp++)
		fbse->rbg_uid[lp] = BIFF_GET_GUINT8(data+2+lp);
	fbse->size       = BIFF_GET_GUINT32(data+20);
	fbse->ref_count  = BIFF_GET_GUINT32(data+24);
	fbse->delay_off  = BIFF_GET_GUINT32(data+28);
	tmp              = BIFF_GET_GUINT8(data+32);
	if (tmp==1)		
		fbse->usage = eUsageTexture;
	else
		fbse->usage = eUsageDefault;
	fbse->name_len   = BIFF_GET_GUINT8(data+33);
	if (fbse->name_len)
		fbse->name = biff_get_text (data+36, fbse->name_len,
					    &txt_byte_len);
	else {
		fbse->name = g_strdup("NoName");
		txt_byte_len=0;
	}

	printf ("FBSE: '%s' 0x%x(=%d) 0x%x 0x%x 0x%x '%s'\n",
		bse_type_to_name (fbse->stored_type), fbse->size, fbse->size,
		fbse->ref_count,
		fbse->delay_off, fbse->usage, fbse->name);
	for (lp=0;lp<16;lp++)
		printf ("0x%x ", fbse->rbg_uid[lp]);
	printf ("\n");

	/* Now the picture data */
	data+=txt_byte_len+36;
	data_len = h->length - ESH_HEADER_LEN -36 - txt_byte_len;

#if ESH_BITMAP_DUMP > 0
	printf ("Header\n");
	dump (h->data, h->length-data_len);

	printf ("Data\n");
	dump (data, data_len);
#endif
	if (fbse->stored_type == eJPEG ||
	    fbse->stored_type == ePNG ||
	    fbse->stored_type == eDIB) {
		data+=25; /* Another header ! */
		data_len-=25;
		write_file ("test", data, fbse->size, fbse->stored_type);
	} else 
		printf ("FIXME: unhandled type 0x%x\n",
			fbse->stored_type);

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

typedef struct {
	guint   pid :14;
	guint   bid:1;
	guint   complex:1;
	guint32 op; /* or value */
	guint16 num_properties;
} OPT_DATA;

static OPT_DATA *
OPT_new (ESH_HEADER *h) /*See: S59FFB.HTM */
{
	guint8 *data=h->data+ESH_HEADER_LEN;
	OPT_DATA *od = g_new (OPT_DATA, 1);
	guint16 d = BIFF_GET_GUINT16(data);
	od->pid = d & 0x3fff;
	od->bid = (d & 0x4000)!=0;
	od->complex = (d &0x8000)!=0;
	od->op = BIFF_GET_GUINT32(data+2);
	od->num_properties = h->instance;
	printf ("OPT: 0x%x %d %d 0x%x, %d props.\n", od->pid,
		od->bid, od->complex, od->op, od->num_properties);
#if ESH_OPT_DUMP > 0
	dump (h->data, h->length);
#endif
	return od;
}

static BLIP_STORAGE_CONTAINER *
BStoreContainer_new (ESH_HEADER *h) /* See: S59FE3.HTM */
{
	GPtrArray *bsc = g_ptr_array_new();
	ESH_HEADER *c = esh_header_contained (h);
	while (esh_header_next(c)) {
		switch (c->type) {
		case BSE:
			g_ptr_array_add (bsc, BSE_new(c));
			break;
		default:
			printf ("Unknown BSC Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
	if (!bsc || bsc->len < h->ver)
		printf ("Too few BLIP entries, are %d should be %d\n",
			bsc->len, h->ver);
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
	ESH_HEADER *c = esh_header_contained (h);
	printf ("Group Container\n");
	while (esh_header_next(c)) {
		switch (c->type) {
		case Dgg:
			printf ("Dgg:\n");
			Dgg_new (c);
			break;
		case OPT:
			OPT_new (c);
			break;
		case BStoreContainer:
			BStoreContainer_new (c);
			break;
		default:
			printf ("Unknown DGGC Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
}

/**
 *  A Shape ... it contains details about it self generaly,
 * only one real shape in inside the container though.
 **/
static void
SpContainer_new (ESH_HEADER *h)  /* See: S59FEB.HTM */
{
	ESH_HEADER *c = esh_header_contained (h);
	while (esh_header_next (c)) {
		switch (c->type) {
		case Sp: /* See S59A001.HTM for Real Geometry Data... */
		{
			guint8 *data = c->data + ESH_HEADER_LEN;
			guint32 spid  = BIFF_GET_GUINT32 (data+0);
			guint32 flags = BIFF_GET_GUINT32 (data+4);
			enum  { Group=1, Child=2, Patriarch=4, Deleted=8, OleShape=16,
				HaveMaster=32, FlipH=64, FlipV=128, Connector=256,
				HasAnchor=512, TypeProp=1024 };
			printf ("Sp: SPID %d, Type %d group? %d, Child? %d, Patriarch? %d, Deleted? %d, OleShape? %d\n",
				spid, c->instance, ((flags&Group)!=0), ((flags&Child)!=0), ((flags&Patriarch)!=0),
				((flags&Deleted)!=0), ((flags&OleShape)!=0));
			break;
		case SpgrContainer:
			printf ("SpgrContainer...\n");
			break;
		}
		default:
			printf ("Unknown shape container thing : type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
	esh_header_destroy(c);
}

static void
SpgrContainer_new (ESH_HEADER *h)  /* See: S59FEA.HTM */
{
	ESH_HEADER *c = esh_header_contained (h);
	while (esh_header_next (c)) {
		switch (c->type) {
		case Dg: /* FIXME: duplicated, whats up here ? See S59FE8.HTM */
		{
			guint32 num_shapes = BIFF_GET_GUINT32(c->data+ESH_HEADER_LEN);
			/* spid_cur = last SPID given to an SP in this DG :-)  */
			guint32 spid_cur   = BIFF_GET_GUINT32(c->data+ESH_HEADER_LEN+4);
			break;
		}
		case SpContainer:
		{
			SpContainer_new (c);
			break;
		}
		case SpgrContainer: /* We contain ourselfs */
		{
			SpgrContainer_new (c);
			break;
		}
		default:
			printf ("Unknown shape group contained : type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				c->type, c->instance, c->ver, c->length);
			break;
		}
	}
	esh_header_destroy(c);
}

static void
read_DgContainer (ESH_HEADER *h) /* See S59FE7.HTM */
{
	ESH_HEADER *c = esh_header_contained (h);
	printf ("Container\n");
	while (esh_header_next(c)) {
		switch (c->type) {
		case Dg: /* See S59FE8.HTM */
		{
			guint32 num_shapes = BIFF_GET_GUINT32(c->data+ESH_HEADER_LEN);
			/* spid_cur = last SPID given to an SP in this DG :-)  */
			guint32 spid_cur   = BIFF_GET_GUINT32(c->data+ESH_HEADER_LEN+4);
			guint32 drawing_id = c->instance;
			/* This drawing has these num_shapes shapes, with a pointer to the last SPID given to it */
			break;
		}
		case SpgrContainer: /* See: S59FEA.HTM */
			SpgrContainer_new (h);
			break;
		default:
			printf ("Unknown contained : type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
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
		case DgContainer:
			read_DgContainer (h);
			break;
		default:
			printf ("Unknown Dissstr Header: type 0x%x, inst 0x%x ver 0x%x len 0x%x\n",
				h->type, h->instance, h->ver, h->length);
			break;
		}
	}
	esh_header_destroy (h); 
}

/**
 * FIXME: See S59FDA.HTM / S59FDB.HTM
 * essentialy the MsOleStream needs to be sub-classed by excel, and
 * forced to store its data inside BIFF records inside the excel stream.
 * For now we'll assume the data is small and doesn't have any CONTINUE
 * records !!!.
 **/
void
ms_escher_hack_get_drawing (const BiffQuery *q)
{
	/* Convert the query to a sort of streeam */
	guint8 *data;
	guint32 len;
	printf ("------ Start Escher -------\n");

	biff_to_flat_data (q, &data, &len);

	disseminate_stream (data, len);

	printf ("------ End Escher -------\n");

}
