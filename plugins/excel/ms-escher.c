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

#include "sheet-object.h"

#ifdef ENABLE_BONOBO
#	include <bonobo/gnome-stream.h>
#	include <bonobo/gnome-stream-memory.h>

#	include "sheet-object-container.h"
#endif

/* A storage accumulator for common state information */
typedef struct
{
	ExcelWorkbook  *wb;
	ExcelSheet     *sheet;
	BiffQuery      *q;

	int	        depth;
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

#ifdef ENABLE_BONOBO

typedef enum { ESCHER_BLIP } EscherType;
typedef struct {
	EscherType type;
	union {
		struct {
			GnomeStream *stream;
			const char  *reproid;
		} blip;
	} v;
} EscherRecord;

static EscherRecord *
escher_record_new_blip (guint8 const *data, guint32 len, const char *reproid)
{
	EscherRecord *er = g_new (EscherRecord, 1);
	guint8       *mem;

	g_return_val_if_fail (len > 0, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (reproid != NULL, NULL);

	mem                = g_malloc (len);
	memcpy (mem, data, len);
	er->type           = ESCHER_BLIP;
	er->v.blip.stream  = gnome_stream_mem_create (mem, len, TRUE);
	er->v.blip.reproid = reproid;

	return er;
}

/* OK it probably leaks / doesn't get called for now :-) */
static void
escher_record_destroy (EscherRecord *er)
{
	if (!er)
		return;

	switch (er->type) {
	case ESCHER_BLIP:
		if (er->v.blip.stream)
			gnome_object_destroy (GNOME_OBJECT (er->v.blip.stream));
		er->v.blip.stream  = NULL;
		er->v.blip.reproid = NULL;
		break;
	default:
		g_warning ("Internal escher type error");
		break;
	}
}
#endif

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
	/* Holds a 'Class ID Record' ID record which is only included in the
	 * 'clipboard format'.  It contains an OLE CLSID record from the source
	 * app, and can be used check where the clipboard data originated.
	 *
	 * We ignore these.  What is an 'OLE CLSID' ?  Would we ever need this ?
	 */
}

static void
ms_escher_read_ColorMRU (MSEscherState * state,
			 MSEscherCommonHeader * h)
{
	guint const num_colours = h->instance;

	printf ("There are %d colours in a record with remaining length %d;\n",
		num_colours, (h->len - common_header_len));

	/* Colors in order from left to right.  */
	/* TODO : When we know how to parse a colour record read these */
}
static void
ms_escher_read_SplitMenuColors (MSEscherState * state,
				MSEscherCommonHeader * h)
{
	g_return_if_fail (h->instance == 4);
	g_return_if_fail (h->len == 24); /* header + 4*4 */
	{
		guint8 const * data = h->data + common_header_len;
		guint32 const top_level_fill = MS_OLE_GET_GUINT32(data + 0);
		guint32 const line	= MS_OLE_GET_GUINT32(data + 4);
		guint32 const shadow	= MS_OLE_GET_GUINT32(data + 8);
		guint32 const threeD	= MS_OLE_GET_GUINT32(data + 12);

		printf ("top_level_fill = 0x%x, line = 0x%x, shadow = 0x%x, threeD = 0x%x;\n",
			top_level_fill, line, shadow, threeD);
	}
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
#ifdef ENABLE_BONOBO
	{
		int const header = 17 + primary_uid_size + common_header_len;
		EscherRecord *er = escher_record_new_blip (h->data + header,
							   h->len - header,
							   "bonobo-object:image-x-png");
		state->wb->eschers = g_list_append (state->wb->eschers, er);
		break;
	}
#endif
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
		     MSEscherCommonHeader * h)
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
	printf ("%s (0x%x);\n", name, h->instance);
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

/* TODO : This is a guess that explains the sheets we have. Find some
 * documentation. */
static void
ms_escher_read_ClientAnchor (MSEscherState * state,
			     MSEscherCommonHeader * h)
{
	Sheet const * sheet = state->sheet->gnum_sheet;
	guint8 const * data = h->data + common_header_len;
	double const zoom = sheet->last_zoom_factor_used;

	/* Warning this is host specific and only works for Excel */

	/* the word at offset 0 always seems to be 2 ?? */

	int i;
	/* Words 2, 6, 10, 14 : The row/col of the corners */
	/* Words 4, 8, 12, 16 : distance from cell edge measured in 1/1440 of an inch */
	float	margin[4], tmp;
	int	pos[4];
	for (i = 0; i < 4; ++i) {
		pos[i] = MS_OLE_GET_GUINT16(data + 4*i + 2);
		margin[i] = (MS_OLE_GET_GUINT16(data + 4*i + 4) / 20.);
		tmp = (i&1) /* odds are rows */
		    ? sheet_row_get_unit_distance (sheet, 0, pos[i])
		    : sheet_col_get_unit_distance (sheet, 0, pos[i]);
		margin[i] += tmp;
		margin[i] *= zoom;
	}

#ifdef ENABLE_BONOBO
	{ /* In the anals of ugly hacks, this is well up there :-) */
		GList        *l = state->wb->eschers;
		EscherRecord *er;
		SheetObject  *so;

		g_return_if_fail (l != NULL);
		er = l->data;
		g_return_if_fail (er != NULL);
		g_return_if_fail (state->sheet != NULL);
		g_return_if_fail (er->type == ESCHER_BLIP);
		g_return_if_fail (er->v.blip.stream != NULL);

		/* And lo, objects appeared always in the TLC */
		so = sheet_object_container_new (state->sheet->gnum_sheet,
						 margin[0], margin[1],
						 margin[2], margin[3],
						 er->v.blip.reproid);
		if (!sheet_object_container_load (so, er->v.blip.stream, TRUE))
			g_warning ("Failed to load '%s' from stream",
				   er->v.blip.reproid);
		
		escher_record_destroy (er);
		state->wb->eschers = g_list_remove (state->wb->eschers, l->data);
	}
#endif
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
	guint8 const * data = h->data + common_header_len;
	guint32 num_shapes = MS_OLE_GET_GUINT32(data);
	/* spid_cur = last SPID given to an SP in this DG :-)  */
	guint32 spid_cur   = MS_OLE_GET_GUINT32(data+4);
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
		    MSEscherCommonHeader * h)
{
	int const num_properties = h->instance;
	guint8 const *fopte = h->data + common_header_len;
	guint8 const *extra = fopte + 6*num_properties;
	guint prev_pid = 0; /* A debug tool */
	char const * name;
	int i;

	/* lets be really careful */
	g_return_if_fail (6*num_properties + common_header_len <= h->len);

	for (i = 0; i < num_properties; ++i, fopte += 6) {
		guint16 const tmp = MS_OLE_GET_GUINT32(fopte);
		guint const pid = tmp & 0x3fff;
		gboolean const is_blip = (tmp & 0x4000) != 0;
		gboolean const is_complex = (tmp & 0x8000) != 0;
		guint32 const val = MS_OLE_GET_GUINT32(fopte+2);

		/* container is sorted by pid. Use this as sanity test */
		g_return_if_fail (prev_pid < pid);
		prev_pid = pid;

		switch (pid) {
		/* LONG 0 fixed point: 16.16 degrees */
		case 4 : name = "rotation"; break;

		/* FALSE Size shape to fit text size */
		case 190 : name = "fFitShapeToText BOOL"; break;

		/* FALSE Size text to fit shape size */
		case 191 : name = "fFitTextToShape BOOL"; break;

		/* 16.16 fraction times total image width or height, as appropriate. */
		case 256 : name = "cropFromTop LONG"; break;

		/* 0 */
		case 257 : name = "cropFromBottom LONG"; break;

		/* 0 */
		case 258 : name = "cropFromLeft LONG"; break;

		/* 0 */
		case 259 : name = "cropFromRight LONG"; break;

		/* IMsoBlip* NULL Blip to display */
		case 260 : name = "pib"; break;

		/* WCHAR* NULL Blip file name */
		case 261 : name = "pibName"; break;

		/* MSOBLIPFLAGS Comment Blip flags */
		case 262 : name = "pibFlags"; break;

		/* LONG ~0 transparent color (none if ~0UL)  */
		case 263 : name = "pictureTransparent"; break;

		/* LONG 1<<16 contrast setting */
		case 264 : name = "pictureContrast"; break;

		/* LONG 0 brightness setting */
		case 265 : name = "pictureBrightness"; break;

		/* LONG 0 16.16 gamma */
		case 266 : name = "pictureGamma"; break;

		/* LONG 0 Host-defined ID for OLE objects (usually a pointer) */
		case 267 : name = "pictureId"; break;

		/* MSOCLR This Modification used if shape has double shadow */
		case 268 : name = "pictureDblCrMod"; break;

		/* MSOCLR undefined */
		case 269 : name = "pictureFillCrMod"; break;

		/* MSOCLR undefined */
		case 270 : name = "pictureLineCrMod"; break;

		/* IMsoBlip* NULL Blip to display when printing */
		case 271 : name = "pibPrint"; break;

		/* WCHAR* NULL Blip file name */
		case 272 : name = "pibPrintName"; break;

		/* MSOBLIPFLAGS Comment Blip flags */
		case 273 : name = "pibPrintFlags"; break;

		/* BOOL FALSE Do not hit test the picture */
		case 316 : name = "fNoHitTestPicture"; break;

		/* BOOL FALSE grayscale display */
		case 317 : name = "pictureGray"; break;

		/* BOOL FALSE bi-level display */
		case 318 : name = "pictureBiLevel"; break;

		/* BOOL FALSE Server is active (OLE objects only) */
		case 319 : name = "pictureActive"; break;

		/* MSOCLR white Foreground color */
		case 385 : name = "fillColor"; break;

		/* MSOCLR black Color of line */
		case 448 : name = "lineColor"; break;

		default : name = "";
		};

		printf ("%s %d = 0x%x (=%d) %s%s;\n", name, pid, val, val,
			is_blip ? " is blip" : "",
			is_complex ? " is complex" : "");
		if (is_complex) {
			extra += val;

			/* check for over run */
			g_return_if_fail (extra - h->data <= h->len);
		}
	}
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

	/* Lets be really really anal */
	g_return_val_if_fail (h->data_len >= h->len, FALSE);

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
			    nq->opcode == BIFF_MS_O_DRAWING_SELECTION) {
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

/**
 * ms_escher_hack_get_drawing:
 * @q:     Biff context.
 * @wb:    required workbook argument
 * @sheet: optional sheet argument
 * 
 *   This function parses an escher stream, and stores relevant data in the
 * workbook. 
 *
 **/
void
ms_escher_hack_get_drawing (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *sheet)
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

	state.wb    = wb;
	state.sheet = sheet;
	state.q     = q;
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
