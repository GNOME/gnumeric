/**
 * ms-escher.c: MS Office drawing layer support
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (michael@nuclecu.unam.mx)
 *
 * (C) 1998, 1999 Jody Goldberg, Michael Meeks
 *
 * See S59FD6.HTM for an overview...
 **/

#include <stdio.h>

#include <config.h>
#include "ms-escher.h"
#include "escher-types.h"
#include "biff-types.h"
#include "ms-excel-read.h"
#include "ms-obj.h"

/* A storage accumulator for common state information */
typedef struct
{
	ExcelWorkbook  *wb;
	ExcelSheet     *sheet;
	BiffQuery      *q;

	guint32	segment_len;	/* number of bytes in current segment */

	/* Offsets from the logical 1st byte in the stream */
	guint32 start_offset;	/* 1st byte in current segment */
	guint32 end_offset;	/* 1st byte past end of current segment */
} MSEscherState;

typedef struct _MSEscherHeader
{
	/* Read from the data stream */
	guint	ver;
	guint	instance;
	guint16	fbt;
	guint32	len; /* Including the common header */

	guint32	offset;
	struct _MSEscherHeader * container;

	/* TODO : decide were to put these cause they dont belong here */
	gboolean anchor_set;
	int anchor[4];
	int blip_id;
} MSEscherHeader;
#define common_header_len 8

static void
ms_escher_blip_new (guint8 const *data, guint32 len, char const *repoid,
		    ExcelWorkbook * wb)
{
	EscherBlip *blip = g_new (EscherBlip, 1);
	guint8 *mem      = g_malloc (len);
	memcpy (mem, data, len);

	blip->reproid  = repoid;
#ifdef ENABLE_BONOBO
	blip->stream   = gnome_stream_mem_create (mem, len, TRUE);
#else
	blip->raw_data = mem;
#endif
	g_ptr_array_add (wb->blips, blip);
}

void
ms_escher_blip_destroy (EscherBlip *blip)
{
	blip->reproid = NULL;
#ifdef ENABLE_BONOBO
	if (blip->stream)
		gnome_object_destroy (GNOME_OBJECT (blip->stream));
	blip->stream  = NULL;
#else
	g_free (blip->raw_data);
	blip->raw_data = NULL;
#endif
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
ms_escher_get_data (MSEscherState * state,
		    gint offset,	/* bytes from logical start of the stream */
		    guint num_bytes,	/* how many bytes we want, incl prefix */
		    guint prefix,	/* number of bytes of header to skip */
		    gboolean * needs_free)
{
	guint8 * res;
	BiffQuery *q = state->q;

	g_return_val_if_fail (num_bytes >= prefix, NULL);
	offset += prefix;
	num_bytes -= prefix;

	g_return_val_if_fail (offset >= state->start_offset, NULL);

	/* find the 1st containing record */
	while (offset >= state->end_offset) {
		if (!ms_biff_query_next (q)) {
			printf ("EXCEL : unexpected end of stream;\n");
			return NULL;
		}

		g_return_val_if_fail (q->opcode == BIFF_MS_O_DRAWING ||
				      q->opcode == BIFF_MS_O_DRAWING_GROUP ||
				      q->opcode == BIFF_MS_O_DRAWING_SELECTION,
				      NULL);

		state->start_offset = state->end_offset;
		state->end_offset += q->length;
		state->segment_len = q->length;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1)
			printf ("Target is 0x%x bytes at 0x%x, current = 0x%x..0x%x;\n"
				"Adding biff-0x%x of length 0x%x;\n",
				num_bytes, offset,
				state->start_offset,
				state->end_offset,
				q->opcode, q->length);
#endif
	}

	res = q->data + offset - state->start_offset;
	if ((*needs_free = ((offset+num_bytes) > state->end_offset))) {
		guint8 * buffer = g_malloc (num_bytes);
		guint8 * tmp = buffer;

		/* Setup front stub */
		int len = q->length - (res - q->data);
		int counter = 0;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1)
			printf ("MERGE needed (%d+%d) >= %d;\n",
				offset, num_bytes, state->end_offset);
#endif

		do {
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1)
				printf ("record %d) add %d bytes;\n", ++counter, len);
#endif
			/* copy necessary portion of current record */
			memcpy (tmp, res, len);
			tmp += len;

			/* Get next record */
			if (!ms_biff_query_next (q)) {
				printf ("EXCEL : unexpected end of stream;\n");
				return NULL;
			}

			/* We should only see DRAW records now */
			g_return_val_if_fail (q->opcode == BIFF_MS_O_DRAWING ||
					      q->opcode == BIFF_MS_O_DRAWING_GROUP ||
					      q->opcode == BIFF_MS_O_DRAWING_SELECTION,
					      NULL);

			state->start_offset = state->end_offset;
			state->end_offset += q->length;
			state->segment_len = q->length;

			res = q->data;
			len = q->length;
		} while ((num_bytes - (tmp - buffer)) > len);

		/* Copy back stub */
		memcpy (tmp, res, num_bytes - (tmp-buffer));
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1)
			printf ("record %d) add %d bytes;\n", ++counter, num_bytes - (tmp-buffer));
#endif

		return buffer;
	}

	return res;
}

static gboolean
ms_escher_read_container (MSEscherState * state, MSEscherHeader * container,
			  gint offset);

/****************************************************************************/

static gboolean
ms_escher_read_CLSID (MSEscherState * state, MSEscherHeader * h)
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
ms_escher_read_ColorMRU (MSEscherState * state, MSEscherHeader * h)
{
	guint const num_Colours = h->instance;

	printf ("There are %d Colours in a record with remaining length %d;\n",
		num_Colours, (h->len - common_header_len));

	/* Colors in order from left to right.  */
	/* TODO : When we know how to parse a Colour record read these */
	return FALSE;
}

static gboolean
ms_escher_read_SplitMenuColors (MSEscherState * state, MSEscherHeader * h)
{
	gboolean needs_free;
	guint8 const * data;

	g_return_val_if_fail (h->instance == 4, TRUE);
	g_return_val_if_fail (h->len == 24, TRUE); /* header + 4*4 */
	
	if ((data = ms_escher_get_data (state, h->offset, 24,
					common_header_len, &needs_free))) {
		guint32 const top_level_fill = MS_OLE_GET_GUINT32(data + 0);
		guint32 const line	= MS_OLE_GET_GUINT32(data + 4);
		guint32 const shadow	= MS_OLE_GET_GUINT32(data + 8);
		guint32 const threeD	= MS_OLE_GET_GUINT32(data + 12);

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
			printf ("top_level_fill = 0x%x;\nline = 0x%x;\nshadow = 0x%x;\nthreeD = 0x%x;\n",
				top_level_fill, line, shadow, threeD);
#endif
	} else
		return TRUE;
	return FALSE;
}

static gboolean
ms_escher_read_BStoreContainer (MSEscherState * state, MSEscherHeader * h)
{
	return ms_escher_read_container (state, h, 0);
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
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
			printf ("written 0x%x bytes to '%s';\n",
				len, file_name->str);
#endif
	} else
		printf ("Can't open '%s';\n",
			file_name->str);
	g_string_free (file_name, 1);
}

static gboolean
ms_escher_read_BSE (MSEscherState * state, MSEscherHeader * h)
{
	/* read the header */
	gboolean needs_free;
	guint8 const * data =
		ms_escher_get_data (state, h->offset, 34,
				    common_header_len, &needs_free);
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

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
		printf ("Win type = %s;\n", bliptype_name (win_type));
		printf ("Mac type = %s;\n", bliptype_name (mac_type));
		printf ("Size = 0x%x(=%d) RefCount = 0x%x DelayOffset = 0x%x '%s';\n",
			size, size, ref_count, del_offset, name);

		switch (is_texture) {
		case 0: printf ("Default usage;\n"); break;
		case 1: printf ("Is texture;\n"); break;
		default:printf ("UNKNOWN USAGE : %d;\n", is_texture);
		};

		printf ("Checksum = 0x");
		for (i = 0; i < 16; ++i)
			printf ("%02x", checksum[i]);
		printf (";\n");
	}
#endif

	/* Very red herring I think */
	if (name_len != 0) {
		puts ("WARNING : Maybe a name ?");
		/* name = biff_get_text (data+36, name_len, &txt_byte_len); */
	}

	return ms_escher_read_container (state, h, 36);
}

static gboolean
ms_escher_read_Blip (MSEscherState * state, MSEscherHeader * h)
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

	case 0x46a : /* JPEG data, with 1 byte header */
	case 0x6e0 : /* PNG  data, with 1 byte header */
	{
		int const header = 17 + primary_uid_size + common_header_len;
		gboolean needs_free;
		guint8 const *data =
			ms_escher_get_data (state, h->offset, h->len,
					    header, &needs_free);
		const char *repoid = NULL;
		
		if (blip_instance == 0x6e0)
			repoid = "bonobo-object:image-x-png";
		else
			repoid = "embeddable:image-jpeg";
			
		ms_escher_blip_new (data, h->len - header,
				    repoid, state->wb);
		write_file ("unknown", data, h->len - header, h->fbt - Blip_START);
		break;
	}

	case 0x7a8 : /* DIB  data, with 1 byte header */
		break;

	default:
		g_warning ("Don't know what to do with this image %x\n", h->instance);
		return TRUE;
	};
	return FALSE;
}

static gboolean
ms_escher_read_RegroupItems (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}

static gboolean
ms_escher_read_ColorScheme (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}

static gboolean
ms_escher_read_SpContainer (MSEscherState * state, MSEscherHeader * h)
{
	return ms_escher_read_container (state, h, 0);
}

static gboolean
ms_escher_read_Spgr (MSEscherState * state, MSEscherHeader * h)
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

	g_return_val_if_fail (h->instance >= 0, TRUE);
	g_return_val_if_fail (h->instance <= 202, TRUE);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("%s (0x%x);\n", shape_names[h->instance],
			h->instance);
#endif
	return FALSE;
}

static gboolean
ms_escher_read_Sp (MSEscherState * state, MSEscherHeader * h)
{
	gboolean needs_free;
	guint8 const *data =
		ms_escher_get_data (state, h->offset, 8,
				    common_header_len, &needs_free);

	if (data != NULL) {
	    guint32 const spid  = MS_OLE_GET_GUINT32 (data+0);
	    guint32 const flags = MS_OLE_GET_GUINT32 (data+4);
#ifndef NO_DEBUG_EXCEL
	    if (ms_excel_read_debug > 0)
		    printf ("SPID %d, Type %d,%s%s%s%s%s%s%s%s%s%s%s;\n",
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
			    (flags&0x400) ? " TypeProp": ""
			   );
#endif
	} else
		return TRUE;

	if (needs_free)
		g_free ((guint8*)data);

	return FALSE;
}

static gboolean
ms_escher_read_Textbox (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}

static gboolean
ms_escher_read_Anchor (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}

static gboolean
ms_escher_read_ChildAnchor (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}

/* TODO : This is a guess that explains the workbooks we have, it seems credible because it
 * matches the pre-biff8 format. Find some confirmation.
 * WARNING : this is host specific and only works for Excel
 */
static gboolean
ms_escher_read_ClientAnchor (MSEscherState * state, MSEscherHeader * h)
{
	gboolean needs_free, res = TRUE;
	guint8 const *data;

	g_return_val_if_fail (!h->anchor_set, TRUE);

	/* FIXME : What is the the word at offset 0 ?? Maybe a sheet index ? */
	data = ms_escher_get_data (state, h->offset, 16,
				   common_header_len+2, &needs_free);
	if (data) {
		h->anchor_set = TRUE;
		res = ms_parse_object_anchor (h->anchor,
					      state->sheet->gnum_sheet, data);
		if (needs_free)
			g_free ((guint8 *)data);
	}

	return res;
}

static gboolean
ms_escher_read_OleObject (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_DeletedPspl (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_SolverContainer (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ConnectorRule (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_AlignRule (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ArcRule (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_ClientRule (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_CalloutRule (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_Selection (MSEscherState * state, MSEscherHeader * h)
{
	return FALSE;
}
static gboolean
ms_escher_read_Dg (MSEscherState * state, MSEscherHeader * h)
{
#if 0
	guint8 const * data = h->data + common_header_len;
	guint32 num_shapes = MS_OLE_GET_GUINT32(data);
	/* spid_cur = last SPID given to an SP in this DG :-)  */
	guint32 spid_cur   = MS_OLE_GET_GUINT32(data+4);
	guint32 drawing_id = h->instance;

	/* This drawing has these num_shapes shapes, with a pointer to the last
	 * SPID given to it */
#endif
	return FALSE;
}

static gboolean
ms_escher_read_Dgg (MSEscherState * state, MSEscherHeader * h)
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
	for (lp = 0; lp < fd.num_id_clust; lp++) {
		ID_CLUST cl;
		cl.DG_owning_spids   = MS_OLE_GET_GUINT32(data+0);
		cl.spids_used_so_far = MS_OLE_GET_GUINT32(data+4);
		g_array_append_val (fd.id_clusts, cl);
	}
#endif
	return FALSE;
}

typedef struct
{
} EscherOption;

typedef enum
{
    shape_Lines = 0,        /*  straight line segments */
    shape_LinesClosed = 1,  /*  closed polygonal shape */
    shape_Curves = 2,       /*  Bezier curve segments */
    shape_CurvesClosed = 3, /*  A closed shape with curved edges */
    shape_Complex = 4,      /*  pSegmentInfo must be non-empty */
} ShapePath;

typedef enum
{
    wrap_Square = 0,
    wrap_ByPoints = 1,
    wrap_None = 2,
    wrap_TopBottom = 3,
    wrap_Through = 4,
} WrapMode;

typedef enum
{
    bw_Color = 0,          /*  only used for predefined shades */
    bw_Automatic = 1,      /*  depends on object type */
    bw_GrayScale = 2,      /*  shades of gray only */
    bw_LightGrayScale = 3, /*  shades of light gray only */
    bw_InverseGray = 4,    /*  dark gray mapped to light gray, etc. */
    bw_GrayOutline = 5,    /*  pure gray and white */
    bw_BlackTextLine = 6,  /*  black text and lines, all else grayscale */
    bw_HighContrast = 7,   /*  pure black and white mode (no grays) */
    bw_Black = 7,          /*  solid black */
    bw_White = 8,          /*  solid white */
    bw_DontShow = 9,       /*  object not drawn */
} BlackWhiteMode;

typedef enum
{
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

typedef enum
{
    rotate_0 = 0,	/*  Right */
    rotate_90 = 1,	/*  Down */
    rotate_180 = 2,	/*  Left */
    rotate_270 = 3	/*  Up */
} RotationType;

typedef enum
{
    connect_Straight = 0,
    connect_Bent = 1,
    connect_Curved = 2,
    connect_None = 3
} ConnectStyle;

typedef enum
{
    flow_HorzN = 0,	/*  Horizontal non-@ */
    flow_TtoBA = 1,	/*  Top to Bottom @-font */
    flow_BtoT = 2,	/*  Bottom to Top non-@ */
    flow_TtoBN = 3,	/*  Top to Bottom non-@ */
    flow_HorzA = 4,	/*  Horizontal @-font */
    flow_VertN = 5,	/*  Vertical, non-@ */
} TextFlow;

typedef enum
{
    textdir_LtoR = 0,
    textdir_RtoL = 1,
    textdir_Context = 2,	/*  depends on context */
} TextDirection;

typedef enum
{
    callout_RightAngle = 1,
    callout_OneSegment = 2,
    callout_TwoSegment = 3,
    callout_ThreeSegment = 4,
} CalloutType;

typedef enum
{
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
    callout_drop_Specified = 3,
} CalloutDrop;

/*  Alignment - WordArt only */
typedef enum
{
    align_TextStretch,      /* Stretch each line of text to fit width. */
    align_TextCenter,       /* Center text on width. */
    align_TextLeft,         /* Left justify. */
    align_TextRight,        /* Right justify. */
    align_TextLetterJust,   /* Spread letters out to fit width. */
    align_TextWordJust,     /* Spread words out to fit width. */
    align_TextInvalid       /* Invalid */
} Alignment;

typedef enum
{
    render_FullRender,
    render_Wireframe,
    render_BoundingCube,
} RenderMode;

/*  MSOXFORMTYPE */
typedef enum
{
    msoxformAbsolute,   /*  Apply transform in absolute space centered on shape */
    msoxformShape,      /*  Apply transform to shape geometry */
    msoxformDrawing     /*  Apply transform in drawing space */
} MSOXFORMTYPE;

/*  MSOSHADOWTYPE */
typedef enum
{
    msoshadowOffset,    /*  N pixel offset shadow */
    msoshadowDouble,    /*  Use second offset too */
    msoshadowRich,      /*  Rich perspective shadow (cast relative to shape) */
    msoshadowShape,     /*  Rich perspective shadow (cast in shape space) */
    msoshadowDrawing,   /*  Perspective shadow cast in drawing space */
    msoshadowEmbossOrEngrave,
} MSOSHADOWTYPE;

/*  LengthMeasure - the type of a (length) measurement */
typedef enum
{
    msodztypeMin          = 0,
    msodztypeDefault      = 0,  /*  Default size, ignore the values */
    msodztypeA            = 1,  /*  Values are in EMUs */
    msodztypeV            = 2,  /*  Values are in pixels */
    msodztypeShape        = 3,  /*  Values are 16.16 fractions of shape size */
    msodztypeFixedAspect  = 4,  /*  Aspect ratio is fixed */
    msodztypeAFixed       = 5,  /*  EMUs, fixed aspect ratio */
    msodztypeVFixed       = 6,  /*  Pixels, fixed aspect ratio */
    msodztypeShapeFixed   = 7,  /*  Proportion of shape, fixed aspect ratio */
    msodztypeFixedAspectEnlarge
	= 8,  /*  Aspect ratio is fixed, favor larger size */
    msodztypeAFixedBig    = 9,  /*  EMUs, fixed aspect ratio */
    msodztypeVFixedBig    = 10, /*  Pixels, fixed aspect ratio */
    msodztypeShapeFixedBig= 11, /*  Proportion of shape, fixed aspect ratio */
    msodztypeMax         = 11
} LengthMeasure;

typedef enum
{
    fill_Solid = 0,
    fill_Pattern = 1,	/*  bitmap */
    fill_Texture = 2,	/*  pattern with private Colour map) */
    fill_Picture = 3,	/*  Center picture on the shape */
    fill_Shade = 4,	/*  Shade from start to end points */
    fill_ShadeCenter =5,/*  Shade from bounding rectangle to end point */
    fill_ShadeShape = 6,/*  Shade from shape outline to end point */
    fill_ShadeScale = 7,/*  Like fill_Shade, but fillAngle is also scaled by
			    the aspect ratio of the shape. If shape is square,
			    it is the same as fill_Shade. */
    fill_ShadeTitle = 8,/*  shade to title  ?? what is this for */
    fill_Background = 9	/*  Use background fill color/pattern */
} FillType;

/*  Colours in a shaded fill. */
typedef enum
{
    msoshadeNone  = 0,        /*  Interpolate without correction between RGBs */
    msoshadeGamma = 1,        /*  Apply gamma correction to colors */
    msoshadeSigma = 2,        /*  Apply a sigma transfer function to position */
    msoshadeBand  = 4,        /*  Add a flat band at the start of the shade */
    msoshadeOneColor = 8,     /*  This is a one color shade */

    /* A parameter for the band or sigma function can be stored in the top
       16 bits of the value - this is a proportion of *each* band of the
       shade to make flat (or the approximate equal value for a sigma
       function).  NOTE: the parameter is not used for the sigma function,
       instead a built in value is used.  This value should not be changed
       from the default! */
    msoshadeParameterShift = 16,
    msoshadeParameterMask  = 0xffff0000,

    msoshadeDefault = (msoshadeGamma|msoshadeSigma|
		       (16384<<msoshadeParameterShift))
} ShadeType;

/*    LineStyle - compound line style */
typedef enum
{
    msolineSimple,            /*  Single line (of width lineWidth) */
    msolineDouble,            /*  Double lines of equal width */
    msolineThickThin,         /*  Double lines, one thick, one thin */
    msolineThinThick,         /*  Double lines, reverse order */
    msolineTriple             /*  Three lines, thin, thick, thin */
} LineStyle;

typedef enum
{
    linefill_SolidType,         /*  Fill with a solid color */
    linefill_Pattern,           /*  Fill with a pattern (bitmap) */
    linefill_Texture,           /*  A texture (pattern with its own color map) */
    linefill_Picture            /*  Center a picture in the shape */
} LineFill;

/*  DashedLineStyle - dashed line style */
typedef enum
{
    msolineSolid,              /*  Solid (continuous) pen */
    msolineDashSys,            /*  PS_DASH system   dash style */
    msolineDotSys,             /*  PS_DOT system   dash style */
    msolineDashDotSys,         /*  PS_DASHDOT system dash style */
    msolineDashDotDotSys,      /*  PS_DASHDOTDOT system dash style */
    msolineDotGEL,             /*  square dot style */
    msolineDashGEL,            /*  dash style */
    msolineLongDashGEL,        /*  long dash style */
    msolineDashDotGEL,         /*  dash short dash */
    msolineLongDashDotGEL,     /*  long dash short dash */
    msolineLongDashDotDotGEL   /*  long dash short dash short dash */
} DashedLineStyle;

typedef enum
{
    line_end_NoEnd,
    line_end_ArrowEnd,
    line_end_ArrowStealthEnd,
    line_end_ArrowDiamondEnd,
    line_end_ArrowOvalEnd,
    line_end_ArrowOpenEnd,
} LineEndStyle;

typedef enum
{
    arrow_width_Narrow = 0,
    arrow_width_Medium = 1,
    arrow_width_Wide = 2
} ArrowWidth;

typedef enum
{
    arrow_len_Short = 0,
    arrow_len_Medium = 1,
    arrow_len_Long = 2
} ArrowLength;

/*    MSOLINEJOIN - line join style. */
typedef enum
{
    msolineJoinBevel,     /*  Join edges by a straight line */
    msolineJoinMiter,     /*  Extend edges until they join */
    msolineJoinRound      /*  Draw an arc between the two edges */
} MSOLINEJOIN;

/*    MSOLINECAP - line cap style (applies to ends of dash segments too). */
typedef enum
{
    msolineEndCapRound,   /*  Rounded ends - the default */
    msolineEndCapSquare,  /*  Square protrudes by half line width */
    msolineEndCapFlat     /*  Line ends at end point */
} MSOLINECAP;

static gboolean
ms_escher_read_OPT (MSEscherState * state, MSEscherHeader * h)
{
	int const num_properties = h->instance;
	gboolean needs_free;
	guint8 const * const data =
		ms_escher_get_data (state, h->offset, h->len,
				    common_header_len, &needs_free);
	guint8 const *fopte = data;
	guint8 const *extra = fopte + 6*num_properties;
	guint prev_pid = 0; /* A debug tool */
	char const * name;
	int i;

	/* lets be really careful */
	g_return_val_if_fail (6*num_properties + common_header_len <= h->len, TRUE);

	for (i = 0; i < num_properties; ++i, fopte += 6) {
		guint16 const tmp = MS_OLE_GET_GUINT32(fopte);
		guint const pid = tmp & 0x3fff;
		gboolean const is_blip = (tmp & 0x4000) != 0;
		gboolean const is_complex = (tmp & 0x8000) != 0;
		guint32 const val = MS_OLE_GET_GUINT32(fopte+2);

		/* container is sorted by pid. Use this as sanity test */
		if (prev_pid >= pid) {
			printf ("Pids not monotonic %d >= %d;\n", prev_pid, pid);
			if (needs_free)
				g_free ((guint8 *)data);
			return TRUE;
		}
		prev_pid = pid;

		switch (pid) {
		/* 0 : fixed point: 16.16 degrees */
		case 4 : name = "long rotation"; break;

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
		/* FALSE */
		case 133 : name = "WrapMode wrap_test_at_margin"; break;
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
		/* TRUE : TRUE if single click selects text, FALSE if two clicks */
		case 187 : name = "bool fSelectText"; break;
		/* FALSE : use host's margin calculations */
		case 188 : name = "bool fAutoTextMargin"; break;
	        /* FALSE : Rotate text with shape */
		case 189 : name = "bool fRotateText"; break;
		/* FALSE : Size shape to fit text size */
		case 190 : name = "bool fFitShapeToText"; break;
		/* FALSE : Size text to fit shape size */
		case 191 : name = "bool fFitTextToShape"; break;

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
		/* FALSE : Reverse row order */
		case 240 : name = "bool gtextFReverseRows"; break;
		/* FALSE : Has text effect */
		case 241 : name = "bool fGtext"; break;
		/* FALSE : Rotate characters */
		case 242 : name = "bool gtextFVertical"; break;
		/* FALSE : Kern characters */
		case 243 : name = "bool gtextFKern"; break;
		/* FALSE : Tightening or tracking */
		case 244 : name = "bool gtextFTight"; break;
		/* FALSE : Stretch to fit shape */
		case 245 : name = "bool gtextFStretch"; break;
		/* FALSE : Char bounding box */
		case 246 : name = "bool gtextFShrinkFit"; break;
		/* FALSE : Scale text-on-path */
		case 247 : name = "bool gtextFBestFit"; break;
		/* FALSE : Stretch char height */
		case 248 : name = "bool gtextFNormalize"; break;
		/* FALSE : Do not measure along path */
		case 249 : name = "bool gtextFDxMeasure"; break;
		/* FALSE : Bold font */
		case 250 : name = "bool gtextFBold"; break;
		/* FALSE : Italic font */
		case 251 : name = "bool gtextFItalic"; break;
		/* FALSE : Underline font */
		case 252 : name = "bool gtextFUnderline"; break;
		/* FALSE : Shadow font */
		case 253 : name = "bool gtextFShadow"; break;
		/* FALSE : Small caps font */
		case 254 : name = "bool gtextFSmallcaps"; break;
		/* FALSE : Strike through font */
		case 255 : name = "bool gtextFStrikethrough"; break;

		/* 0 : 16.16 fraction times total image width or height, as appropriate. */
		case 256 : name = "fixed16_16 cropFromTop"; break;
		case 257 : name = "fixed16_16 cropFromBottom"; break;
		case 258 : name = "fixed16_16 cropFromLeft"; break;
		case 259 : name = "fixed16_16 cropFromRight"; break;

		/* NULL : Blip to display */
		case 260 : name = "Blip * pib";
			   h->blip_id = (int)val - 1;
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

		/* FALSE : Do not hit test the picture */
		case 316 : name = "bool fNoHitTestPicture"; break;

		/* FALSE : grayscale display */
		case 317 : name = "bool pictureGray"; break;

		/* FALSE : bi-level display */
		case 318 : name = "bool pictureBiLevel"; break;

		/* FALSE : Server is active (OLE objects only) */
		case 319 : name = "bool pictureActive"; break;

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
		/* TRUE : Shadow may be set */
		case 378 : name = "bool fShadowOK"; break;
		/* TRUE : 3D may be set */
		case 379 : name = "bool f3DOK"; break;
		/* TRUE : Line style may be set */
		case 380 : name = "bool fLineOK"; break;
		/* FALSE : Text effect (WordArt) supported */
		case 381 : name = "bool fGtextOK"; break;
		/* FALSE :  */
		case 382 : name = "bool fFillShadeShapeOK"; break;
		/* TRUE : OK to fill the shape through the UI or VBA? */
		case 383 : name = "bool fFillOK"; break;

		/* Solid : Type of fill */
		case 384 : name = "FillType fillType"; break;
		/* white : Foreground color */
		case 385 : name = "Colour fillColor"; break;
		/* 1<<16 : Fixed 16.16 */
		case 386 : name = "long fillOpacity"; break;
		/* white : Background color */
		case 387 : name = "Colour fillBackColor"; break;
		/* 1<<16 : Shades only */
		case 388 : name = "long fillBackOpacity"; break;
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
		case 395 : name = "long fillAngle"; break;
		/* 0 : Linear shaded fill focus percent */
		case 396 : name = "long fillFocus"; break;
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
		/* Default :  */
		case 405 : name = "LengthMeasure fillDztype"; break;
		/* 0 : Special shades */
		case 406 : name = "long fillShadePreset"; break;
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
		/* Default : Type of shading, if a shaded (gradient) fill. */
		case 412 : name = "ShadeType fillShadeType"; break;
		/* TRUE : Is shape filled? */
		case 443 : name = "bool fFilled"; break;
		/* TRUE : Should we hit test fill?  */
		case 444 : name = "bool fHitTestFill"; break;
		/* TRUE : Register pattern on shape */
		case 445 : name = "bool fillShape"; break;
		/* FALSE : Use the large rect? */
		case 446 : name = "bool fillUseRect"; break;
		/* FALSE : Hit test a shape as though filled */
		case 447 : name = "bool fNoFillHitTest"; break;

		/* black : Color of line */
		case 448 : name = "Colour lineColor"; break;
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
		/* Default : How to interpret fillWidth/Height numbers. */
		case 458 : name = "LengthMeasure lineFillDztype"; break;
		/* 9525 : A units; 1pt == 12700 EMUs */
		case 459 : name = "long lineWidth"; break;
		/* 8<<16 : ratio (16.16) of width */
		case 460 : name = "long lineMiterLimit"; break;
		/* Simple : Draw parallel lines? */
		case 461 : name = "LineStyle lineStyle"; break;
		/* Solid : Can be overridden by: */
		case 462 : name = "DashedLineStyle lineDashing"; break;
		/* NULL : As Win32 ExtCreatePen */
		case 463 : name = "IMsoArray lineDashStyle"; break;
		/* NoEnd : Arrow at start */
		case 464 : name = "LineEndStyle lineStartArrowhead"; break;
		/* NoEnd : Arrow at end */
		case 465 : name = "LineEndStyle lineEndArrowhead"; break;
		/* MediumWidthArrow : Arrow at start */
		case 466 : name = "ArrowWidth lineStartArrowWidth"; break;
		/* MediumLenArrow : Arrow at end */
		case 467 : name = "ArrowLength lineStartArrowLength"; break;
		/* MediumWidthArrow : Arrow at start */
		case 468 : name = "ArrowWidth lineEndArrowWidth"; break;
		/* MediumLenArrow : Arrow at end */
		case 469 : name = "ArrowLength lineEndArrowLength"; break;
		/* JoinRound : How to join lines */
		case 470 : name = "MSOLINEJOIN lineJoinStyle"; break;
		/* EndCapFlat : How to end lines */
		case 471 : name = "MSOLINECAP lineEndCapStyle"; break;
		/* FALSE : Allow arrowheads if prop. is set */
		case 507 : name = "bool fArrowheadsOK"; break;
		/* TRUE : Any line? */
		case 508 : name = "bool fLine"; break;
		/* TRUE : Should we hit test lines?  */
		case 509 : name = "bool fHitTestLine"; break;
		/* TRUE : Register pattern on shape */
		case 510 : name = "bool lineFillShape"; break;
		/* FALSE : Draw a dashed line if no line */
		case 511 : name = "bool fNoLineDrawDash"; break;

		/* Offset : Type of effect */
		case 512 : name = "MSOSHADOWTYPE shadowType"; break;
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
		/* FALSE : Any shadow? */
		case 574 : name = "bool fShadow"; break;
		/* FALSE : Excel5-style shadow */
		case 575 : name = "bool fshadowObscured"; break;

		/* Shape : Where transform applies */
		case 576 : name = "MSOXFORMTYPE perspectiveType"; break;
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
		/* FALSE : Does this shape have a 3D effect? */
		case 700 : name = "bool f3D"; break;
		/* 0 : Use metallic specularity? */
		case 701 : name = "bool fc3DMetallic"; break;
		/* FALSE :  */
		case 702 : name = "bool fc3DUseExtrusionColor"; break;
		/* TRUE :  */
		case 703 : name = "bool fc3DLightFace"; break;

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
		/* TRUE :  */
		case 763 : name = "bool fc3DConstrainRotation"; break;
		/* FALSE :  */
		case 764 : name = "bool fc3DRotationCenterAuto"; break;
		/* 1 : Parallel projection? */
		case 765 : name = "bool fc3DParallel"; break;
		/* 1 : Is key lighting harsh? */
		case 766 : name = "bool fc3DKeyHarsh"; break;
		/* 0 : Is fill lighting harsh?` */
		case 767 : name = "bool fc3DFillHarsh"; break;
	   
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
		/* FALSE : For OLE objects, whether the object is in icon form */
		case 826 : name = "bool fOleIcon"; break;
		/* FALSE : For UI only. Prefer relative resizing.  */
		case 827 : name = "bool fPreferRelativeResize"; break;
		/* FALSE : Lock the shape type (don't allow Change Shape) */
		case 828 : name = "bool fLockShapeType"; break;
		/* FALSE :  */
		case 830 : name = "bool fDeleteAttachedObject"; break;
		/* FALSE : If TRUE, this is the background shape. */
		case 831 : name = "bool fBackground"; break;

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

		/* FALSE : Is the shape a callout? */
		case 889 : name = "bool fCallout"; break;

		/* FALSE : does callout have accent bar */
		case 890 : name = "bool fCalloutAccentBar"; break;

		/* TRUE : does callout have a text border */
		case 891 : name = "bool fCalloutTextBorder"; break;

		/* FALSE :  */
		case 892 : name = "bool fCalloutMinusX"; break;

		/* FALSE :  */
		case 893 : name = "bool fCalloutMinusY"; break;

		/* FALSE : If true, then we occasionally invert the drop distance */
		case 894 : name = "bool fCalloutDropAuto"; break;

		/* FALSE : if true, we look at dxyCalloutLengthSpecified */
		case 895 : name = "bool fCalloutLengthSpecified"; break;

		/* NULL : Shape Name (present only if explicitly set) */
		case 896 : name = "wchar* wzName"; break;

		/* NULL : alternate text */
		case 897 : name = "wchar* wzDescription"; break;

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

		/* FALSE : Has the wrap polygon been edited? */
		case 953 : name = "bool fEditedWrap"; break;

		/* FALSE : Word-only (shape is behind text) */
		case 954 : name = "bool fBehindDocument"; break;

		/* FALSE : Notify client on a double click */
		case 955 : name = "bool fOnDblClickNotify"; break;

		/* FALSE : A button shape (i.e., clicking performs an action).
		 * Set for shapes with attached hyperlinks or macros. */
		case 956 : name = "bool fIsButton"; break;

		/* FALSE : 1D adjustment */
		case 957 : name = "bool fOneD"; break;

		/* FALSE : Do not display */
		case 958 : name = "bool fHidden"; break;

		/* TRUE : Print this shape */
		case 959 : name = "bool fPrint"; break;

		default : name = "";
		};

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
			printf ("%s %d = 0x%x (=%d) %s%s;\n", name, pid, val, val,
				is_blip ? " is blip" : "",
				is_complex ? " is complex" : "");
#endif
		if (is_complex) {
			extra += val;

			/* check for over run */
			g_return_val_if_fail (extra - data + common_header_len <= h->len, TRUE);
		}
	}
	if (needs_free)
		g_free ((guint8 *)data);

	return FALSE;
}

static gboolean
ms_escher_read_SpgrContainer (MSEscherState * state, MSEscherHeader * h)
{
	return ms_escher_read_container (state, h, 0);
}
static gboolean
ms_escher_read_DgContainer (MSEscherState * state, MSEscherHeader * h)
{
	return ms_escher_read_container (state, h, 0);
}
static gboolean
ms_escher_read_DggContainer (MSEscherState * state, MSEscherHeader * h)
{
	return ms_escher_read_container (state, h, 0);
}
static gboolean
ms_escher_read_ClientTextbox (MSEscherState * state, MSEscherHeader * h)
{
	guint16 opcode;

	g_return_val_if_fail (h->len == common_header_len, TRUE);
	g_return_val_if_fail (h->offset + h->len == state->end_offset, TRUE);

	/* Read the TXO, be VERY careful until we are sure of the state */
	g_return_val_if_fail (ms_biff_query_peek_next (state->q, &opcode), TRUE);
	g_return_val_if_fail (opcode == BIFF_TXO, TRUE);
	g_return_val_if_fail (ms_biff_query_next (state->q), TRUE);

	ms_read_TXO (state->q, state->wb);
	return FALSE;
}

static gboolean
ms_escher_read_ClientData (MSEscherState *state, MSEscherHeader *h)
{
	int     i;
	guint16 opcode;
	MSObj  *obj;
	ExcelSheet *sheet;

	g_return_val_if_fail (state->sheet != NULL, TRUE);
	g_return_val_if_fail (h->len == common_header_len, TRUE);
	g_return_val_if_fail (h->offset + h->len == state->end_offset, TRUE);

	/* Read the OBJ, be VERY careful until we are sure of the state */
	g_return_val_if_fail (ms_biff_query_peek_next (state->q, &opcode), TRUE);
	g_return_val_if_fail (opcode == BIFF_OBJ, TRUE);
	g_return_val_if_fail (ms_biff_query_next (state->q), TRUE);

	sheet = state->sheet;
	obj = ms_read_OBJ (state->q, state->wb, sheet->gnum_sheet);

	if (obj == NULL)
		return FALSE;

	/* We should have an anchor set by now */
	g_return_val_if_fail (h->anchor_set, FALSE);
	g_return_val_if_fail (!obj->anchor_set, FALSE);

	for (i = 4; --i >= 0 ; )
		obj->anchor[i] = h->anchor[i];
	obj->anchor_set = TRUE;

	switch (obj->gnumeric_type) {
	case SHEET_OBJECT_GRAPHIC : /* If this was a picture */
		obj->v.picture.blip_id = h->blip_id;
		break;
	default:
		break;
	};

	sheet->obj_queue = g_list_append (sheet->obj_queue,
					  obj);
	return FALSE;
}

/****************************************************************************/

/* NOTE : this does not init h->container or h->offset */
static void
ms_escher_init_header(MSEscherHeader * h)
{
	h->ver = h->instance = h->fbt = h->len = 0;
	h->anchor_set = FALSE;
	h->blip_id = -1;
}

static gboolean
ms_escher_read_container (MSEscherState * state, MSEscherHeader * container,
			  gint const prefix)
{
	MSEscherHeader	h;
	ms_escher_init_header(&h);
	h.container = container;

	/* Skip the common header */
	h.offset = container->offset + prefix + common_header_len;

	do {
		guint16 tmp;
		char const * fbt_name = NULL;
		gboolean (*handler)(MSEscherState * state,
				    MSEscherHeader * container) = NULL;
		gboolean needs_free;

		guint8 const * data =
			ms_escher_get_data (state, h.offset, common_header_len,
					    0, &needs_free);

		if (data == NULL)
			return TRUE;

		tmp	= MS_OLE_GET_GUINT16(data+0);
		h.fbt	= MS_OLE_GET_GUINT16(data+2);

		/* Include the length of this header in the record size */
		h.len	= MS_OLE_GET_GUINT32(data+4) + common_header_len;
		h.ver      = tmp & 0x0f;
		h.instance = (tmp>>4) & 0xfff;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0) {
			printf ("length 0x%x(=%d), ver 0x%x, instance 0x%x, offset = 0x%x;\n",
				h.len, h.len, h.ver, h.instance, h.offset);
		}
#endif
		/*
		 * Lets double check that the data we just read makes sense.
		 * If problems arise in the next tests it probably indicates that
		 * the PRECEDING record length was invalid.  Check that it included the header */
		if ((h.fbt & (~0x1ff)) != 0xf000) {
			printf ("WARNING EXCEL : Invalid fbt = %x\n", h.fbt);
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
		default : fbt_name = NULL;
		};
#undef EshRecord

		if (Blip_START <= h.fbt && h.fbt <= Blip_END) {
			ms_escher_read_Blip (state, &h);
		} else if (fbt_name != NULL) {
			gboolean res;

			/* Not really needed */
			g_return_val_if_fail (handler != NULL, TRUE);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
				printf ("{ /* %s */\n", fbt_name);
#endif
			res = (*handler)(state, &h);

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
				printf ("}; /* %s */\n", fbt_name);
#endif
			if (res) {
				printf ("ERROR;\n");
				return TRUE;
			}

		} else
			printf ("WARNING EXCEL : Invalid fbt = %x\n", h.fbt);

		h.offset += h.len;
	} while (h.offset < (container->offset + container->len));
	return FALSE;
}

/*
 * ms_escher_parse:
 * @q:     Biff context.
 * @wb:    required workbook argument
 * @sheet: optional sheet argument
 * 
 *   This function parses an escher stream, and stores relevant data in the
 * workbook. 
 */
void
ms_escher_parse (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *sheet)
{
	MSEscherState state;
	MSEscherHeader fake_header;
	char const *drawing_record_name = "Unknown";

	g_return_if_fail (q != NULL);
	g_return_if_fail (wb != NULL);
	if (q->opcode != BIFF_MS_O_DRAWING)
		drawing_record_name = "Drawing";
	else if (q->opcode != BIFF_MS_O_DRAWING_GROUP)
		drawing_record_name = "Drawing Group";
	else if (q->opcode != BIFF_MS_O_DRAWING_SELECTION)
		drawing_record_name = "Drawing Selection";
	else {
		g_warning ("EXCEL : unexpected biff type %x\n", q->opcode);
		return;
	}

	state.wb    = wb;
	state.sheet = sheet;
	state.q     = q;
	state.segment_len  = q->length;
	state.start_offset = 0;
	state.end_offset   = q->length;

	ms_escher_init_header(&fake_header);
	fake_header.container = NULL;
	fake_header.offset = 0;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("{  /* Escher '%s'*/\n", drawing_record_name);
#endif

	ms_escher_read_container (&state, &fake_header,
				  -common_header_len);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("}; /* Escher '%s'*/\n", drawing_record_name);
#endif
}
