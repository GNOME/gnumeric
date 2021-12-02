/*
 * ms-escher.c: MS Office drawing layer support
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <stdio.h>

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>
#include "boot.h"
#include "ms-escher.h"
#include "escher-types.h"
#include "biff-types.h"
#include "ms-excel-read.h"
#include "ms-obj.h"

#include <gsf/gsf-utils.h>
#include <glib/gstdio.h>
#include <zlib.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:escher"

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_escher_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

GOLineDashType
ms_escher_xl_to_line_type (guint16 pattern)
{
	static GOLineDashType const dash_map []= {
		GO_LINE_SOLID,
		GO_LINE_DASH,
		GO_LINE_DOT,
		GO_LINE_DASH_DOT,
		GO_LINE_DASH_DOT_DOT,
		GO_LINE_DOT, /* ? */
		GO_LINE_DASH,
		GO_LINE_DASH, /* Long dash */
		GO_LINE_DASH_DOT,
		GO_LINE_DASH_DOT, /* Long dash dot */
		GO_LINE_DASH_DOT_DOT  /* Long dash dot dot */
	};

	return (pattern >= G_N_ELEMENTS (dash_map))
		? GO_LINE_SOLID
		: dash_map[pattern];
}

int
ms_escher_line_type_to_xl (GOLineDashType ld)
{
	switch (ld) {
	default:
	case GO_LINE_NONE:		return -1;
	case GO_LINE_SOLID:		return 0;
	case GO_LINE_S_DOT:		return 2;
	case GO_LINE_S_DASH_DOT:	return 3;
	case GO_LINE_S_DASH_DOT_DOT:	return 4;
	case GO_LINE_DASH_DOT_DOT_DOT:	return 4;
	case GO_LINE_DOT:		return 5;
	case GO_LINE_S_DASH:		return 6;
	case GO_LINE_DASH:		return 7;
	case GO_LINE_LONG_DASH:		return 8;
	case GO_LINE_DASH_DOT:		return 9;
	case GO_LINE_DASH_DOT_DOT:	return 10;
	}
}


/* A storage accumulator for common state information */
typedef struct {
	MSContainer	*container;
	BiffQuery	*q;

	guint32	segment_len;	/* number of bytes in current segment */

	/* Offsets from the logical 1st byte in the stream */
	gint32 start_offset;	/* 1st byte in current segment */
	gint32 end_offset;	/* 1st byte past end of current segment */
} MSEscherState;

typedef struct _MSEscherHeader {
	/* Read from the data stream */
	guint	ver;
	guint	instance;
	guint16	fbt;
	gint32	len; /* Including the common header */

	gint32	offset;
	struct _MSEscherHeader *container;

	MSObjAttrBag *attrs;
	gboolean      release_attrs;
} MSEscherHeader;

#define COMMON_HEADER_LEN	 8
#define META_FILE_HEADER_LEN	34

/* NOTE : this does not init h->container or h->offset */
static void
ms_escher_header_init (MSEscherHeader *h)
{
	h->ver = h->instance = h->fbt = h->len = 0;
	h->attrs = NULL;
	h->release_attrs = TRUE;
}

static void
ms_escher_header_release (MSEscherHeader *h)
{
	if (h->attrs != NULL) {
		if (h->release_attrs)
			ms_obj_attr_bag_destroy (h->attrs);
		h->attrs = NULL;
	}
}

static void
ms_escher_header_add_attr (MSEscherHeader *h, MSObjAttr *attr)
{
	if (h->attrs == NULL)
		h->attrs = ms_obj_attr_bag_new ();
	ms_obj_attr_bag_insert (h->attrs, attr);
}

static MSEscherBlip *
ms_escher_blip_new (guint8 *data, guint32 len, char const *type, gboolean copy)
{
	MSEscherBlip *blip = g_new (MSEscherBlip, 1);

	blip->type = type;
	blip->data_len = len;
	blip->needs_free = TRUE;
	blip->data = copy ? go_memdup (data, len) : data;

	return blip;
}

void
ms_escher_blip_free (MSEscherBlip *blip)
{
	blip->type = NULL;
	if (blip->needs_free) {
		g_free (blip->data);
		blip->needs_free = FALSE;
	}
	blip->data = NULL;
	g_free (blip);
}

/*
 * Get the requested number of bytes from the data stream data pointer, merge
 * and increment biff records if we need too.
 *
 * It seems evident that the stated internal size of the drawing records can be
 * larger than the containing biff record.  However,  if we add the biff
 * records following the initial record things usually don't add up.  The
 * drawing record size seems to only count the draw records.  When we only add
 * those the sizes match perfectly.
 */
static guint8 const *
ms_escher_get_data (MSEscherState *state,
		    gint offset,	/* bytes from logical start of the stream */
		    gint num_bytes,	/*how many bytes we want, NOT incl prefix */
		    gboolean * needs_free)
{
	BiffQuery *q = state->q;
	guint8    *res;

	g_return_val_if_fail (offset >= state->start_offset, NULL);

	/* find the 1st containing record */
	while (offset >= state->end_offset) {
		if (!ms_biff_query_next (q)) {
			g_warning ("unexpected end of stream;");
			return NULL;
		}

		if (q->opcode != BIFF_MS_O_DRAWING &&
		    q->opcode != BIFF_MS_O_DRAWING_GROUP &&
		    q->opcode != BIFF_MS_O_DRAWING_SELECTION &&
		    q->opcode != BIFF_CHART_gelframe &&
		    q->opcode != BIFF_CONTINUE) {
		  g_warning ("Unexpected record type 0x%x len=0x%x @ 0x%lx;", q->opcode, q->length, (long)q->streamPos);
			return NULL;
		}

		d (1, g_printerr ("Target is 0x%x bytes at 0x%x, current = 0x%x..0x%x;\n"
			      "Adding biff-0x%x of length 0x%x;\n",
			      num_bytes, offset,
			      state->start_offset,
			      state->end_offset,
			      q->opcode, q->length););

		state->start_offset = state->end_offset;
		state->end_offset += q->length;
		state->segment_len = q->length;
	}

	g_return_val_if_fail (offset >= state->start_offset, NULL);
	g_return_val_if_fail ((size_t)(offset - state->start_offset) < q->length, NULL);

	res = q->data + offset - state->start_offset;
	if ((*needs_free = ((offset + num_bytes) > state->end_offset))) {
		guint8 *buffer = g_malloc (num_bytes);
		guint8 *tmp = buffer;

		/* Setup front stub */
		int len = q->length - (res - q->data);
		int counter = 0;

		d (1, g_printerr ("MERGE needed (%d) which is >= -%d + %d;\n",
			      num_bytes, offset, state->end_offset););

		do {
			int maxlen = (buffer + num_bytes) - tmp;
			len = MIN (len, maxlen);
			d (1, g_printerr ("record %d) add %d bytes;\n", ++counter, len););

			/* copy necessary portion of current record */
			memcpy (tmp, res, len);
			tmp += len;

			/* Get next record */
			if (!ms_biff_query_next (q)) {
				g_warning ("unexpected end of stream;");
				g_free (buffer);
				return NULL;
			}

			/* We should only see DRAW records now */
			if (q->opcode != BIFF_MS_O_DRAWING &&
			    q->opcode != BIFF_MS_O_DRAWING_GROUP &&
			    q->opcode != BIFF_MS_O_DRAWING_SELECTION &&
			    q->opcode != BIFF_CHART_gelframe &&
			    q->opcode != BIFF_CONTINUE) {
				g_warning ("Unexpected record type 0x%x @ 0x%lx;", q->opcode, (long)q->streamPos);
				g_free (buffer);
				return NULL;
			}

			state->start_offset = state->end_offset;
			state->end_offset += q->length;
			state->segment_len = q->length;

			res = q->data;
			len = q->length;

		} while ((num_bytes - (tmp - buffer)) > len);

		/* Copy back stub */
		memcpy (tmp, res, num_bytes - (tmp-buffer));
		d (1, g_printerr ("record %d) add %d bytes;\n",
			      ++counter,
			      num_bytes - (int)(tmp-buffer)););
		return buffer;
	}

	return res;
}

static gboolean
ms_escher_read_container (MSEscherState *state, MSEscherHeader *container,
			  gint prefix, gboolean return_attrs_in_container);

/****************************************************************************/

static gboolean
ms_escher_read_CLSID (MSEscherState *state, MSEscherHeader *h)
{
	/* Holds a 'Class ID Record' ID record which is only included in the
	 * 'clipboard format'.  It contains an OLE CLSID record from the source
	 * app, and can be used check where the clipboard data originated.
	 *
	 * We ignore these.  What is an 'OLE CLSID' ?  Would we ever need this ?
	 */
	return FALSE;
}

static gboolean
ms_escher_read_ColorMRU (MSEscherState *state, MSEscherHeader *h)
{
	d (3 , {
		guint const num_Colours = h->instance;
		g_printerr ("There are %d Colours in a record with remaining length %d;\n",
			num_Colours, (h->len - COMMON_HEADER_LEN));
	});

	/* Colors in order from left to right.  */
	/* TODO : When we know how to parse a Colour record read these */
	return FALSE;
}

static gboolean
ms_escher_read_SplitMenuColors (MSEscherState *state, MSEscherHeader *h)
{
	gboolean needs_free;
	guint8 const * data;

	g_return_val_if_fail (h->instance == 4, TRUE);
	g_return_val_if_fail (h->len == 24, TRUE); /*header + 4*4 */

	if ((data = ms_escher_get_data (state, h->offset + COMMON_HEADER_LEN,
					16, &needs_free))) {
		guint32 const top_level_fill = GSF_LE_GET_GUINT32(data + 0);
		guint32 const line	= GSF_LE_GET_GUINT32(data + 4);
		guint32 const shadow	= GSF_LE_GET_GUINT32(data + 8);
		guint32 const threeD	= GSF_LE_GET_GUINT32(data + 12);

		d (0, g_printerr ("top_level_fill = 0x%x;\nline = 0x%x;\nshadow = 0x%x;\nthreeD = 0x%x;\n",
			      top_level_fill, line, shadow, threeD););
	} else
		return TRUE;
	return FALSE;
}

static gboolean
ms_escher_read_BStoreContainer (MSEscherState *state, MSEscherHeader *h)
{
	return ms_escher_read_container (state, h, 0, FALSE);
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

static gboolean
ms_escher_read_BSE (MSEscherState *state, MSEscherHeader *h)
{
	/* read the header */
	gboolean needs_free;
	guint8 const * data = ms_escher_get_data (state,
		h->offset + COMMON_HEADER_LEN, 36, &needs_free);
	if (data != NULL) {
		guint8 const  win_type	= GSF_LE_GET_GUINT8  (data + 0);
		guint8 const  mac_type	= GSF_LE_GET_GUINT8  (data + 1);
		/*guint16 const tag	= GSF_LE_GET_GUINT16 (data + 18);*/
		guint32 const size	= GSF_LE_GET_GUINT32 (data + 20);
		guint32 const ref_count	= GSF_LE_GET_GUINT32 (data + 24);
		gint32 const del_offset	= GSF_LE_GET_GUINT32 (data + 28);
		guint8 const is_texture	= GSF_LE_GET_GUINT8  (data + 32);
		guint8 const name_len	= GSF_LE_GET_GUINT8  (data + 33);
		guint8 checksum[16]; /* RSA Data Security, Inc. MD4 Message-Digest Algorithm */
		char const *name = "unknown";
		int i;
		for (i = 16; i-- > 0;)
			checksum[i] = GSF_LE_GET_GUINT8 (data + 2 + i);

		d (0 , {
			g_printerr ("Win type = %s;\n", bliptype_name (win_type));
			g_printerr ("Mac type = %s;\n", bliptype_name (mac_type));
			g_printerr ("Size = 0x%x(=%d) RefCount = 0x%x DelayOffset = 0x%x '%s';\n",
				size, size, ref_count, del_offset, name);

			switch (is_texture) {
			case 0: g_printerr ("Default usage;\n"); break;
			case 1: g_printerr ("Is texture;\n"); break;
			default:g_printerr ("UNKNOWN USAGE : %d;\n", is_texture);
			}

			g_printerr ("Checksum = 0x");
			for (i = 0; i < 16; ++i)
				g_printerr ("%02x", checksum[i]);
			g_printerr (";\n");
		});

		/* Very red herring I think */
		if (name_len != 0) {
			g_printerr ("WARNING : Maybe a name?\n");
			/* name = biff_get_text (data+36, name_len, &txt_byte_len); */
		}

		/* Ignore empties */
		if (h->len > 36 + COMMON_HEADER_LEN)
			return ms_escher_read_container (state, h, 36, FALSE);
	} else
		return TRUE;

	/* Store a blank */
	ms_container_add_blip (state->container, NULL);
	return FALSE;
}

static gboolean
ms_escher_read_Blip (MSEscherState *state, MSEscherHeader *h)
{
	int offset = COMMON_HEADER_LEN + 16;
	guint32 inst = h->instance;
	gboolean needs_free, failure = FALSE;
	guint8 const *data;
	MSEscherBlip *blip = NULL;
	char const   *type = NULL;

	/*  This doesn't make alot of sense.
	 *  Which is the normative indicator of what type the blip is ?
	 *  We have the requested type for each OS, and the instance code for
	 *  this record.  The magic number for this record seems to be the right
	 *  things to use, but the comments from S59FE3.HTM
	 *
	 *  NOTE!: The primary UID is only saved to disk
	 *     if (inst ^ blip_signature == 1).
	 *
	 * suggest that sometimes the magic numers have built flags.
	 * I'll assume that what they mean to say is that the magic number
	 * in the instance code may have its low bit set to indicate the
	 * presence of a primary uid if the rest of the signature is one
	 * of the known signatures.
	 */
	if (inst & 0x1) {
		offset += 16;
		inst &= (~0x1);
	}

	/* Clients may set bit 0x800 */
	inst &= (~0x800);
	if (inst == 0x216 || inst == 0x3d4 || inst == 0x542) {
		guint8 const *tmp = ms_escher_get_data (state, h->offset + offset,
			META_FILE_HEADER_LEN, &needs_free);
		if (tmp != NULL) {
			guint32 uncompressed_len = GSF_LE_GET_GUINT32 (tmp+0);
			guint32 compressed_len = GSF_LE_GET_GUINT32 (tmp+28);
			guint8  compress = tmp[32];
			guint8  filter	 = tmp[33];

			if (needs_free)
				g_free ((guint8*)tmp);
			offset += META_FILE_HEADER_LEN;

			if (inst == 0x216)
				type = "wmf";
			else if (inst == 0x3d4)
				type = "emf";
			else
				type = "pict";

			if (filter != 0xfe /* must be none */	||
			    (unsigned)(h->len - offset) < compressed_len) {
				failure = TRUE;
				g_warning ("invalid metafile header %x, %u != %u;", filter,
					   (h->len - offset), compressed_len);
			} else if ((tmp = ms_escher_get_data (state, h->offset + offset,
			                                      compressed_len, &needs_free))) {
				if (compress == 0) {		/* Yes 0 == deflate */
					uLongf len = uncompressed_len*4;
					guint8 *buffer = g_malloc (len);

					int res = uncompress (buffer, &len, tmp, compressed_len);
					if (res != Z_OK) {
						g_free (buffer);
						failure = TRUE;
						g_warning ("compression failure %d;", res);
					} else
						blip = ms_escher_blip_new (buffer,
							len, type, FALSE);
				} else if (compress == 0xfe) {	/* 0xfe  == none */
					blip = ms_escher_blip_new ((guint8 *)tmp, compressed_len,
						type, !needs_free);
					needs_free = FALSE;
				} else {
					failure = TRUE;
					g_warning ("Unknown compression type %hhx;", compress);
				}
				if (needs_free)
					g_free ((guint8*)tmp);
			}
		} else
			failure = TRUE;
	} else if (inst == 0x46a || inst == 0x6e0 || inst == 0x7a8) {
		if (inst == 0x46a)	/* JPEG data, with 1 byte header */
			type = "jpeg";
		else if (inst == 0x6e0)	/* PNG  data, with 1 byte header */
			type = "png";
		else			/* DIB  data, with 1 byte header */
			type = "dib";
		offset++;
		if (h->len < offset) {
			data = NULL;
			g_warning ("Invalid Blip in escher stream");
		} else
			data = ms_escher_get_data (state, h->offset + offset,
						   h->len - offset,
						   &needs_free);
		if (data)
			blip = ms_escher_blip_new ((guint8 *)data,
						   h->len - offset,
						   type, !needs_free);
		else
			failure = TRUE;
	} else {
		failure = TRUE;
		g_warning ("Don't know what to do with this image %x;", inst);
	}

	/* Always add a blip to keep the indicies in sync, even if it's NULL */
	ms_container_add_blip (state->container, blip);
	return failure;
}

static gboolean
ms_escher_read_RegroupItems (MSEscherState *state, MSEscherHeader *h)
{
	/* Each shape in a drawing has a regroup ID (separate from the shape
	 * ID), so that the regroup command can find shapes that were once
	 * grouped. In order to handle nested grouping and ungrouping
	 * there is a table logging changes to regroup IDs. Each entry has an
	 * old ID and a new ID and records the change of all instances of the
	 * old ID to the new ID.
	 *
	 * The instance of an msofbtRegroupItems record contains the number of
	 * entries, and the record itself is just that many FRITs (File Regroup
	 * Items).
	 *
	 * typedef struct { FRID fridNew; FRID fridOld; } FRIT;
	 */

	return FALSE;
}

static gboolean
ms_escher_read_ColorScheme (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}

static gboolean
ms_escher_read_SpContainer (MSEscherState *state, MSEscherHeader *h)
{
	return ms_escher_read_container (state, h, 0, FALSE);
}

static gboolean
ms_escher_read_Sp (MSEscherState *state, MSEscherHeader *h)
{
	static char const * const shape_names[] = {
		/* 0 */ "Not a primitive",
		/* 1 */ "Rectangle",
		/* 2 */ "RoundRectangle",
		/* 3 */ "Ellipse",
		/* 4 */ "Diamond",
		/* 5 */ "IsocelesTriangle",
		/* 6 */ "RightTriangle",
		/* 7 */ "Parallelogram",
		/* 8 */ "Trapezoid",
		/* 9 */ "Hexagon",
		/* 10 */ "Octagon",
		/* 11 */ "Plus",
		/* 12 */ "Star",
		/* 13 */ "Arrow",
		/* 14 */ "ThickArrow",
		/* 15 */ "HomePlate",
		/* 16 */ "Cube",
		/* 17 */ "Balloon",
		/* 18 */ "Seal",
		/* 19 */ "Arc",
		/* 20 */ "Line",
		/* 21 */ "Plaque",
		/* 22 */ "Can",
		/* 23 */ "Donut",
		/* 24-31 */	"TextSimple", "TextOctagon", "TextHexagon", "TextCurve", "TextWave",
				"TextRing", "TextOnCurve", "TextOnRing",
		/* 32 */ "StraightConnector1",
		/* 33-36 */	"BentConnector2",	"BentConnector3",
				"BentConnector4",	"BentConnector5",
		/* 37-40 */	"CurvedConnector2",	"CurvedConnector3",
				"CurvedConnector4",	"CurvedConnector5",
		/* 41-43 */ "Callout1",		"Callout2",	"Callout3",
		/* 44-46 */ "AccentCallout1", "AccentCallout2", "AccentCallout3",
		/* 47-49 */ "BorderCallout1", "BorderCallout2", "BorderCallout3",
		/* 50-52 */ "AccentBorderCallout1", "AccentBorderCallout2", "AccentBorderCallout3",
		/* 53-54 */ "Ribbon", "Ribbon2",
		/* 55 */ "Chevron",
		/* 56 */ "Pentagon",
		/* 57 */ "NoSmoking",
		/* 58 */ "Seal8",
		/* 59 */ "Seal16",
		/* 60 */ "Seal32",
		/* 61 */ "WedgeRectCallout",
		/* 62 */ "WedgeRRectCallout",
		/* 63 */ "WedgeEllipseCallout",
		/* 64 */ "Wave",
		/* 65 */ "FoldedCorner",
		/* 66 */ "LeftArrow",
		/* 67 */ "DownArrow",
		/* 68 */ "UpArrow",
		/* 69 */ "LeftRightArrow",
		/* 70 */ "UpDownArrow",
		/* 71 */ "IrregularSeal1",
		/* 72 */ "IrregularSeal2",
		/* 73 */ "LightningBolt",
		/* 74 */ "Heart",
		/* 75 */ "PictureFrame",
		/* 76 */ "QuadArrow",
		/* 77-83 */ "LeftArrowCallout", "RightArrowCallout",
			"UpArrowCallout", "DownArrowCallout", "LeftRightArrowCallout",
			"UpDownArrowCallout", "QuadArrowCallout",
		/* 84 */ "Bevel",
		/* 85 */ "LeftBracket",
		/* 86 */ "RightBracket",
		/* 87 */ "LeftBrace",
		/* 88 */ "RightBrace",
		/* 89 */ "LeftUpArrow",
		/* 90 */ "BentUpArrow",
		/* 91 */ "BentArrow",
		/* 92 */ "Seal24",
		/* 93 */ "StripedRightArrow",
		/* 94 */ "NotchedRightArrow",
		/* 95 */ "BlockArc",
		/* 96 */ "SmileyFace",
		/* 97 */ "VerticalScroll",
		/* 98 */ "HorizontalScroll",
		/* 99 */ "CircularArrow",
		/* 100 */ "NotchedCircularArrow",
		/* 101 */ "UturnArrow",
		/* 102-105 */ "CurvedRightArrow", "CurvedLeftArrow", "CurvedUpArrow", "CurvedDownArrow",
		/* 106 */ "CloudCallout",
		/* 107-108 */ "EllipseRibbon", "EllipseRibbon2",
		/* 109-135 */ "FlowChartProcess", "FlowChartDecision",
			"FlowChartInputOutput", "FlowChartPredefinedProcess",
			"FlowChartInternalStorage", "FlowChartDocument",
			"FlowChartMultidocument", "FlowChartTerminator",
			"FlowChartPreparation", "FlowChartManualInput",
			"FlowChartManualOperation", "FlowChartConnector",
			"FlowChartPunchedCard", "FlowChartPunchedTape",
			"FlowChartSummingJunction", "FlowChartOr", "FlowChartCollate",
			"FlowChartSort", "FlowChartExtract", "FlowChartMerge",
			"FlowChartOfflineStorage", "FlowChartOnlineStorage",
			"FlowChartMagneticTape", "FlowChartMagneticDisk",
			"FlowChartMagneticDrum", "FlowChartDisplay", "FlowChartDelay",
		/* 136 */ "TextPlainText",
		/* 137 */ "TextStop",
		/* 138-139 */ "TextTriangle", "TextTriangleInverted",
		/* 140-141 */ "TextChevron", "TextChevronInverted",
		/* 142-143 */ "TextRingInside", "TextRingOutside",
		/* 144-145 */ "TextArchUpCurve", "TextArchDownCurve",
		/* 146-147 */ "TextCircleCurve", "TextButtonCurve",
		/* 148-149 */ "TextArchUpPour", "TextArchDownPour",
		/* 150 */ "TextCirclePour",
		/* 151 */ "TextButtonPour",
		/* 152-153 */ "TextCurveUp",	"TextCurveDown",
		/* 154-155 */ "TextCascadeUp",	"TextCascadeDown",
		/* 156-159 */ "TextWave1",		"TextWave2", "TextWave3", "TextWave4",
		/* 160-161 */ "TextInflate",	"TextDeflate",
		/* 162-163 */ "TextInflateBottom", "TextDeflateBottom",
		/* 164-165 */ "TextInflateTop",	"TextDeflateTop",
		/* 166-167 */ "TextDeflateInflate",	"TextDeflateInflateDeflate",
		/* 168-171 */ "TextFadeRight", "TextFadeLeft", "TextFadeUp", "TextFadeDown",
		/* 172-174 */ "TextSlantUp", "TextSlantDown", "TextCanUp",
		/* 175 */ "TextCanDown",
		/* 176 */ "FlowChartAlternateProcess",
		/* 177 */ "FlowChartOffpageConnector",
		/* 178 */ "Callout90",
		/* 179 */ "AccentCallout90",
		/* 180 */ "BorderCallout90",
		/* 181 */ "AccentBorderCallout90",
		/* 182 */ "LeftRightUpArrow",
		/* 183-184 */ "Sun",	"Moon",
		/* 185 */ "BracketPair",
		/* 186 */ "BracePair",
		/* 187 */ "Seal4",
		/* 188 */ "DoubleWave",
		/* 189-200 */ "ActionButtonBlank", "ActionButtonHome",
			"ActionButtonHelp", "ActionButtonInformation",
			"ActionButtonForwardNext", "ActionButtonBackPrevious",
			"ActionButtonEnd", "ActionButtonBeginning",
			"ActionButtonReturn", "ActionButtonDocument",
			"ActionButtonSound", "ActionButtonMovie",
		/* 201 */ "HostControl",
		/* 202 */ "TextBox"
	};

	gboolean needs_free;
	guint32 spid, flags;
	guint8 const *data;

	g_return_val_if_fail (h->instance <= 202, TRUE);

	d (0, g_printerr ("%s (0x%x);\n", shape_names[h->instance],
		      h->instance););

	data = ms_escher_get_data (state,
		h->offset + COMMON_HEADER_LEN, 8, &needs_free);
	if (data == NULL)
		return TRUE;

	spid  = GSF_LE_GET_GUINT32 (data+0);
	flags = GSF_LE_GET_GUINT32 (data+4);
	d (0, g_printerr ("SPID %d, Type %d,%s%s%s%s%s%s%s%s%s%s%s%s;\n",
		      spid, h->instance,
			(flags&0x01) ? " Group": "",
			(flags&0x02) ? " Child": "",
			(flags&0x04) ? " Patriarch": "",
			(flags&0x08) ? " Deleted": "",
			(flags&0x10) ? " OleShape": "",
			(flags&0x20) ? " HaveMaster": "",
			(flags&0x40) ? " FlipH": "",
			(flags&0x80) ? " FlipV": "",
			(flags&0x100) ? " Connector":"",
			(flags&0x200) ? " HasAnchor": "",
			(flags&0x400) ? " HasBackground": "",
			(flags&0x800) ? " HasSpt": ""
		       ););

	if (flags & 0x40)
		ms_escher_header_add_attr (h,
			ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_H));
	if (flags & 0x80)
		ms_escher_header_add_attr (h,
			ms_obj_attr_new_flag (MS_OBJ_ATTR_FLIP_V));
	if (needs_free)
		g_free ((guint8*)data);

	return FALSE;
}

static gboolean
ms_escher_read_Spgr (MSEscherState *state, MSEscherHeader *h)
{
	gboolean needs_free;
	int len = h->len - COMMON_HEADER_LEN;
	guint8 const *data = ms_escher_get_data (state,
		h->offset + COMMON_HEADER_LEN, len, &needs_free);
	if (data == NULL)
		return TRUE;

	/* Stored as absolute pixels in fixed point for pre-grouped position
	 * What we do not know is where the parent group stores the offset from
	 * original to current */
	d (1, {
		g_print ("SPGR\t");
		gsf_mem_dump (data, len);});

	if (needs_free)
		g_free ((guint8 *)data);
	return FALSE;
}

static gboolean
ms_escher_read_Textbox (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}

static gboolean
ms_escher_read_Anchor (MSEscherState *state, MSEscherHeader *h)
{
	/* only needed for clipboard info, like child anchor with dim 100,000 */
	return FALSE;
}

static gboolean
ms_escher_read_ChildAnchor (MSEscherState *state, MSEscherHeader *h)
{
	gboolean needs_free;
	int len = h->len - COMMON_HEADER_LEN;
	guint8 const *data = ms_escher_get_data (state,
		h->offset + COMMON_HEADER_LEN, len, &needs_free);
	if (data == NULL)
		return TRUE;

	/* Stored as absolute pixels in fixed point for pre-grouped position
	 * What we do not know is where the parent group stores the offset from
	 * original to current */
	d (1, {
		g_print ("ChildAnchor");
		gsf_mem_dump (data, len);});

	if (needs_free)
		g_free ((guint8 *)data);
	return FALSE;
}

/* TODO : This is a guess that explains the workbooks we have, it seems credible because it
 * matches the pre-biff8 format. Find some confirmation.
 * WARNING : this is host specific and only works for Excel
 */
static gboolean
ms_escher_read_ClientAnchor (MSEscherState *state, MSEscherHeader *h)
{
	gboolean needs_free;
	guint8 const *data;

	g_return_val_if_fail (state != NULL, TRUE);
	g_return_val_if_fail (state->container != NULL, TRUE);

	data = ms_escher_get_data (state, h->offset + COMMON_HEADER_LEN,
		MS_ANCHOR_SIZE, &needs_free);
#if 0
	gsf_mem_dump (data, MS_ANCHOR_SIZE);
#endif
	if (data) {
		guint8 *anchor = go_memdup (data, MS_ANCHOR_SIZE);

		ms_escher_header_add_attr (h,
			ms_obj_attr_new_ptr (MS_OBJ_ATTR_ANCHOR, anchor));

		if (needs_free)
			g_free ((guint8 *)data);
		return FALSE;
	}

	return TRUE;
}

static gboolean
ms_escher_read_OleObject (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_DeletedPspl (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_SolverContainer (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ConnectorRule (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_AlignRule (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ArcRule (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ClientRule (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_CalloutRule (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_Selection (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_UserDefined (MSEscherState *state, MSEscherHeader *h)
{
	return FALSE;
}
static gboolean
ms_escher_read_Dg (MSEscherState *state, MSEscherHeader *h)
{
#if 0
	guint8 const *data = h->data + COMMON_HEADER_LEN;
	guint32 num_shapes = GSF_LE_GET_GUINT32(data);
	/* spid_cur = last SPID given to an SP in this DG :-)  */
	guint32 spid_cur   = GSF_LE_GET_GUINT32(data+4);
	guint32 drawing_id = h->instance;

	/* This drawing has these num_shapes shapes, with a pointer to the last
	 * SPID given to it */
#endif
	return FALSE;
}

static gboolean
ms_escher_read_Dgg (MSEscherState *state, MSEscherHeader *h)
{
#if 0
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
	guint8 const *data = h->data + COMMON_HEADER_LEN;
	fd.id_clusts = g_array_new (1, 1, sizeof(ID_CLUST));
	fd.max_spid           = GSF_LE_GET_GUINT32(data+ 0);
	fd.num_id_clust       = GSF_LE_GET_GUINT32(data+ 4);
	fd.num_shapes_saved   = GSF_LE_GET_GUINT32(data+ 8);
	fd.num_drawings_saved = GSF_LE_GET_GUINT32(data+12);

	g_printerr ("maxspid 0x%x clusts 0x%x shapes 0x%x drawings x%x\n",
		fd.max_spid, fd.num_id_clust, fd.num_shapes_saved,
		fd.num_drawings_saved);

	data+=16;
	for (lp = 0; lp < fd.num_id_clust; lp++) {
		ID_CLUST cl;
		cl.DG_owning_spids   = GSF_LE_GET_GUINT32(data+0);
		cl.spids_used_so_far = GSF_LE_GET_GUINT32(data+4);
		g_array_append_val (fd.id_clusts, cl);
	}
#endif
	return FALSE;
}

typedef struct {
	gint dummy;
} EscherOption;

typedef enum {
	shape_Lines = 0,        /* straight line segments */
	shape_LinesClosed = 1,  /* closed polygonal shape */
	shape_Curves = 2,       /* Bezier curve segments */
	shape_CurvesClosed = 3, /* A closed shape with curved edges */
	shape_Complex = 4       /* pSegmentInfo must be non-empty */
} ShapePath;

typedef enum {
	wrap_Square = 0,
	wrap_ByPoints = 1,
	wrap_None = 2,
	wrap_TopBottom = 3,
	wrap_Through = 4
} WrapMode;

typedef enum {
	bw_Color = 0,          /* only used for predefined shades */
	bw_Automatic = 1,      /* depends on object type */
	bw_GrayScale = 2,      /* shades of gray only */
	bw_LightGrayScale = 3, /* shades of light gray only */
	bw_InverseGray = 4,    /* dark gray mapped to light gray, etc. */
	bw_GrayOutline = 5,    /* pure gray and white */
	bw_BlackTextLine = 6,  /* black text and lines, all else grayscale */
	bw_HighContrast = 7,   /* pure black and white mode (no grays) */
	bw_Black = 7,          /* solid black */
	bw_White = 8,          /* solid white */
	bw_DontShow = 9        /* object not drawn */
} BlackWhiteMode;

typedef enum {
	anchor_Top,
	anchor_Middle,
	anchor_Bottom,
	anchor_TopCentered,
	anchor_MiddleCentered,
	anchor_BottomCentered,
	anchor_TopBaseline,
	anchor_BottomBaseline,
	anchor_TopCenteredBaseline,
	anchor_BottomCenteredBaseline
} AnchorType;

typedef enum {
	rotate_0 = 0,	/* Right */
	rotate_90 = 1,	/* Down */
	rotate_180 = 2,	/* Left */
	rotate_270 = 3	/* Up */
} RotationType;

typedef enum {
	connect_Straight = 0,
	connect_Bent = 1,
	connect_Curved = 2,
	connect_None = 3
} ConnectStyle;

typedef enum {
	flow_HorzN = 0,	/* Horizontal non-@ */
	flow_TtoBA = 1,	/* Top to Bottom @-font */
	flow_BtoT = 2,	/* Bottom to Top non-@ */
	flow_TtoBN = 3,	/* Top to Bottom non-@ */
	flow_HorzA = 4,	/* Horizontal @-font */
	flow_VertN = 5	/* Vertical, non-@ */
} TextFlow;

typedef enum {
	textdir_LtoR = 0,
	textdir_RtoL = 1,
	textdir_Context = 2	/* depends on context */
} TextDirection;

typedef enum {
	callout_RightAngle = 1,
	callout_OneSegment = 2,
	callout_TwoSegment = 3,
	callout_ThreeSegment = 4
} CalloutType;

typedef enum {
	callout_angle_Any = 0,
	callout_angle_30 = 1,
	callout_angle_45 = 2,
	callout_angle_60 = 3,
	callout_angle_90 = 4,
	callout_angle_0 = 5
} CallOutAngle;

typedef enum
{
	callout_drop_Top = 0,
	callout_drop_Center = 1,
	callout_drop_Bottom = 2,
	callout_drop_Specified = 3
} CalloutDrop;

/* Alignment - WordArt only */
typedef enum {
	align_TextStretch,      /* Stretch each line of text to fit width. */
	align_TextCenter,       /* Center text on width. */
	align_TextLeft,         /* Left justify. */
	align_TextRight,        /* Right justify. */
	align_TextLetterJust,   /* Spread letters out to fit width. */
	align_TextWordJust,     /* Spread words out to fit width. */
	align_TextInvalid       /* Invalid */
} Alignment;

typedef enum {
	render_FullRender,
	render_Wireframe,
	render_BoundingCube
} RenderMode;

typedef enum {
	transform_Absolute,   /* Apply in absolute space centered on shape */
	transform_Shape,      /* Apply to shape geometry */
	transform_Drawing     /* Apply in drawing space */
} Transform;

typedef enum {
	shadow_Offset,    /* N pixel offset shadow */
	shadow_Double,    /* Use second offset too */
	shadow_Rich,      /* Rich perspective shadow (cast relative to shape) */
	shadow_Shape,     /* Rich perspective shadow (cast in shape space) */
	shadow_Drawing,   /* Perspective shadow cast in drawing space */
	shadow_EmbossOrEngrave
} Shadow;

/* LengthMeasure - the type of a (length) measurement */
typedef enum {
	measure_Default      = 0,  /* Default size, ignore the values */
	measure_A            = 1,  /* Values are in EMUs */
	measure_V            = 2,  /* Values are in pixels */
	measure_Shape        = 3,  /* Values are 16.16 fractions of shape size */
	measure_FixedAspect  = 4,  /* Aspect ratio is fixed */
	measure_AFixed       = 5,  /* EMUs, fixed aspect ratio */
	measure_VFixed       = 6,  /* Pixels, fixed aspect ratio */
	measure_ShapeFixed   = 7,  /* Proportion of shape, fixed aspect ratio */
	measure_FixedAspectEnlarge = 8,  /*  Aspect ratio is fixed, favor larger size */
	measure_AFixedBig    = 9,  /* EMUs, fixed aspect ratio */
	measure_VFixedBig    = 10, /* Pixels, fixed aspect ratio */
	measure_ShapeFixedBig= 11  /* Proportion of shape, fixed aspect ratio */
} LengthMeasure;

typedef enum {
	fill_Solid = 0,
	fill_Pattern = 1,	/* bitmap */
	fill_Texture = 2,	/* pattern with private Colour map) */
	fill_Picture = 3,	/* Center picture on the shape */
	fill_Shade = 4,		/* Shade from start to end points */
	fill_ShadeCenter =5,	/* Shade from bounding rectangle to end point */
	fill_ShadeShape = 6,	/* Shade from shape outline to end point */
	fill_ShadeScale = 7,	/* Like fill_Shade, but fillAngle is also scaled by
				   the aspect ratio of the shape. If shape is square,
				   it is the same as fill_Shade. */
	fill_ShadeTitle = 8,	/* shade to title  ?? what is this for */
	fill_Background = 9	/* Use background fill color/pattern */
} FillType;

/* Colours in a shaded fill. */
typedef enum {
	shade_None  = 0,        /* Interpolate without correction between RGBs */
	shade_Gamma = 1,        /* Apply gamma correction to colors */
	shade_Sigma = 2,        /* Apply a sigma transfer function to position */
	shade_Band  = 4,        /* Add a flat band at the start of the shade */
	shade_OneColor = 8,     /* This is a one color shade */

	/* It looks like the top 16 bits of the val can be used as a parameter.
	 * The Sigma shade style seems to hard code that to be 0x4000 ??
	 */
	shade_ParamShift = 16,
	shade_ParamMask  = (int)0xffff0000,

	shade_Default = (shade_Gamma|shade_Sigma|(0x4000<<shade_ParamShift))
} ShadeType;

/* LineStyle - compound line style */
typedef enum {
	line_Simple,            /* Single line (of width lineWidth) */
	line_Double,            /* Double lines of equal width */
	line_ThickThin,         /* Double lines, one thick, one thin */
	line_ThinThick,         /* Double lines, reverse order */
	line_Triple             /* Three lines, thin, thick, thin */
} LineStyle;

typedef enum {
	line_fill_SolidType,         /* Fill with a solid color */
	line_fill_Pattern,           /* Fill with a pattern (bitmap) */
	line_fill_Texture,           /* A texture (pattern with its own color map) */
	line_fill_Picture            /* Center a picture in the shape */
} LineFill;

typedef enum {
	dash_Solid,              /* Solid (continuous) pen */
	dash_DashSys,            /* PS_DASH system   dash style */
	dash_DotSys,             /* PS_DOT system   dash style */
	dash_DashDotSys,         /* PS_DASHDOT system dash style */
	dash_DashDotDotSys,      /* PS_DASHDOTDOT system dash style */
	dash_DotGEL,             /* square dot style */
	dash_DashGEL,            /* dash style */
	dash_LongDashGEL,        /* long dash style */
	dash_DashDotGEL,         /* dash short dash */
	dash_LongDashDotGEL,     /* long dash short dash */
	dash_LongDashDotDotGEL   /* long dash short dash short dash */
} DashedLineStyle;

typedef enum {
	line_end_NoEnd,
	line_end_ArrowEnd,
	line_end_ArrowStealthEnd,
	line_end_ArrowDiamondEnd,
	line_end_ArrowOvalEnd,
	line_end_ArrowOpenEnd
} LineEndStyle;

typedef enum {
	arrow_width_Narrow = 0,
	arrow_width_Medium = 1,
	arrow_width_Wide = 2
} ArrowWidth;

typedef enum {
	arrow_len_Short = 0,
	arrow_len_Medium = 1,
	arrow_len_Long = 2
} ArrowLength;

typedef enum {
	line_join_Bevel,     /* Join edges by a straight line */
	line_join_Miter,     /* Extend edges until they join */
	line_join_Round      /* Draw an arc between the two edges */
} LineJoin;

/* Line cap style (applies to ends of dash segments too). */
typedef enum {
	line_cap_Round,   /* Rounded ends - the default */
	line_cap_Square,  /* Square protrudes by half line width */
	line_cap_Flat     /* Line ends at end point */
} LineCap;

typedef struct {
	char const	*name;
	unsigned	 pid;
	gboolean	 default_val;
	MSObjAttrID	 id;
} MSEscherBoolOptTable;

static MSEscherBoolOptTable const protection_bools[] = {
	{ "LockRotation",	119, FALSE, 0 },
	{ "LockAspectRatio",	120, FALSE, 0 },
	{ "LockPosition",	121, FALSE, 0 },
	{ "LockAgainstSelect",	122, FALSE, 0 },
	{ "LockCropping",	123, FALSE, 0 },
	{ "LockVertices",	124, FALSE, 0 },
	{ "LockText",		125, FALSE, 0 },
	{ "LockAdjustHandles",	126, FALSE, 0 },
	{ "LockAgainstGrouping",127, FALSE, 0 }
};

static MSEscherBoolOptTable const text_bools[] = {
	{ "fSelectText",	187,	 TRUE, 0 }, /* TRUE if single click selects text, FALSE if two clicks */
	{ "fAutoTextMargin",	188,	FALSE, 0 }, /* use host's margin calculations */
	{ "fRotateText",	189,	FALSE, 0 }, /* Rotate text with shape */
	{ "fFitShapeToText",	190,	FALSE, 0 }, /* Size shape to fit text size */
	{ "fFitTextToShape",	191,	FALSE, 0 }  /* Size text to fit shape size */
};

static MSEscherBoolOptTable const geotext_bools[] = {
	{ "gtextFReverseRows",	240,	FALSE, 0 }, /* Reverse row order */
	{ "fGtext",		241,	FALSE, 0 }, /* Has text effect */
	{ "gtextFVertical",	242,	FALSE, 0 }, /* Rotate characters */
	{ "gtextFKern",		243,	FALSE, 0 }, /* Kern characters */
	{ "gtextFTight",	244,	FALSE, 0 }, /* Tightening or tracking */
	{ "gtextFStretch",	245,	FALSE, 0 }, /* Stretch to fit shape */
	{ "gtextFShrinkFit",	246,	FALSE, 0 }, /* Char bounding box */
	{ "gtextFBestFit",	247,	FALSE, 0 }, /* Scale text-on-path */
	{ "gtextFNormalize",	248,	FALSE, 0 }, /* Stretch char height */
	{ "gtextFDxMeasure",	249,	FALSE, 0 }, /* Do not measure along path */
	{ "gtextFBold",		250,	FALSE, 0 }, /* Bold font */
	{ "gtextFItalic",	251,	FALSE, 0 }, /* Italic font */
	{ "gtextFUnderline",	252,	FALSE, 0 }, /* Underline font */
	{ "gtextFShadow",	253,	FALSE, 0 }, /* Shadow font */
	{ "gtextFSmallcaps",	254,	FALSE, 0 }, /* Small caps font */
	{ "gtextFStrikethrough",255,	FALSE, 0 }  /* Strike through font */
};

static MSEscherBoolOptTable const picture_bools[] = {
	{ "fNoHitTestPicture",	316,	FALSE, 0 }, /* Do not hit test the picture */
	{ "pictureGray",	317,	FALSE, 0 }, /* grayscale display */
	{ "pictureBiLevel",	318,	FALSE, 0 }, /* bi-level display */
	{ "pictureActive",	319,	FALSE, 0 }  /* Server is active (OLE objects only) */
};

static MSEscherBoolOptTable const geometry_bools[] = {
	{ "fShadowOK",		378,	 TRUE, 0 }, /* Shadow may be set */
	{ "f3DOK",		379,	 TRUE, 0 }, /* 3D may be set */
	{ "fLineOK",		380,	 TRUE, 0 }, /* Line style may be set */
	{ "fGtextOK",		381,	FALSE, 0 }, /* Text effect (WordArt) supported */
	{ "fFillShadeShapeOK",	382,	FALSE, 0 },
	{ "fFillOK",		383,	 TRUE, 0 }  /* OK to fill the shape through the UI or VBA? */
};

static MSEscherBoolOptTable const fill_bools[] = {
	{ "fFilled",		443,	 TRUE, MS_OBJ_ATTR_UNFILLED }, /* Is shape filled? */
	{ "fHitTestFill",	444,	 TRUE, 0 }, /* Should we hit test fill?  */
	{ "fillShape",		445,	 TRUE, 0 }, /* Register pattern on shape */
	{ "fillUseRect",	446,	FALSE, 0 }, /* Use the large rect? */
	{ "fNoFillHitTest",	447,	FALSE, 0 } /* Hit test a shape as though filled */
};

static MSEscherBoolOptTable const line_bools[] = {
	{ "fArrowheadsOK",	507,	FALSE, 0 }, /* Allow arrowheads if prop. is set */
	{ "fLine",		508,	 TRUE, MS_OBJ_ATTR_OUTLINE_HIDE }, /* Any line? */
	{ "fHitTestLine",	509,	 TRUE, 0 }, /* Should we hit test lines?  */
	{ "lineFillShape",	510,	 TRUE, 0 }, /* Register pattern on shape */
	{ "fNoLineDrawDash",	511,	FALSE, 0 }  /* Draw a dashed line if no line */
};

static MSEscherBoolOptTable const shadow_bools[] = {
	{ "fShadow",		574,	FALSE, 0 }, /* Any shadow? */
	{ "fshadowObscured",	575,	FALSE, 0 } /* Excel5-style shadow */
};

static MSEscherBoolOptTable const three_d_obj_bools[] = {
	{ "f3D",			700,	FALSE, 0 }, /* Does this shape have a 3D effect? */
	{ "fc3DMetallic",		701,	FALSE, 0 }, /* metallic specularity? */
	{ "fc3DUseExtrusionColor",	702,	FALSE, 0 },
	{ "fc3DLightFace",		703,	 TRUE, 0 }
};

static MSEscherBoolOptTable const three_d_style_bools[] = {
	{ "fc3DConstrainRotation",	763,	 TRUE, 0 },
	{ "fc3DRotationCenterAuto",	764,	FALSE, 0 },
	{ "fc3DParallel",		765,	 TRUE, 0 }, /* projection? */
	{ "fc3DKeyHarsh",		766,	 TRUE, 0 }, /* key lighting harsh? */
	{ "fc3DFillHarsh",		767,	FALSE, 0 } /* fill lighting harsh?` */
};

static MSEscherBoolOptTable const shape_bools[] = {
	{ "fOleIcon",			826,	FALSE, 0 }, /* For OLE objects, whether the object is in icon form */
	{ "fPreferRelativeResize",	827,	FALSE, 0 }, /* For UI only. Prefer relative resizing.  */
	{ "fLockShapeType",		828,	FALSE, 0 }, /* Lock the shape type (don't allow Change Shape) */
	{ "fDeleteAttachedObject",	830,	FALSE, 0 },
	{ "fBackground",		831,	FALSE, 0 } /* If TRUE, this is the background shape. */
};

static MSEscherBoolOptTable const callout_bools[] = {
	{ "fCallout",			889,	FALSE, 0 }, /* Is the shape a callout? */
	{ "fCalloutAccentBar",		890,	FALSE, 0 }, /* does callout have accent bar */
	{ "fCalloutTextBorder",		891,	 TRUE, 0 }, /* does callout have a text border */
	{ "fCalloutMinusX",		892,	FALSE, 0 },
	{ "fCalloutMinusY",		893,	FALSE, 0 },
	{ "fCalloutDropAuto",		894,	FALSE, 0 }, /* If true, then we occasionally invert the drop distance */
	{ "fCalloutLengthSpecified",	895,	FALSE, 0 }  /* if true, we look at dxyCalloutLengthSpecified */
};

static MSEscherBoolOptTable const group_bools[] = {
	{ "fEditedWrap",	953,	FALSE, 0 }, /* Has the wrap polygon been edited? */
	{ "fBehindDocument",	954,	FALSE, 0 }, /* Word-only (shape is behind text) */
	{ "fOnDblClickNotify",	955,	FALSE, 0 }, /* Notify client on a double click */
	/* A button shape (i.e., clicking performs an action).  Set for shapes
	 * with attached hyperlinks or macros. */
	{ "fIsButton",		956,	FALSE, 0 },
	{ "fOneD",		957,	FALSE, 0 }, /* 1D adjustment */
	{ "fHidden",		958,	FALSE, 0 }, /* Do not display */
	{ "fPrint",		959,	 TRUE, 0 }  /* Print this shape */
};

static void
ms_escher_read_OPT_bools (MSEscherHeader *h,
			  MSEscherBoolOptTable const *bools, unsigned n_bools,
			  guint pid, guint32 const val)
{
	unsigned i;
	guint32 mask = 0x10000 << (n_bools - 1);
	guint32 bit  = 0x00001 << (n_bools - 1);

	g_return_if_fail (n_bools > 0);
	g_return_if_fail (bools[n_bools-1].pid == pid);

	d (2, g_printerr ("Set of Bools %d-%d = 0x%08x;\n{\n",
		      bools[0].pid, bools[n_bools-1].pid, val););

	pid -= (n_bools - 1);
	for (i = 0; i < n_bools; i++, mask >>= 1, bit >>= 1, pid++) {
		gboolean set_val = ((val & bit) == bit);
		gboolean def_val = bools[i].default_val;
		const MSObjAttrID aid = bools[i].id;

		if (!(val & mask))	/* the value is set */
			continue;

		d (0, g_printerr ("bool %s(%d) = %s; /* def: %s; gnm: %d */\n",
			      bools[i].name, pid,
			      set_val ? "true" : "false",
			      def_val ? "true" : "false",
			      aid););

		if (set_val != def_val && aid != 0)
			ms_escher_header_add_attr
				(h, ms_obj_attr_new_flag (aid));
	}
	d (2, g_printerr ("};\n"););
}

static gboolean
ms_escher_read_OPT (MSEscherState *state, MSEscherHeader *h)
{
	int const num_properties = h->instance;
	gboolean needs_free;
	guint8 const *data = ms_escher_get_data (state,
		h->offset + COMMON_HEADER_LEN, h->len - COMMON_HEADER_LEN, &needs_free);
	guint8 const *fopte = data;
	guint8 const *extra = fopte + 6*num_properties;
	guint prev_pid = 0; /* A debug tool */
	char const *name;
	int i;

	/* let's be really careful */
	g_return_val_if_fail (6*num_properties + COMMON_HEADER_LEN <= h->len, TRUE);
	g_return_val_if_fail (data != NULL, TRUE);

	for (i = 0; i < num_properties; ++i, fopte += 6) {
		guint16 const tmp = GSF_LE_GET_GUINT32(fopte);
		guint const pid = tmp & 0x3fff;
		gboolean const is_blip = (tmp & 0x4000) != 0;
		gboolean const is_complex = (tmp & 0x8000) != 0;
		guint32 val = GSF_LE_GET_GUINT32 (fopte+2);
		MSObjAttrID id = MS_OBJ_ATTR_NONE;

		/* container is sorted by pid. Use this as sanity test */
		if (prev_pid >= pid) {
			g_printerr ("Pids not monotonic %d >= %d;\n", prev_pid, pid);
			if (needs_free)
				g_free ((guint8 *)data);
			return TRUE;
		}
		prev_pid = pid;

		name = NULL;
		switch (pid) {
	/* Transofrmation */
		/* 0 : fixed point: 16.16 degrees */
		case 4 : name = "long rotation"; break;

	/* Protection */
		case 119 : case 120 : case 121 : case 122 : case 123 :
		case 124 : case 125 : case 126 : case 127 :
			ms_escher_read_OPT_bools (h,
				protection_bools, G_N_ELEMENTS (protection_bools),
				pid, val);
			break;

	/* Text */
		/* 0 : id for the text, value determined by the host */
		case 128 : name = "long Txid"; break;
		/* 1/10" : margins relative to shape's inscribed text rectangle (in EMUs) */
		case 129 : name = "long dxTextLeft"; break;
		/* 1/20" :  */
		case 130 : name = "long dyTextTop"; break;
		/* 1/10" :  */
		case 131 : name = "long dxTextRight"; break;
		/* 1/20" :  */
		case 132 : name = "long dyTextBottom"; break;
		/* Square */
		case 133 : name = "WrapMode wrap_text_at_margin"; break;
		/* 0 : Text zoom/scale (used if fFitTextToShape) */
		case 134 : name = "long scaleText"; break;
		/* anchor_Top : How to anchor the text */
		case 135 : name = "AnchorType anchorText"; break;
		/* HorzN : Text flow */
		case 136 : name = "TextFlow txflTextFlow"; break;
		/* msocdir0 : Font rotation */
		case 137 : name = "RotationType cdirFont"; break;
		/* NULL : ID of the next shape (used by Word for linked textboxes) */
		case 138 : name = "MSOHSP hspNext"; break;
		/* LTR : Bi-Di Text direction */
		case 139 : name = "TextDirection txdir"; break;

		case 187 : case 188 : case 189 : case 190 : case 191 :
			ms_escher_read_OPT_bools (h,
				text_bools, G_N_ELEMENTS (text_bools),
				pid, val);
			break;

	/* GeoText */
		/* NULL : UNICODE text string */
		case 192 : name = "wchar* gtextUNICODE"; break;
		/* NULL : RTF text string */
		case 193 : name = "char* gtextRTF"; break;
		/* Center : alignment on curve */
		case 194 : name = "Alignment gtextAlign"; break;
		/* 36<<16 : default point size */
		case 195 : name = "long gtextSize"; break;
		/* 1<<16 : fixed point 16.16 */
		case 196 : name = "long gtextSpacing"; break;
		/* NULL : font family name */
		case 197 : name = "wchar* gtextFont"; break;

		case 240 : case 241 : case 242 : case 243 : case 244 :
		case 245 : case 246 : case 247 : case 248 : case 249 :
		case 250 : case 251 : case 252 : case 253 : case 254 :
		case 255 :
			ms_escher_read_OPT_bools (h,
				geotext_bools, G_N_ELEMENTS (geotext_bools),
				pid, val);
			break;

	/* Blip */
		/* 0 : 16.16 fraction times total image width or height, as appropriate. */
		case 256 : name = "fixed16_16 cropFromTop";
			   id = MS_OBJ_ATTR_BLIP_CROP_TOP;
			   break;
		case 257 : name = "fixed16_16 cropFromBottom";
			   id = MS_OBJ_ATTR_BLIP_CROP_BOTTOM;
			   break;
		case 258 : name = "fixed16_16 cropFromLeft";
			   id = MS_OBJ_ATTR_BLIP_CROP_LEFT;
			   break;
		case 259 : name = "fixed16_16 cropFromRight";
			   id = MS_OBJ_ATTR_BLIP_CROP_RIGHT;
			   break;

		/* NULL : Blip to display */
		case 260 : id = MS_OBJ_ATTR_BLIP_ID;
			   name = "Blip * pib";
			   break;

		/* NULL : Blip file name */
		case 261 : name = "wchar * pibName"; break;
		/* What are BlipFlags ? */
		/* Comment Blip flags */
		case 262 : name = "BlipType pibFlags"; break;
		/* ~0 : transparent color (none if ~0UL)  */
		case 263 : name = "long pictureTransparent"; break;
		/* 1<<16 : contrast setting */
		case 264 : name = "long pictureContrast"; break;
		/* 0 : brightness setting */
		case 265 : name = "long pictureBrightness"; break;
		/* 0 : 16.16 gamma */
		case 266 : name = "fixed16_16 pictureGamma"; break;
		/* 0 : Host-defined ID for OLE objects (usually a pointer) */
		case 267 : name = "Long pictureId"; break;
		/* undefined : Double shadow Colour */
		case 268 : name = "Colour pictureDblCrMod"; break;
		/* undefined : */
		case 269 : name = "Colour pictureFillCrMod"; break;
		/* undefined : */
		case 270 : name = "Colour pictureLineCrMod"; break;
		/* NULL : Blip to display when printing */
		case 271 : name = "Blip * pibPrint"; break;
		/* NULL : Blip file name */
		case 272 : name = "wchar * pibPrintName"; break;
		/* Comment Blip flags */
		case 273 : name = "BlipType pibPrintFlags"; break;

		case 316 : case 317 : case 318 : case 319 :
			ms_escher_read_OPT_bools (h,
				picture_bools, G_N_ELEMENTS (picture_bools),
				pid, val);
			break;

	/* Geometry */
		/* 0 : Defines the G (geometry) coordinate space. */
		case 320 : name = "long geoLeft"; break;
		/* 0 :  */
		case 321 : name = "long geoTop"; break;
		/* 21600 :  */
		case 322 : name = "long geoRight"; break;
		/* 21600 :  */
		case 323 : name = "long geoBottom"; break;
		/* msoshapeLinesClosed :  */
		case 324 : name = "ShapePath shapePath"; break;
		/* NULL : An array of points, in G units. */
		case 325 : name = "IMsoArray pVertices"; break;
		/* NULL :  */
		case 326 : name = "IMsoArray pSegmentInfo"; break;
		/* 0 : Adjustment values corresponding to the positions of the
		 * adjust handles of the shape. The number of values used and
		 * their allowable ranges vary from shape type to shape type.
		 */
		case 327 : name = "long adjustValue"; break;
		/* 0 :  */
		case 328 : name = "long adjust2Value"; break;
		/* 0 :  */
		case 329 : name = "long adjust3Value"; break;
		/* 0 :  */
		case 330 : name = "long adjust4Value"; break;
		/* 0 :  */
		case 331 : name = "long adjust5Value"; break;
		/* 0 :  */
		case 332 : name = "long adjust6Value"; break;
		/* 0 :  */
		case 333 : name = "long adjust7Value"; break;
		/* 0 :  */
		case 334 : name = "long adjust8Value"; break;
		/* 0 :  */
		case 335 : name = "long adjust9Value"; break;
		/* 0 :  */
		case 336 : name = "long adjust10Value"; break;

		case 378 : case 379 : case 380 : case 381 : case 382 :
		case 383 :
			ms_escher_read_OPT_bools (h,
				geometry_bools, G_N_ELEMENTS (geometry_bools),
				pid, val);
			break;

	/* FillStyle */
		/* Solid : Type of fill */
		case 384 : id = MS_OBJ_ATTR_FILL_TYPE;
			   name = "FillType fillType";
			   break;

		/* white : Foreground color */
		case 385 : id = MS_OBJ_ATTR_FILL_COLOR;
			   name = "Colour fillColor";
			   break;

		/* 1<<16 : Fixed 16.16 */
		case 386 : id = MS_OBJ_ATTR_FILL_ALPHA;
			   name = "long fillOpacity";
			   break;
		/* white : Background color */
		case 387 : id = MS_OBJ_ATTR_FILL_BACKGROUND;
			   name = "Colour fillBackColor";
			   break;
		/* 1<<16 : Shades only */
		case 388 : id = MS_OBJ_ATTR_FILL_BACKGROUND_ALPHA;
			   name = "long fillBackOpacity";
			   break;
		/* undefined : Modification for BW views */
		case 389 : name = "Colour fillCrMod"; break;
		/* NULL : Pattern/texture */
		case 390 : name = "IMsoBlip* fillBlip"; break;
		/* NULL : Blip file name */
		case 391 : name = "wchar* fillBlipName"; break;
		/* Comment : Blip flags */
		case 392 : name = "BlipFlags fillBlipFlags"; break;
		/* 0 : How big (A units) to make a metafile texture. */
		case 393 : name = "long fillWidth"; break;
		/* 0 :  */
		case 394 : name = "long fillHeight"; break;
		/* 0 : Fade angle - degrees in 16.16 */
		case 395 : id = MS_OBJ_ATTR_FILL_ANGLE;
			   name = "long fillAngle";
			   break;
		/* 0 : Linear shaded fill focus percent */
		case 396 : id = MS_OBJ_ATTR_FILL_FOCUS;
			   name = "long fillFocus";
			   break;
		/* 0 : Fraction 16.16 */
		case 397 : name = "long fillToLeft"; break;
		/* 0 : Fraction 16.16 */
		case 398 : name = "long fillToTop"; break;
		/* 0 : Fraction 16.16 */
		case 399 : name = "long fillToRight"; break;
		/* 0 : Fraction 16.16 */
		case 400 : name = "long fillToBottom"; break;
		/* 0 : For shaded fills, use the specified rectangle instead of
		 * the shape's bounding rect to define how large the fade is
		 * going to be. */
		case 401 : name = "long fillRectLeft"; break;
		/* 0 :  */
		case 402 : name = "long fillRectTop"; break;
		/* 0 :  */
		case 403 : name = "long fillRectRight"; break;
		/* 0 :  */
		case 404 : name = "long fillRectBottom"; break;
		/* measure_Default :  */
		case 405 : name = "LengthMeasure fillDztype"; break;
		/* 0 : Special shades */
		case 406 : id = MS_OBJ_ATTR_FILL_PRESET;
			   name = "long fillShadePreset";
			   break;
		/* NULL : a preset array of colors */
		case 407 : name = "IMsoArray fillShadeColors"; break;
		/* 0 :  */
		case 408 : name = "long fillOriginX"; break;
		/* 0 :  */
		case 409 : name = "long fillOriginY"; break;
		/* 0 :  */
		case 410 : name = "long fillShapeOriginX"; break;
		/* 0 :  */
		case 411 : name = "long fillShapeOriginY"; break;
		/* shade_Default : Type of shading, if a shaded (gradient) fill. */
		case 412 : id = MS_OBJ_ATTR_FILL_SHADE_TYPE;
			   name = "ShadeType fillShadeType";
			   break;

		case 443 : case 444 : case 445 : case 446 : case 447 :
			ms_escher_read_OPT_bools (h,
				fill_bools, G_N_ELEMENTS (fill_bools),
				pid, val);
			break;

	/* LineStyle */
		/* black : Color of line */
		case 448 : id = MS_OBJ_ATTR_OUTLINE_COLOR;
			   name = "Colour lineColor";
			   break;
		/* 1<<16 : Not implemented */
		case 449 : name = "long lineOpacity"; break;
		/* white : Background color */
		case 450 : name = "Colour lineBackColor"; break;
		/* undefined : Modification for BW views */
		case 451 : name = "Colour lineCrMod"; break;
		/* Solid : Type of line */
		case 452 : name = "LineFill lineType"; break;
		/* NULL : Pattern/texture */
		case 453 : name = "IMsoBlip* lineFillBlip"; break;
		/* NULL : Blip file name */
		case 454 : name = "wchar* lineFillBlipName"; break;
		/* Comment : Blip flags */
		case 455 : name = "BlipFlags lineFillBlipFlags"; break;
		/* 0 : How big (A units) to make a metafile texture. */
		case 456 : name = "long lineFillWidth"; break;
		/* 0 :  */
		case 457 : name = "long lineFillHeight"; break;
		/* measure_Default : How to interpret fillWidth/Height numbers. */
		case 458 : name = "LengthMeasure lineFillDztype"; break;
		/* 9525 : A units; 1pt == 12700 EMUs */
		case 459 : id = MS_OBJ_ATTR_OUTLINE_WIDTH;
			   name = "long lineWidth";
			   val = val * 256. / 12700.; /* convert to 256th of pt */
			   break;
		/* 8<<16 : ratio (16.16) of width */
		case 460 : name = "long lineMiterLimit"; break;
		/* Simple : Draw parallel lines? */
		case 461 : name = "LineStyle lineStyle"; break;
		/* Solid : Can be overridden by: */
		case 462 : id = MS_OBJ_ATTR_OUTLINE_STYLE;
			   name = "DashedLineStyle lineDashing";
			   break;
		/* NULL : As Win32 ExtCreatePen */
		case 463 : name = "IMsoArray lineDashStyle"; break;
		/* NoEnd : Arrow at start */
		case 464 : id = MS_OBJ_ATTR_ARROW_START;
			   name = "LineEndStyle lineStartArrowhead";
			   break;
		/* NoEnd : Arrow at end */
		case 465 : id = MS_OBJ_ATTR_ARROW_END;
			   name = "LineEndStyle lineEndArrowhead";
			   break;
		/* MediumWidthArrow : Arrow at start */
		case 466 : id = MS_OBJ_ATTR_ARROW_START_WIDTH;
			   name = "ArrowWidth lineStartArrowWidth"; break;
		/* MediumLenArrow : Arrow at end */
		case 467 : id = MS_OBJ_ATTR_ARROW_START_LENGTH;
			   name = "ArrowLength lineStartArrowLength"; break;
		/* MediumWidthArrow : Arrow at start */
		case 468 : id = MS_OBJ_ATTR_ARROW_END_WIDTH;
			   name = "ArrowWidth lineEndArrowWidth"; break;
		/* MediumLenArrow : Arrow at end */
		case 469 : id = MS_OBJ_ATTR_ARROW_END_LENGTH;
			   name = "ArrowLength lineEndArrowLength"; break;
		/* JoinRound : How to join lines */
		case 470 : name = "LineJoin lineJoinStyle"; break;
		/* EndCapFlat : How to end lines */
		case 471 : name = "LineCap lineEndCapStyle"; break;

		case 507 : case 508 : case 509 : case 510 : case 511 :
			ms_escher_read_OPT_bools (h,
				line_bools, G_N_ELEMENTS (line_bools),
				pid, val);
			break;

	/* ShadowStyle */
		/* Offset : Type of effect */
		case 512 : name = "Shadow shadowType"; break;
		/* 0x808080 : Foreground color */
		case 513 : name = "Colour shadowColor"; break;
		/* 0xCBCBCB : Embossed color */
		case 514 : name = "Colour shadowHighlight"; break;
		/* undefined : Modification for BW views */
		case 515 : name = "Colour shadowCrMod"; break;
		/* 1<<16 : Fixed 16.16 */
		case 516 : name = "long shadowOpacity"; break;
		/* 25400 : Offset shadow */
		case 517 : name = "long shadowOffsetX"; break;
		/* 25400 : Offset shadow */
		case 518 : name = "long shadowOffsetY"; break;
		/* 0 : Double offset shadow */
		case 519 : name = "long shadowSecondOffsetX"; break;
		/* 0 : Double offset shadow */
		case 520 : name = "long shadowSecondOffsetY"; break;
		/* 1<<16 : 16.16 */
		case 521 : name = "long shadowScaleXToX"; break;
		/* 0 : 16.16 */
		case 522 : name = "long shadowScaleYToX"; break;
		/* 0 : 16.16 */
		case 523 : name = "long shadowScaleXToY"; break;
		/* 1<<16 : 16.16 */
		case 524 : name = "long shadowScaleYToY"; break;
		/* 0 : 16.16 / weight */
		case 525 : name = "long shadowPerspectiveX"; break;
		/* 0 : 16.16 / weight */
		case 526 : name = "long shadowPerspectiveY"; break;
		/* 1<<8 : scaling factor */
		case 527 : name = "long shadowWeight"; break;
		/* 0 :  */
		case 528 : name = "long shadowOriginX"; break;
		/* 0 :  */
		case 529 : name = "long shadowOriginY"; break;
		case 574 : case 575 :
			ms_escher_read_OPT_bools (h,
				shadow_bools, G_N_ELEMENTS (shadow_bools),
				pid, val);
			break;

	/* PerspectiveStyle */
		/* Shape : Where transform applies */
		case 576 : name = "Transform perspectiveType"; break;
		/* 0 : The long values define a transformation matrix,
		 * effectively, each value is scaled by the perspectiveWeight
		 * parameter. */
		case 577 : name = "long perspectiveOffsetX"; break;
		/* 0 :  */
		case 578 : name = "long perspectiveOffsetY"; break;
		/* 1<<16 :  */
		case 579 : name = "long perspectiveScaleXToX"; break;
		/* 0 :  */
		case 580 : name = "long perspectiveScaleYToX"; break;
		/* 0 :  */
		case 581 : name = "long perspectiveScaleXToY"; break;
		/* 1<<16 :  */
		case 582 : name = "long perspectiveScaleYToY"; break;
		/* 0 :  */
		case 583 : name = "long perspectivePerspectiveX"; break;
		/* 0 :  */
		case 584 : name = "long perspectivePerspectiveY"; break;
		/* 1<<8 : Scaling factor */
		case 585 : name = "long perspectiveWeight"; break;
		/* 1<<15 :  */
		case 586 : name = "long perspectiveOriginX"; break;
		/* 1<<15 :  */
		case 587 : name = "long perspectiveOriginY"; break;
		/* FALSE : On/off */
		case 639 : name = "bool fPerspective"; break;

	/* 3D Object */
		/* 0 : Fixed-point 16.16 */
		case 640 : name = "long DSpecularAmt"; break;
		/* 65536 : Fixed-point 16.16 */
		case 641 : name = "long c3DDiffuseAmt"; break;
		/* 5 : Default gives OK results */
		case 642 : name = "long c3DShininess"; break;
		/* 12700 : Specular edge thickness */
		case 643 : name = "long c3DEdgeThickness"; break;
		/* 0 : Distance of extrusion in EMUs */
		case 644 : name = "long c3DExtrudeForward"; break;
		/* 457200 :  */
		case 645 : name = "long c3DExtrudeBackward"; break;
		/* 0 : Extrusion direction */
		case 646 : name = "long c3DExtrudePlane"; break;
		/* FillThenLine : Basic color of extruded part of shape; the
		 * lighting model used will determine the exact shades used
		 * when rendering.  */
		case 647 : name = "Colour c3DExtrusionColor"; break;
		/* undefined : Modification for BW views */
		case 648 : name = "Colour c3DCrMod"; break;
		case 700 : case 701 : case 702 : case 703 :
			ms_escher_read_OPT_bools (h,
				three_d_obj_bools, G_N_ELEMENTS (three_d_obj_bools),
				pid, val);
			break;

	/* 3D Style */
		/* 0 : degrees (16.16) about y axis */
		case 704 : name = "long c3DYRotationAngle"; break;
		/* 0 : degrees (16.16) about x axis */
		case 705 : name = "long c3DXRotationAngle"; break;
		/* 100 : These specify the rotation axis; only their relative magnitudes matter. */
		case 706 : name = "long c3DRotationAxisX"; break;
		/* 0 :  */
		case 707 : name = "long c3DRotationAxisY"; break;
		/* 0 :  */
		case 708 : name = "long c3DRotationAxisZ"; break;
		/* 0 : degrees (16.16) about axis */
		case 709 : name = "long c3DRotationAngle"; break;
		/* 0 : rotation center x (16.16 or g-units) */
		case 710 : name = "long c3DRotationCenterX"; break;
		/* 0 : rotation center y (16.16 or g-units) */
		case 711 : name = "long c3DRotationCenterY"; break;
		/* 0 : rotation center z (absolute (emus)) */
		case 712 : name = "long c3DRotationCenterZ"; break;
		/* FullRender : Full,wireframe, or bcube */
		case 713 : name = "RenderMode c3DRenderMode"; break;
		/* 30000 : pixels (16.16) */
		case 714 : name = "long c3DTolerance"; break;
		/* 1250000 : X view point (emus) */
		case 715 : name = "long c3DXViewpoint"; break;
		/* -1250000 : Y view point (emus) */
		case 716 : name = "long c3DYViewpoint"; break;
		/* 9000000 : Z view distance (emus) */
		case 717 : name = "long c3DZViewpoint"; break;
		/* 32768 :  */
		case 718 : name = "long c3DOriginX"; break;
		/* -32768 :  */
		case 719 : name = "long c3DOriginY"; break;
		/* -8847360 : degree (16.16) skew angle */
		case 720 : name = "long c3DSkewAngle"; break;
		/* 50 : Percentage skew amount */
		case 721 : name = "long c3DSkewAmount"; break;
		/* 20000 : Fixed point intensity */
		case 722 : name = "long c3DAmbientIntensity"; break;
		/* 50000 : Key light source direc- */
		case 723 : name = "long c3DKeyX"; break;
		/* 0 : tion; only their relative */
		case 724 : name = "long c3DKeyY"; break;
		/* 10000 : magnitudes matter */
		case 725 : name = "long c3DKeyZ"; break;
		/* 38000 : Fixed point intensity */
		case 726 : name = "long c3DKeyIntensity"; break;
		/* -50000 : Fill light source direc- */
		case 727 : name = "long c3DFillX"; break;
		/* 0 : tion; only their relative */
		case 728 : name = "long c3DFillY"; break;
		/* 10000 : magnitudes matter */
		case 729 : name = "long c3DFillZ"; break;
		/* 38000 : Fixed point intensity */
		case 730 : name = "long c3DFillIntensity"; break;

		case 763 : case 764 : case 765 : case 766 : case 767 :
			ms_escher_read_OPT_bools (h,
				three_d_style_bools, G_N_ELEMENTS (three_d_style_bools),
				pid, val);
			break;

	/* Shape */
		/* NULL : master shape */
		case 769 : name = "MSOHSP pMaster"; break;
		/* None : Type of connector */
		case 771 : name = "ConnectStyle"; break;
		/* Automatic : Settings for modifications to be made when in
		 * different forms of black-and-white mode. */
		case 772 : name = "BlackWhiteMode bWMode"; break;
		/* Automatic :  */
		case 773 : name = "BlackWhiteMode bWModePureBW"; break;
		/* Automatic :  */
		case 774 : name = "BlackWhiteMode bWModeBW"; break;
		case 826 : case 827 : case 828 : case 830 : case 831 :
			ms_escher_read_OPT_bools (h,
				shape_bools, G_N_ELEMENTS (shape_bools),
				pid, val);
			break;

	/* CallOut */
		/* TwoSegment : CalloutType */
		case 832 : name = "CalloutType spcot"; break;
		/* 1/12" : Distance from box to first point.(EMUs) */
		case 833 : name = "long dxyCalloutGap"; break;
		/* Any : Callout angle */
		case 834 : name = "CallOutAngle spcoa"; break;
		/* Specified : Callout drop type */
		case 835 : name = "CalloutDrop spcod"; break;
		/* 9 points : if msospcodSpecified, the actual drop distance */
		case 836 : name = "long dxyCalloutDropSpecified"; break;
		/* 0 : if fCalloutLengthSpecified, the actual distance */
		case 837 : name = "long dxyCalloutLengthSpecified"; break;

		case 889 : case 890 : case 891 : case 892 : case 893 :
		case 894 : case 895 :
			ms_escher_read_OPT_bools (h,
				callout_bools, G_N_ELEMENTS (callout_bools),
				pid, val);
			break;

	/* GroupShape */
		/* NULL : Shape Name (present only if explicitly set) */
		case 896 : id = MS_OBJ_ATTR_OBJ_NAME;
			   name = "wchar* wzName";
			   break;
		/* NULL : alternate text */
		case 897 : id = MS_OBJ_ATTR_OBJ_ALT_TEXT;
			   name = "wchar* wzDescription";
			   break;

		/* NULL : The hyperlink in the shape. */
		case 898 : name = "IHlink* pihlShape"; break;
		/* NULL : The polygon that text will be wrapped around (Word) */
		case 899 : name = "IMsoArray pWrapPolygonVertices"; break;
		/* 1/8" : Left wrapping distance from text (Word) */
		case 900 : name = "long dxWrapDistLeft"; break;
		/* 0 : Top wrapping distance from text (Word) */
		case 901 : name = "long dyWrapDistTop"; break;
		/* 1/8" : Right wrapping distance from text (Word) */
		case 902 : name = "long dxWrapDistRight"; break;
		/* 0 : Bottom wrapping distance from text (Word) */
		case 903 : name = "long dyWrapDistBottom"; break;
		/* 0 : Regroup ID  */
		case 904 : name = "long lidRegroup"; break;

		case 953 : case 954 : case 955 : case 956 : case 957 :
		case 958 : case 959 :
			ms_escher_read_OPT_bools (h,
				group_bools, G_N_ELEMENTS (group_bools),
				pid, val);
			break;

		default : name = "UnknownID";
		}

		d (0, {
			if (NULL != name)
				   g_printerr ("%s %d = 0x%08x (=%d) %s%s;\n", name, pid, val, val,
			      is_blip ? " is blip" : "",
					   is_complex ? " is complex" : "");
		});

		/* TODO : use this for something */
		if (is_complex) {
			/* check for over run */
			g_return_val_if_fail (extra + val - data + COMMON_HEADER_LEN <= h->len, TRUE);

			d (5, gsf_mem_dump (extra, val););
			d (11, {
				static int count = 0;
				char *name = g_strdup_printf ("gnumeric-complex-opt-[%d]-%d", pid, count++);
				FILE *f = g_fopen (name, "w");
				if (f != NULL) {
					fwrite (extra, 1, val, f);
					fclose (f);
				}
				g_free (name);
			});
			if (id & MS_OBJ_ATTR_IS_PTR_MASK) {
				char *s = g_utf16_to_utf8 ((gunichar2*)extra, val / 2,
							   NULL, NULL, NULL);
				ms_escher_header_add_attr (h,
					ms_obj_attr_new_ptr (id, s));
				id = MS_OBJ_ATTR_NONE;
			}
			extra += val;
		}

		if (id & MS_OBJ_ATTR_IS_INT_MASK)
			ms_escher_header_add_attr (h,
				ms_obj_attr_new_uint (id, val));
		else if (id != MS_OBJ_ATTR_NONE) {
			g_warning ("We know attr[%d] with id == 0x%x, but do not store it ?", pid, id);
		}
	}
	if (needs_free)
		g_free ((guint8 *)data);

	return FALSE;
}

static gboolean
ms_escher_read_SpgrContainer (MSEscherState *state, MSEscherHeader *h)
{
	return ms_escher_read_container (state, h, 0, FALSE);
}
static gboolean
ms_escher_read_DgContainer (MSEscherState *state, MSEscherHeader *h)
{
	return ms_escher_read_container (state, h, 0, FALSE);
}
static gboolean
ms_escher_read_DggContainer (MSEscherState *state, MSEscherHeader *h)
{
	return ms_escher_read_container (state, h, 0, FALSE);
}
static gboolean
ms_escher_read_ClientTextbox (MSEscherState *state, MSEscherHeader *h)
{
	guint16 opcode;
	int has_next_record;
	char *text;
	PangoAttrList *markup;

	g_return_val_if_fail (h->len == COMMON_HEADER_LEN, TRUE);
	g_return_val_if_fail (h->offset + h->len == state->end_offset, TRUE);

	/* Read the TXO, be VERY careful until we are sure of the state */
	has_next_record = ms_biff_query_peek_next (state->q, &opcode);
	g_return_val_if_fail (has_next_record, TRUE);
	g_return_val_if_fail (opcode == BIFF_TXO, TRUE);
	has_next_record = ms_biff_query_next (state->q);
	g_return_val_if_fail (has_next_record, TRUE);

	text = ms_read_TXO (state->q, state->container, &markup);
	ms_escher_header_add_attr (h,
		ms_obj_attr_new_ptr (MS_OBJ_ATTR_TEXT, text));
	if (markup != NULL) {
		ms_escher_header_add_attr (h,
			ms_obj_attr_new_markup (MS_OBJ_ATTR_MARKUP, markup));
		pango_attr_list_unref (markup);
	}
	d (0, g_printerr ("'%s';\n", text););
	return FALSE;
}

static gboolean
ms_escher_read_ClientData (MSEscherState *state, MSEscherHeader *h)
{
	guint16 opcode;
	int has_next_record;

	g_return_val_if_fail (h->len == COMMON_HEADER_LEN, TRUE);
	g_return_val_if_fail (h->offset + h->len == state->end_offset, TRUE);

	/* Read the OBJ, be VERY careful until we are sure of the state */
	has_next_record = ms_biff_query_peek_next (state->q, &opcode);
	g_return_val_if_fail (has_next_record, TRUE);
	g_return_val_if_fail (opcode == BIFF_OBJ, TRUE);
	has_next_record = ms_biff_query_next (state->q);
	g_return_val_if_fail (has_next_record, TRUE);

	/* The object takes responsibility for the attributes */
	h->release_attrs = FALSE;
	if (ms_read_OBJ (state->q, state->container, h->attrs)) {
		h->attrs = NULL;  /* It got deleted.  */
		return TRUE;
	}

	return FALSE;
}

/****************************************************************************/

static gboolean
ms_escher_read_container (MSEscherState *state, MSEscherHeader *container,
			  gint prefix, gboolean return_attrs_in_container)
{
	MSEscherHeader h;

	g_return_val_if_fail (container != NULL, TRUE);

	ms_escher_header_init (&h);
	h.container = container;

	/* Skip the common header */
	h.offset = container->offset + prefix + COMMON_HEADER_LEN;

	do {
		guint16 tmp;
		char const *fbt_name = NULL;
		gboolean (*handler)(MSEscherState *state,
				    MSEscherHeader *container) = NULL;
		gboolean needs_free;
		guint32 datalen;

		guint8 const *data = ms_escher_get_data (state, h.offset,
			COMMON_HEADER_LEN, &needs_free);

		if (!data) {
			ms_escher_header_release (&h);
			return TRUE;
		}

		tmp	= GSF_LE_GET_GUINT16 (data + 0);
		h.fbt	= GSF_LE_GET_GUINT16 (data + 2);
		datalen = GSF_LE_GET_GUINT32 (data + 4);

		if (h.offset > state->end_offset || datalen >= 0xfffffff0) {
			g_warning ("Crazy data length in escher record.  (%d,%d,%d,%d)",
				   h.offset, state->end_offset, datalen,
				   (state->end_offset - h.offset));
			ms_escher_header_release (&h);
			return TRUE;
		}

		/* Include the length of this header in the record size */
		h.len	   = datalen + COMMON_HEADER_LEN;
		h.ver      = tmp & 0x0f;
		h.instance = (tmp >> 4) & 0xfff;

		d (0 , g_printerr ("length 0x%x(=%d), ver 0x%x, instance 0x%x, offset = 0x%x(=%d);\n",
			       h.len, h.len, h.ver, h.instance, h.offset, h.offset););

		if (needs_free)
			g_free ((void *)data);

		/*
		 * Let's double check that the data we just read makes sense.
		 * If problems arise in the next tests it probably indicates
		 * that the PRECEDING record length was invalid.  Check that
		 * it included the header.
		 */
		if ((h.fbt & (~0x1ff)) != 0xf000) {
			g_warning ("Invalid fbt = 0x%x\n", h.fbt);
			ms_escher_header_release (&h);
			return TRUE;
		}

#define EshRecord(x) \
		x : fbt_name = #x; \
		    handler = &ms_escher_read_ ## x; \
		break

		switch (h.fbt) {
		case EshRecord(DggContainer);	case EshRecord(Dgg);
		case EshRecord(DgContainer);	case EshRecord(Dg);
		case EshRecord(SpgrContainer);	case EshRecord(Spgr);
		case EshRecord(SpContainer);	case EshRecord(Sp);
		case EshRecord(BStoreContainer);case EshRecord(BSE);
		case EshRecord(Textbox);	case EshRecord(ClientTextbox);
		case EshRecord(Anchor); case EshRecord(ChildAnchor); case EshRecord(ClientAnchor);
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
		case EshRecord(UserDefined);
		default : fbt_name = NULL;
		}
#undef EshRecord

		if (Blip_START <= h.fbt && h.fbt <= Blip_END) {
			ms_escher_read_Blip (state, &h);
		} else if (fbt_name != NULL) {
			gboolean res;

			/* Not really needed */
			g_return_val_if_fail (handler != NULL, TRUE);

			d (0, g_printerr ("{ /* %s */\n", fbt_name););
			res = (*handler)(state, &h);

			d (0, g_printerr ("}; /* %s */\n", fbt_name););
			if (res) {
				ms_escher_header_release (&h);
				g_warning ("%s;", fbt_name);
				return TRUE;
			}

		} else
			g_warning ("Invalid fbt = %x;", h.fbt);

		h.offset += h.len;
	} while (h.offset < (container->offset + container->len));

	if (container->attrs == NULL && return_attrs_in_container) {
		container->attrs = h.attrs;
		h.release_attrs = FALSE;
	}
	ms_escher_header_release (&h);
	return FALSE;
}

/*
 * ms_escher_parse:
 * @q:
 * @container:    required workbook argument
 * @return_attrs: Should we steal our childrens attributes and return them
 *
 * Parse an escher stream stores in a sequence of biff records
 *
 * Returns NULL or an MSObjAttrBag (callers frees) depending on @return_attrs
 */
MSObjAttrBag *
ms_escher_parse (BiffQuery *q, MSContainer *container, gboolean return_attrs)
{
	MSEscherState state;
	MSEscherHeader fake_header;
	MSObjAttrBag *res = NULL;
	char const *drawing_record_name = "Unknown";

	g_return_val_if_fail (q != NULL, NULL);

	if (q->opcode == BIFF_MS_O_DRAWING)
		drawing_record_name = "Drawing";
	else if (q->opcode == BIFF_MS_O_DRAWING_GROUP)
		drawing_record_name = "Drawing Group";
	else if (q->opcode == BIFF_MS_O_DRAWING_SELECTION)
		drawing_record_name = "Drawing Selection";
	else if (q->opcode == BIFF_CHART_gelframe)
		drawing_record_name = "Chart GelFrame";
	else {
		g_warning ("EXCEL : unexpected biff type %x;", q->opcode);
		return NULL;
	}

	state.container	   = container;
	state.q		   = q;
	state.segment_len  = q->length;
	state.start_offset = 0;
	state.end_offset   = q->length;

	ms_escher_header_init (&fake_header);
	fake_header.container = NULL;
	fake_header.offset = 0;

	d (0, g_printerr ("{  /* Escher '%s'*/\n", drawing_record_name););
	ms_escher_read_container (&state,
		&fake_header, -COMMON_HEADER_LEN, return_attrs);
	d (0, g_printerr ("}; /* Escher '%s'*/\n", drawing_record_name););

	if (return_attrs) {
		res = fake_header.attrs;
		fake_header.release_attrs = FALSE;
	}
	ms_escher_header_release (&fake_header);
	return res;
}

/*********************************************************************/

guint
ms_escher_get_inst (GString *buf, gsize marker)
{
	guint16 iv = GSF_LE_GET_GUINT16 (buf->str + marker);
	return (iv >> 4);
}

void
ms_escher_set_inst (GString *buf, gsize marker, guint inst)
{
	guint16 iv = GSF_LE_GET_GUINT16 (buf->str + marker);
	GSF_LE_SET_GUINT16 (buf->str + marker,
			    (iv & 15) | (inst << 4));
}

gsize
ms_escher_spcontainer_start (GString *buf)
{
	gsize res = buf->len;
	guint8 tmp[8] = { 0xf, 0, 4, 0xf0, 0xde, 0xad, 0xbe, 0xef };
	g_string_append_len (buf, tmp, sizeof tmp);
	return res;
}

void
ms_escher_spcontainer_end (GString *buf, gsize marker)
{
	/* Length does not include header.  */
	gsize len = buf->len - marker - 8;
	GSF_LE_SET_GUINT32 (buf->str + marker + 4, len);
}

void
ms_escher_sp (GString *buf, guint32 spid, guint16 shape, guint32 flags)
{
	gsize marker = buf->len;
	guint8 tmp[16] = {
		0xa2,  0xc, 0xa, 0xf0, 0xde, 0xad, 0xbe, 0xef,
		0, 0, 0, 0, 0, 0, 0, 0
	};
	GSF_LE_SET_GUINT32 (tmp + 4, sizeof (tmp) - 8);
	GSF_LE_SET_GUINT32 (tmp + 8, spid);
	GSF_LE_SET_GUINT32 (tmp + 12, flags);
	g_string_append_len (buf, tmp, sizeof tmp);
	ms_escher_set_inst (buf, marker, shape);
}

gsize
ms_escher_opt_start (GString *buf)
{
	gsize res = buf->len;
	guint8 tmp[8] = { 0x03,   0, 0xb, 0xf0, 0xde, 0xad, 0xbe, 0xef };
	g_string_append_len (buf, tmp, sizeof tmp);
	return res;
}

void
ms_escher_opt_end (GString *buf, gsize marker)
{
	/* Length does not include header.  */
	gsize len = buf->len - marker - 8;
	GSF_LE_SET_GUINT32 (buf->str + marker + 4, len);
}

void
ms_escher_opt_add_simple (GString *buf, gsize marker, guint16 pid, gint32 val)
{
	guint8 tmp[6];

	GSF_LE_SET_GUINT16 (tmp, pid);
	GSF_LE_SET_GINT32 (tmp + 2, val);
	g_string_append_len (buf, tmp, sizeof tmp);

	ms_escher_set_inst (buf, marker,
			    ms_escher_get_inst (buf, marker) + 1);
}

void
ms_escher_opt_add_bool (GString *buf, gsize marker,
			guint16 pid, gboolean b)
{
	int N = ms_escher_get_inst (buf, marker);
	guint16 gid = pid | 0xf;
	guint8 shift = gid - pid;
	guint32 val = (b ? 0x00010001 : 0x00010000) << shift;

	if (N > 0 && GSF_LE_GET_GUINT16 (buf->str + buf->len - 6) == gid) {
		val |= GSF_LE_GET_GUINT32 (buf->str + buf->len - 4);
		GSF_LE_SET_GUINT32 (buf->str + buf->len - 4, val);
	} else
		ms_escher_opt_add_simple (buf, marker, gid, val);
}

void
ms_escher_opt_add_color (GString *buf, gsize marker,
			 guint16 pid, GOColor c)
{
	guint8 r = GO_COLOR_UINT_R (c);
	guint8 g = GO_COLOR_UINT_G (c);
	guint8 b = GO_COLOR_UINT_B (c);
	guint32 val = r | (g << 8) | (b << 16);
	ms_escher_opt_add_simple (buf, marker, pid, val);
}

void
ms_escher_opt_add_str_wchar (GString *buf, gsize marker, GString *extra,
			     guint16 pid, const char *str)
{
	glong ic;
	gunichar2 *str16 = g_utf8_to_utf16 (str, -1, NULL, &ic, NULL);
	guint8 tmp[6];

	GSF_LE_SET_GUINT16 (tmp, pid | 0x8000);
	GSF_LE_SET_GUINT32 (tmp + 2, ic * 2 + 2);
	g_string_append_len (buf, tmp, sizeof tmp);
	g_string_append_len (extra, (gpointer)str16, ic * 2 + 2);

	g_free (str16);

	ms_escher_set_inst (buf, marker,
			    ms_escher_get_inst (buf, marker) + 1);
}


void
ms_escher_clientanchor (GString *buf, SheetObjectAnchor const *anchor)
{
	guint8 tmp[26] = {
		0, 0, 0x10, 0xf0, 0xde, 0xad, 0xbe, 0xef,
		0, 0,
		0,0, 0,0,
		0,0, 0,0,
		0,0, 0,0,
		0,0, 0,0
	};
	guint8 *p = tmp + 10;
	GSF_LE_SET_GUINT32 (tmp + 4, sizeof (tmp) - 8);

	switch (anchor->mode) {
	case GNM_SO_ANCHOR_ONE_CELL:
		tmp[8] = 2;
		break;
	case GNM_SO_ANCHOR_ABSOLUTE:
		tmp[8] = 3;
		break;
	default:
		break;
	}
	GSF_LE_SET_GUINT16 (p +  0, anchor->cell_bound.start.col);
	GSF_LE_SET_GUINT16 (p +  2, (guint16)(anchor->offset[0]*1024. + .5));
	GSF_LE_SET_GUINT16 (p +  4, anchor->cell_bound.start.row);
	GSF_LE_SET_GUINT16 (p +  6, (guint16)(anchor->offset[1]*256. + .5));
	GSF_LE_SET_GUINT16 (p +  8, anchor->cell_bound.end.col);
	GSF_LE_SET_GUINT16 (p + 10, (guint16)(anchor->offset[2]*1024. + .5));
	GSF_LE_SET_GUINT16 (p + 12, anchor->cell_bound.end.row);
	GSF_LE_SET_GUINT16 (p + 14, (guint16)(anchor->offset[3]*256. + .5));
	g_string_append_len (buf, tmp, sizeof tmp);
}

void
ms_escher_clientdata (GString *buf)
{
	guint8 tmp[8] = { 0, 0, 0x11, 0xf0, 0xde, 0xad, 0xbe, 0xef };
	GSF_LE_SET_GUINT32 (tmp + 4, 0);
	g_string_append_len (buf, tmp, sizeof tmp);
}
