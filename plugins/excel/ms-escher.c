/**
 * ms-escher.c: MS Office drawing layer support
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (michael@nuclecu.unam.mx)
 *
 * See S59FD6.HTM for an overview...
 **/

#include <stdio.h>

#include "config.h"
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
		if (ms_excel_read_debug > 0)
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
		if (ms_excel_read_debug > 0)
			printf ("MERGE needed (%d+%d) >= %d;\n",
				offset, num_bytes, state->end_offset);
#endif

		do {
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
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
		if (ms_excel_read_debug > 0)
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
	guint const num_colours = h->instance;

	printf ("There are %d colours in a record with remaining length %d;\n",
		num_colours, (h->len - common_header_len));

	/* Colors in order from left to right.  */
	/* TODO : When we know how to parse a colour record read these */
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
	char const * name;
	switch (h->instance) {
	case 0: name = "Not a primitive"; break;
	case 1: name = "Rectangle"; break;
	case 2: name = "RoundRectangle"; break;
	case 3: name = "Ellipse"; break;
	case 4: name = "Diamond"; break;
	case 5: name = "IsocelesTriangle"; break;
	case 6: name = "RightTriangle"; break;
	case 7: name = "Parallelogram"; break;
	case 8: name = "Trapezoid"; break;
	case 9: name = "Hexagon"; break;
	case 10: name = "Octagon"; break;
	case 11: name = "Plus"; break;
	case 12: name = "Star"; break;
	case 13: name = "Arrow"; break;
	case 14: name = "ThickArrow"; break;
	case 15: name = "HomePlate"; break;
	case 16: name = "Cube"; break;
	case 17: name = "Balloon"; break;
	case 18: name = "Seal"; break;
	case 19: name = "Arc"; break;
	case 20: name = "Line"; break;
	case 21: name = "Plaque"; break;
	case 22: name = "Can"; break;
	case 23: name = "Donut"; break;
	case 24: name = "TextSimple"; break;
	case 25: name = "TextOctagon"; break;
	case 26: name = "TextHexagon"; break;
	case 27: name = "TextCurve"; break;
	case 28: name = "TextWave"; break;
	case 29: name = "TextRing"; break;
	case 30: name = "TextOnCurve"; break;
	case 31: name = "TextOnRing"; break;
	case 32: name = "StraightConnector1"; break;
	case 33: name = "BentConnector2"; break;
	case 34: name = "BentConnector3"; break;
	case 35: name = "BentConnector4"; break;
	case 36: name = "BentConnector5"; break;
	case 37: name = "CurvedConnector2"; break;
	case 38: name = "CurvedConnector3"; break;
	case 39: name = "CurvedConnector4"; break;
	case 40: name = "CurvedConnector5"; break;
	case 41: name = "Callout1"; break;
	case 42: name = "Callout2"; break;
	case 43: name = "Callout3"; break;
	case 44: name = "AccentCallout1"; break;
	case 45: name = "AccentCallout2"; break;
	case 46: name = "AccentCallout3"; break;
	case 47: name = "BorderCallout1"; break;
	case 48: name = "BorderCallout2"; break;
	case 49: name = "BorderCallout3"; break;
	case 50: name = "AccentBorderCallout1"; break;
	case 51: name = "AccentBorderCallout2"; break;
	case 52: name = "AccentBorderCallout3"; break;
	case 53: name = "Ribbon"; break;
	case 54: name = "Ribbon2"; break;
	case 55: name = "Chevron"; break;
	case 56: name = "Pentagon"; break;
	case 57: name = "NoSmoking"; break;
	case 58: name = "Seal8"; break;
	case 59: name = "Seal16"; break;
	case 60: name = "Seal32"; break;
	case 61: name = "WedgeRectCallout"; break;
	case 62: name = "WedgeRRectCallout"; break;
	case 63: name = "WedgeEllipseCallout"; break;
	case 64: name = "Wave"; break;
	case 65: name = "FoldedCorner"; break;
	case 66: name = "LeftArrow"; break;
	case 67: name = "DownArrow"; break;
	case 68: name = "UpArrow"; break;
	case 69: name = "LeftRightArrow"; break;
	case 70: name = "UpDownArrow"; break;
	case 71: name = "IrregularSeal1"; break;
	case 72: name = "IrregularSeal2"; break;
	case 73: name = "LightningBolt"; break;
	case 74: name = "Heart"; break;
	case 75: name = "PictureFrame"; break;
	case 76: name = "QuadArrow"; break;
	case 77: name = "LeftArrowCallout"; break;
	case 78: name = "RightArrowCallout"; break;
	case 79: name = "UpArrowCallout"; break;
	case 80: name = "DownArrowCallout"; break;
	case 81: name = "LeftRightArrowCallout"; break;
	case 82: name = "UpDownArrowCallout"; break;
	case 83: name = "QuadArrowCallout"; break;
	case 84: name = "Bevel"; break;
	case 85: name = "LeftBracket"; break;
	case 86: name = "RightBracket"; break;
	case 87: name = "LeftBrace"; break;
	case 88: name = "RightBrace"; break;
	case 89: name = "LeftUpArrow"; break;
	case 90: name = "BentUpArrow"; break;
	case 91: name = "BentArrow"; break;
	case 92: name = "Seal24"; break;
	case 93: name = "StripedRightArrow"; break;
	case 94: name = "NotchedRightArrow"; break;
	case 95: name = "BlockArc"; break;
	case 96: name = "SmileyFace"; break;
	case 97: name = "VerticalScroll"; break;
	case 98: name = "HorizontalScroll"; break;
	case 99: name = "CircularArrow"; break;
	case 100: name = "NotchedCircularArrow"; break;
	case 101: name = "UturnArrow"; break;
	case 102: name = "CurvedRightArrow"; break;
	case 103: name = "CurvedLeftArrow"; break;
	case 104: name = "CurvedUpArrow"; break;
	case 105: name = "CurvedDownArrow"; break;
	case 106: name = "CloudCallout"; break;
	case 107: name = "EllipseRibbon"; break;
	case 108: name = "EllipseRibbon2"; break;
	case 109: name = "FlowChartProcess"; break;
	case 110: name = "FlowChartDecision"; break;
	case 111: name = "FlowChartInputOutput"; break;
	case 112: name = "FlowChartPredefinedProcess"; break;
	case 113: name = "FlowChartInternalStorage"; break;
	case 114: name = "FlowChartDocument"; break;
	case 115: name = "FlowChartMultidocument"; break;
	case 116: name = "FlowChartTerminator"; break;
	case 117: name = "FlowChartPreparation"; break;
	case 118: name = "FlowChartManualInput"; break;
	case 119: name = "FlowChartManualOperation"; break;
	case 120: name = "FlowChartConnector"; break;
	case 121: name = "FlowChartPunchedCard"; break;
	case 122: name = "FlowChartPunchedTape"; break;
	case 123: name = "FlowChartSummingJunction"; break;
	case 124: name = "FlowChartOr"; break;
	case 125: name = "FlowChartCollate"; break;
	case 126: name = "FlowChartSort"; break;
	case 127: name = "FlowChartExtract"; break;
	case 128: name = "FlowChartMerge"; break;
	case 129: name = "FlowChartOfflineStorage"; break;
	case 130: name = "FlowChartOnlineStorage"; break;
	case 131: name = "FlowChartMagneticTape"; break;
	case 132: name = "FlowChartMagneticDisk"; break;
	case 133: name = "FlowChartMagneticDrum"; break;
	case 134: name = "FlowChartDisplay"; break;
	case 135: name = "FlowChartDelay"; break;
	case 136: name = "TextPlainText"; break;
	case 137: name = "TextStop"; break;
	case 138: name = "TextTriangle"; break;
	case 139: name = "TextTriangleInverted"; break;
	case 140: name = "TextChevron"; break;
	case 141: name = "TextChevronInverted"; break;
	case 142: name = "TextRingInside"; break;
	case 143: name = "TextRingOutside"; break;
	case 144: name = "TextArchUpCurve"; break;
	case 145: name = "TextArchDownCurve"; break;
	case 146: name = "TextCircleCurve"; break;
	case 147: name = "TextButtonCurve"; break;
	case 148: name = "TextArchUpPour"; break;
	case 149: name = "TextArchDownPour"; break;
	case 150: name = "TextCirclePour"; break;
	case 151: name = "TextButtonPour"; break;
	case 152: name = "TextCurveUp"; break;
	case 153: name = "TextCurveDown"; break;
	case 154: name = "TextCascadeUp"; break;
	case 155: name = "TextCascadeDown"; break;
	case 156: name = "TextWave1"; break;
	case 157: name = "TextWave2"; break;
	case 158: name = "TextWave3"; break;
	case 159: name = "TextWave4"; break;
	case 160: name = "TextInflate"; break;
	case 161: name = "TextDeflate"; break;
	case 162: name = "TextInflateBottom"; break;
	case 163: name = "TextDeflateBottom"; break;
	case 164: name = "TextInflateTop"; break;
	case 165: name = "TextDeflateTop"; break;
	case 166: name = "TextDeflateInflate"; break;
	case 167: name = "TextDeflateInflateDeflate"; break;
	case 168: name = "TextFadeRight"; break;
	case 169: name = "TextFadeLeft"; break;
	case 170: name = "TextFadeUp"; break;
	case 171: name = "TextFadeDown"; break;
	case 172: name = "TextSlantUp"; break;
	case 173: name = "TextSlantDown"; break;
	case 174: name = "TextCanUp"; break;
	case 175: name = "TextCanDown"; break;
	case 176: name = "FlowChartAlternateProcess"; break;
	case 177: name = "FlowChartOffpageConnector"; break;
	case 178: name = "Callout90"; break;
	case 179: name = "AccentCallout90"; break;
	case 180: name = "BorderCallout90"; break;
	case 181: name = "AccentBorderCallout90"; break;
	case 182: name = "LeftRightUpArrow"; break;
	case 183: name = "Sun"; break;
	case 184: name = "Moon"; break;
	case 185: name = "BracketPair"; break;
	case 186: name = "BracePair"; break;
	case 187: name = "Seal4"; break;
	case 188: name = "DoubleWave"; break;
	case 189: name = "ActionButtonBlank"; break;
	case 190: name = "ActionButtonHome"; break;
	case 191: name = "ActionButtonHelp"; break;
	case 192: name = "ActionButtonInformation"; break;
	case 193: name = "ActionButtonForwardNext"; break;
	case 194: name = "ActionButtonBackPrevious"; break;
	case 195: name = "ActionButtonEnd"; break;
	case 196: name = "ActionButtonBeginning"; break;
	case 197: name = "ActionButtonReturn"; break;
	case 198: name = "ActionButtonDocument"; break;
	case 199: name = "ActionButtonSound"; break;
	case 200: name = "ActionButtonMovie"; break;
	case 201: name = "HostControl"; break;
	case 202: name = "TextBox"; break;
	default :
		  name = "UNKNOWN";
	};
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("%s (0x%x);\n", name, h->instance);
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

		/* FALSE : Size shape to fit text size */
		case 190 : name = "BOOL fFitShapeToText"; break;

		/* FALSE : Size text to fit shape size */
		case 191 : name = "BOOL fFitTextToShape"; break;

		/* 0 : 16.16 fraction times total image width or height, as appropriate. */
		case 256 : name = "fixed16_16 cropFromTop"; break;
		case 257 : name = "fixed16_16 cropFromBottom"; break;
		case 258 : name = "fixed16_16 cropFromLeft"; break;
		case 259 : name = "fixed16_16 cropFromRight"; break;

		/* NULL : Blip to display */
		case 260 :
			   name = "Blip * pib";
			   h->blip_id = (int)val - 1;
			   break;

		/* NULL : Blip file name */
		case 261 : name = "wchar * pibName"; break;

		/* What are MSOBLIPFLAGS ? */
		/* Comment Blip flags */
		case 262 : name = "BLIPFLAGS pibFlags"; break;

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

		/* undefined : Double shadow colour */
		case 268 : name = "Colour pictureDblCrMod"; break;

		/* undefined : */
		case 269 : name = "Colour pictureFillCrMod"; break;

		/* undefined : */
		case 270 : name = "Colour pictureLineCrMod"; break;

		/* NULL : Blip to display when printing */
		case 271 : name = "Blip * pibPrint"; break;

		/* NULL : Blip file name */
		case 272 : name = "wchar * pibPrintName"; break;

		/* What are MSOBLIPFLAGS ? */
		/* Comment Blip flags */
		case 273 : name = "BLIPFLAGS pibPrintFlags"; break;

		/* FALSE : Do not hit test the picture */
		case 316 : name = "bool fNoHitTestPicture"; break;

		/* FALSE : grayscale display */
		case 317 : name = "bool pictureGray"; break;

		/* FALSE : bi-level display */
		case 318 : name = "bool pictureBiLevel"; break;

		/* FALSE : Server is active (OLE objects only) */
		case 319 : name = "bool pictureActive"; break;

		/* white : Foreground color */
		case 385 : name = "Colour fillColor"; break;

		/* black : Color of line */
		case 448 : name = "Colour lineColor"; break;

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
ms_escher_read_ClientData (MSEscherState * state, MSEscherHeader * h)
{
	int i;
	guint16 opcode;
	MSObj * obj;

	g_return_val_if_fail (h->len == common_header_len, TRUE);
	g_return_val_if_fail (h->offset + h->len == state->end_offset, TRUE);

	/* Read the OBJ, be VERY careful until we are sure of the state */
	g_return_val_if_fail (ms_biff_query_peek_next (state->q, &opcode), TRUE);
	g_return_val_if_fail (opcode == BIFF_OBJ, TRUE);
	g_return_val_if_fail (ms_biff_query_next (state->q), TRUE);

	obj = ms_read_OBJ (state->q, state->wb, state->sheet->gnum_sheet);

	if (obj == NULL)
		return FALSE;

	/* We should have an anchor set by now */
	g_return_val_if_fail (h->anchor_set, FALSE);
	g_return_val_if_fail (!obj->anchor_set, FALSE);

	for (i = 4; --i >= 0 ; )
		obj->anchor[i] = h->anchor[i];
	obj->anchor_set = TRUE;
	obj->v.picture.blip_id = h->blip_id;

	return ms_obj_realize(obj, state->wb, state->sheet);
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
