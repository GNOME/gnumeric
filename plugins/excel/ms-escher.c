/**
 * ms-escher.c: MS Office drawing layer support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * See S59FD6.HTM for an overview...
 **/

#include "ms-escher.h"
#include "escher-types.h"
#include "biff-types.h"
#include "ms-excel-read.h"
#include "ms-obj.h"

#include <stdio.h>

/* FIXME : We should not use the MS_OLE_GET_ routines.  All numbers are stored in intel
 *         format.
 */

/* A storage accumulator for common state information */
typedef struct
{
	ExcelWorkbook	*wb;
	BiffQuery	*q;

	int	depth;
} MSEscherState;

typedef struct
{
	/* Read from the data stream */
	guint	ver;
	guint	instance;
	guint16	fbt;
	guint32	len; /* Including the common header */

	/* Initialized via ms_escher_header_init */
	gint32 data_len;
	guint8 const *	data;
	gboolean	needs_deletion;
	int		SpContainer_count;
} MSEscherCommonHeader;
#define common_header_len 8

static gboolean
ms_escher_next_record (MSEscherState * state,
		       MSEscherCommonHeader *h);

static void
ms_escher_header_init (MSEscherCommonHeader * h, 
		       MSEscherCommonHeader const * container, 
		       gint offset)
{
	h->data_len = container->len - offset;
	h->data = container->data + offset;
	h->len = 0;
	h->needs_deletion = FALSE;
	h->SpContainer_count = 0;
}

static void
ms_escher_read_container (MSEscherState * state,
			  MSEscherCommonHeader *containing_header,
			  gint offset)
{
	MSEscherCommonHeader h;
	ms_escher_header_init (&h, containing_header,
			       common_header_len + offset);
	while (ms_escher_next_record (state, &h))
		;
}

/****************************************************************************/

static void
ms_escher_read_CLSID (MSEscherState * state,
		      MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ColorMRU (MSEscherState * state,
			 MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_SplitMenuColors (MSEscherState * state,
				MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_BStoreContainer (MSEscherState * state,
				MSEscherCommonHeader * h)
{
	ms_escher_read_container (state, h, 0);
}

static gchar const *
bliptype_name (int const type)
{
	switch (type) {
	case 2:	 return "emf.gz";
	case 3:	 return "wmf.gz";
	case 4:	 return "pict.gz";
	case 5:	 return "jpg";
	case 6:	 return "png";
	case 7:	 return "dib";
	default: return "Unknown";
	}
}

static void
write_file (gchar const * const name, guint8 const * data,
	    gint len, int stored_type)
{
	static int num = 0;
	char const * suffix = bliptype_name (stored_type);
	GString *file_name = g_string_new (name);
	FILE *f;

	g_string_sprintfa (file_name, "-%d.%s", num++, suffix);

	f = fopen (file_name->str, "w");
	if (f) {
		fwrite (data, len, 1, f);
		fclose (f);
		printf ("written 0x%x bytes to '%s';\n",
			len, file_name->str);
	} else
		printf ("Can't open '%s';\n",
			file_name->str);
	g_string_free (file_name, 1);
}

static void
ms_escher_read_BSE (MSEscherState * state,
		    MSEscherCommonHeader * h)
{
	guint8 const *data = h->data + common_header_len;
	guint8 const win_type	= MS_OLE_GET_GUINT8 (data + 0);
	guint8 const mac_type	= MS_OLE_GET_GUINT8 (data + 1);
	guint32 const size	= MS_OLE_GET_GUINT32(data + 20);
	guint32 const ref_count	= MS_OLE_GET_GUINT32(data + 24);
	gint32 const del_offset	= MS_OLE_GET_GUINT32(data + 28);
	guint8 const is_texture	= MS_OLE_GET_GUINT8 (data + 32);
	guint8 const name_len	= MS_OLE_GET_GUINT8 (data + 33);
	guint8 checksum[16]; /* RSA Data Security, Inc. MD4 Message-Digest Algorithm */
	char *name = "unknown";
	int i;
	for (i = 16; i-- > 0;)
		checksum[i] = MS_OLE_GET_GUINT8 (data + 2 + i);

	printf ("Win type = %s;\n", bliptype_name (win_type));
	printf ("Mac type = %s;\n", bliptype_name (mac_type));
	printf ("Size = 0x%x(=%d) RefCount = 0x%x DelayOffset = 0x%x '%s';\n",
		size, size, ref_count, del_offset, name);

	switch (is_texture) {
	case 0: printf ("Default usage;\n"); break;
	case 1: printf ("Is texture;\n"); break;
	default:printf ("UNKNOWN USAGE : %d;\n", is_texture);
	};

	/* Very red herring I think */
	if (name_len != 0) {
		puts ("WARNING : Maybe a name ?");
		/* name = biff_get_text (data+36, name_len, &txt_byte_len); */
	}

	printf ("Checksum = 0x");
	for (i = 0; i < 16; ++i)
		printf ("%02x", checksum[i]);
	printf (";\n");

	ms_escher_read_container (state, h, 36);
}

static void
ms_escher_read_Blip (MSEscherState * state,
		     MSEscherCommonHeader * h)
{
	int primary_uid_size = 0;
	guint32 blip_instance = h->instance;

	/*  This doesn't make alot of sense.
	 *  Which is the normative indicator of what type the blip is ?
	 *  We have the requested type for each OS, and the instance code for
	 *  this record.  The magic number for this record seems to be the right
	 *  things to use, but the comments from S59FE3.HTM
	 *
	 *  NOTE!: The primary UID is only saved to disk
	 *     if (blip_instance ^ blip_signature == 1).
	 *
	 * suggest that sometimes the magic numers have built flags.
	 * I'll assume that what they mean to say is that the magic number
	 * in the instance code may have its low bit set to indicate the
	 * presence of a primary uid if the rest of the signature is one
	 * of the known signatures.
	 */
	if (blip_instance & 0x1) {
		primary_uid_size = 16;
		blip_instance &= (~0x1);
	}

	/* Clients may set bit 0x800 */
	blip_instance &= (~0x800);

	switch (blip_instance) {
	case 0x216 : /* compressed WMF, with Metafile header */
		break;
	case 0x3d4 : /* compressed EMF, with Metafile header */
		break;
	case 0x542 : /* compressed PICT, with Metafile header */
		break;
	case 0x6e0 : /* PNG  data, with 1 byte header */
	case 0x46a : /* JPEG data, with 1 byte header */
	case 0x7a8 : /* DIB  data, with 1 byte header */
	{
		int const header = 17 + primary_uid_size + common_header_len;
		write_file ("unknown", h->data + header, h->len - header, h->fbt - Blip_START);
		break;
	}

	default:
		g_warning ("Don't know what to do with this image %x\n", h->instance);
	};
}

static void
ms_escher_read_RegroupItems (MSEscherState * state,
			     MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ColorScheme (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_SpContainer (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
	ms_escher_read_container (state, containing_header, 0);
}
static void
ms_escher_read_Spgr (MSEscherState * state,
		     MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_Sp (MSEscherState * state,
		   MSEscherCommonHeader * h)
{
	guint8 const *data = h->data + common_header_len;
	guint32 const spid  = MS_OLE_GET_GUINT32 (data+0);
	guint32 const flags = MS_OLE_GET_GUINT32 (data+4);
	printf ("SPID %d, Type %d,%s%s%s%s%s%s%s%s%s%s%s;\n", spid, h->instance,
		(flags&0x01) ? " Group": "",	(flags&0x02) ? " Child": "",
		(flags&0x04) ? " Patriarch": "",(flags&0x08) ? " Deleted": "",
		(flags&0x10) ? " OleShape": "",	(flags&0x20) ? " HaveMaster": "",
		(flags&0x40) ? " FlipH": "",	(flags&0x80) ? " FlipV": "",
		(flags&0x100) ? " Connector":"",(flags&0x200) ? " HasAnchor": "",
		(flags&0x400) ? " TypeProp": ""
	       );
}

static void
ms_escher_read_Textbox (MSEscherState * state,
			MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_Anchor (MSEscherState * state,
		       MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ChildAnchor (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ClientAnchor (MSEscherState * state,
			     MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ClientData (MSEscherState * state,
			   MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_OleObject (MSEscherState * state,
			  MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_DeletedPspl (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_SolverContainer (MSEscherState * state,
				MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ConnectorRule (MSEscherState * state,
			      MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_AlignRule (MSEscherState * state,
			  MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ArcRule (MSEscherState * state,
			MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_ClientRule (MSEscherState * state,
			   MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_CalloutRule (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_Selection (MSEscherState * state,
			  MSEscherCommonHeader * containing_header)
{
}
static void
ms_escher_read_Dg (MSEscherState * state,
		   MSEscherCommonHeader * h)
{
#if 0
	guint32 num_shapes = MS_OLE_GET_GUINT32(h->data + common_header_len);

	/* spid_cur = last SPID given to an SP in this DG :-)  */
	guint32 spid_cur   = MS_OLE_GET_GUINT32(h->data + common_header_len+4);
	guint32 drawing_id = h->instance;

	/* This drawing has these num_shapes shapes, with a pointer to the last
	 * SPID given to it */
#endif
}

static void
ms_escher_read_Dgg (MSEscherState * state,
		    MSEscherCommonHeader * h)
{
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

	FDGG fd;
	guint32 lp;
	guint8 const *data = h->data + common_header_len;
	fd.id_clusts = g_array_new (1, 1, sizeof(ID_CLUST));
	fd.max_spid           = MS_OLE_GET_GUINT32(data+ 0);
	fd.num_id_clust       = MS_OLE_GET_GUINT32(data+ 4);
	fd.num_shapes_saved   = MS_OLE_GET_GUINT32(data+ 8);
	fd.num_drawings_saved = MS_OLE_GET_GUINT32(data+12);

	printf ("maxspid 0x%x clusts 0x%x shapes 0x%x drawings x%x\n",
		fd.max_spid, fd.num_id_clust, fd.num_shapes_saved,
		fd.num_drawings_saved);

	data+=16;
	for (lp=0;lp<fd.num_id_clust;lp++) {
		ID_CLUST cl;
		cl.DG_owning_spids   = MS_OLE_GET_GUINT32(data+0);
		cl.spids_used_so_far = MS_OLE_GET_GUINT32(data+4);
		g_array_append_val (fd.id_clusts, cl);
	}
}

static void
ms_escher_read_OPT (MSEscherState * state,
		    MSEscherCommonHeader * containing_header)
{
#if 0
typedef struct {
	guint   pid :14;
	guint   bid:1;
	guint   complex:1;
	guint32 op; /* or value */
	guint16 num_properties;
} OPT_DATA;

	guint8 const *data = h->data + common_header_len;
	OPT_DATA *od = g_new (OPT_DATA, 1);
	guint16 d = MS_OLE_GET_GUINT16(data);
	od->pid = d & 0x3fff;
	od->bid = (d & 0x4000)!=0;
	od->complex = (d &0x8000)!=0;
	od->op = MS_OLE_GET_GUINT32(data+2);
	od->num_properties = h->instance;
#endif
}
static void
ms_escher_read_SpgrContainer (MSEscherState * state,
			     MSEscherCommonHeader * containing_header)
{
	ms_escher_read_container (state, containing_header, 0);
}
static void
ms_escher_read_DgContainer (MSEscherState * state,
			    MSEscherCommonHeader * containing_header)
{
	ms_escher_read_container (state, containing_header, 0);
}
static void
ms_escher_read_DggContainer (MSEscherState * state,
			     MSEscherCommonHeader * containing_header)
{
	ms_escher_read_container (state, containing_header, 0);
}
static void
ms_escher_read_ClientTextbox (MSEscherState * state,
			      MSEscherCommonHeader * containing_header)
{
	g_return_if_fail (state->depth == 1);

	if (containing_header->data_len == containing_header->len) {
		guint16 opcode;

		/* Read the TXO, be VERY careful until we are sure of the state */
		g_return_if_fail (ms_biff_query_peek_next (state->q, &opcode));
		g_return_if_fail (opcode == BIFF_TXO);
		g_return_if_fail (ms_biff_query_next (state->q));
		ms_obj_read_text_impl (state->q, state->wb);
	} else {
		g_warning ("EXCEL : expected ClientTextBox to be last element (%d != %d)",
			   containing_header->data_len, containing_header->len);
	}
}

/****************************************************************************/

/*
 * Increment the state by the size of the record refered to by
 * the header, then read the next header
 */
static gboolean
ms_escher_next_record (MSEscherState * state,
		       MSEscherCommonHeader *h)
{
	guint16 tmp;
	char const * fbt_name = NULL;
	void (*handler)(MSEscherState * state,
			MSEscherCommonHeader * containing_header) = NULL;

	/* Increment to the next header */
	h->data_len -= h->len;
	h->data += h->len;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1)
		printf ("h->data_len == %d;\n", h->data_len);
#endif
	if (h->data_len == 0)
		return FALSE;

	g_return_val_if_fail (h->data_len >= common_header_len, FALSE);

	tmp	= MS_OLE_GET_GUINT16(h->data+0);
	h->fbt	= MS_OLE_GET_GUINT16(h->data+2);
	h->len	= MS_OLE_GET_GUINT32(h->data+4);
	h->ver      = tmp & 0x0f;
	h->instance = (tmp>>4) & 0xfff;

	/*
	 * If this is a container (ver == 0xf) the length already includes the header
	 * However, the docs are incomplete and these 3 fbt types also need adjustment
	 * FIXME FIXME FIXME : It seems like only the 1st SpContainer at depth > 1
	 * includes the common header in its length.  Is this cruft really necessary ?
	 */
	if (h->ver != 0xf ||
	    h->fbt == DggContainer ||
	    h->fbt == BStoreContainer ||
	    (h->fbt == SpContainer && ++(h->SpContainer_count) == 1 && state->depth > 1))
		h->len += common_header_len;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
		printf ("length 0x%x(=%d), ver 0x%x, instance 0x%x, Block size = 0x%x(=%d);\n",
			h->len, h->len, h->ver, h->instance, h->data_len, h->data_len);
	}
#endif
	/*
	 * Lets double check that the data we just read makes sense.
	 * If problems arise in the next tests it probably indicates that
	 * the PRECEDING record length was invalid.  Check that it included the header */
	if ((h->fbt & (~0x1ff)) != 0xf000) {
		printf ("WARNING EXCEL : Invalid fbt = %x\n", h->fbt);
		return FALSE;
	}

#define EshRecord(x) \
	x : fbt_name = #x; \
	    handler = &ms_escher_read_ ## x; \
	break

	switch (h->fbt) {
	case EshRecord(DggContainer);	case EshRecord(Dgg);
	case EshRecord(DgContainer);	case EshRecord(Dg);
	case EshRecord(SpgrContainer);	case EshRecord(Spgr);
	case EshRecord(SpContainer);	case EshRecord(Sp);
	case EshRecord(BStoreContainer);case EshRecord(BSE);
	case EshRecord(Textbox);	case EshRecord(ClientTextbox);
	case EshRecord(Anchor);	case EshRecord(ChildAnchor); case EshRecord(ClientAnchor);
	case EshRecord(ClientData);
	case EshRecord(CLSID);
	case EshRecord(OPT);
	case EshRecord(ColorMRU);
	case EshRecord(SplitMenuColors);
	case EshRecord(RegroupItems);
	case EshRecord(ColorScheme);
	case EshRecord(OleObject);
	case EshRecord(DeletedPspl);
	case EshRecord(SolverContainer);
	case EshRecord(ConnectorRule);
	case EshRecord(AlignRule);
	case EshRecord(ArcRule);
	case EshRecord(ClientRule);
	case EshRecord(CalloutRule);
	case EshRecord(Selection);
	default : fbt_name = NULL;
	};
#undef EshRecord

	/* Handle continuation records here */

	/* Even more error checking */
	if (h->len > h->data_len) {
		BiffQuery *nq = ms_biff_query_copy (state->q);
		int cnt = 0, len = h->data_len;
		do {
			/* FIXME : what is the logic here ?
			 * It seems evident that the stated internal size of the
			 * drawing records can be larger than the containing biff
			 * record.  However,  if we add the biff records following
			 * the initial record things usually don't add up.  The
			 * drawing record size seems to only count the draw or
			 * continue records.  When we only add those the sizes match
			 * perfectly.
			 *
			 * BUT
			 *
			 * That makes no sense at all.  There are frequently embedded
			 * TXO records.  Those records are documented to always have 2
			 * CONTINUE records.  The stategy that works adds the sizes of
			 * the CONTINUE records associated with the TXO as if they were
			 * part of the drawing, but the TXO was not ????
			 *
			 * TODO : when we figure the above question out we can get rid
			 * of the biff_to_flat routine and do a more on demand style of
			 * merging.
			 */
			ms_biff_query_next(nq);
			if (nq->opcode == BIFF_MS_O_DRAWING ||
			    nq->opcode == BIFF_MS_O_DRAWING_GROUP ||
			    nq->opcode == BIFF_MS_O_DRAWING_SELECTION ||
			    nq->opcode == BIFF_CONTINUE) {
				printf ("Adding a 0x%x of length %d;\n",
					nq->opcode, nq->length);
				len += nq->length;
				++cnt;
			} else
				printf ("Skipping a 0x%x of length %d;\n",
					nq->opcode, nq->length);
		} while (h->len > len);

		printf ("WARNING EXCEL : remaining bytes = %d < claimed length = %d (fbt = %s %x);\n",
			len, h->len, ((fbt_name != NULL) ? fbt_name : ""), h->fbt);
		printf ("WARNING EXCEL : truncating;\n");

		h->len = h->data_len;
	}

	if (Blip_START <= h->fbt && h->fbt <= Blip_END) {
		ms_escher_read_Blip (state, h);
	} else if (fbt_name != NULL) {
		/* Not really needed */
		g_return_val_if_fail (handler != NULL, FALSE);

		printf ("{ /* %s */\n", fbt_name);
		++(state->depth);
		(*handler)(state, h);
		--(state->depth);
		printf ("}; /* %s */\n", fbt_name);
	} else
		printf ("WARNING EXCEL : Invalid fbt = %x\n", h->fbt);

	return TRUE;
}

/**
 *  Builds a flat record by merging CONTINUE records,
 *  Have to do until we move this into ms_ole.c
 *  pass pointers to your length & data variables.
 *  This is dead sluggish.
 **/
static guint8 *
biff_to_flat_data (BiffQuery *q, guint32 *length, gboolean * needs_to_be_free)
{
	BiffQuery *nq = ms_biff_query_copy (q);
	guint8 *ptr, *data;
	int cnt=0;

	*length=0;
	do {
		*length+=nq->length;
		ms_biff_query_next(nq);
		cnt++;
	} while (nq->opcode == BIFF_MS_O_DRAWING ||
		 nq->opcode == BIFF_MS_O_DRAWING_GROUP ||
		 nq->opcode == BIFF_MS_O_DRAWING_SELECTION ||
		 nq->opcode == BIFF_CONTINUE);

	if (!(*needs_to_be_free = (cnt > 1)))
		return q->data;

	ptr = data = g_malloc (*length);
	do {
		memcpy (ptr, q->data, q->length);
		ptr += q->length;
		ms_biff_query_next(q);
	} while (q->opcode == BIFF_MS_O_DRAWING ||
		 q->opcode == BIFF_MS_O_DRAWING_GROUP ||
		 q->opcode == BIFF_CONTINUE);

	return data;
}
void
ms_escher_hack_get_drawing (BiffQuery *q, ExcelWorkbook *wb)
{
	MSEscherState state;
	MSEscherCommonHeader h, fake_container;
	char const *drawing_record_name = "Unknown";
	gboolean needs_to_be_free;

	g_return_if_fail (q != NULL);
	g_return_if_fail (wb != NULL);
	if (q->opcode != BIFF_MS_O_DRAWING)
		drawing_record_name = "Drawing";
	else if (q->opcode != BIFF_MS_O_DRAWING_GROUP)
		drawing_record_name = "Drawing Group";
	else if (q->opcode != BIFF_MS_O_DRAWING_SELECTION)
		drawing_record_name = "Drawing Selection";
	else
		g_warning ("EXCEL : unexpected biff type %x\n", q->opcode);

	/* Only support during debugging for now */
	if (ms_excel_read_debug <= 0)
		return;

	state.wb = wb;
	state.q = q;
	state.depth = 0;

	fake_container.data = biff_to_flat_data (q, &fake_container.len, &needs_to_be_free);
	ms_escher_header_init (&h, &fake_container, 0);

	printf ("{  /* Escher '%s'*/\n", drawing_record_name);
	while (ms_escher_next_record (&state, &h))
		;
	printf ("}; /* Escher '%s'*/\n", drawing_record_name);

	if (needs_to_be_free)
		g_free ((guint8 *)fake_container.data);
}
