/**
 * ms-ole-summary.c: MS Office OLE support
 *
 * Authors:
 *    Michael Meeks (mmeeks@gnu.org)
 *    Frank Chiulli (fc-linux@home.com)
 * From work by:
 *    Caolan McNamara (Caolan.McNamara@ul.ie)
 * Built on work by:
 *    Somar Software's CPPSUM (http://www.somar.com)
 **/

#include <glib.h>
#include <stdio.h>

#include "ms-ole.h"
#include "ms-ole-summary.h"


#define SUMMARY_ID(x) ((x) & 0xff)

typedef struct {
	guint32             offset;
	guint32             id;
	MsOlePropertySetID  ps_id;
} item_t;

const guint32	sum_fmtid[4] =  {
				 0xF29F85E0,
		           	 0x10684FF9,
		           	 0x000891AB,
		           	 0xD9B3272B
				};

const guint32	doc_fmtid[4] =  {	
				 0xD5CDD502,
		           	 0x101B2E9C,
		           	 0x00089793,
		           	 0xAEF92C2B
			        };


const guint32	user_fmtid[4] = {			
				 0XD5CDD505,
				 0X101B2E9C,
		           	 0X00089793,
		           	 0XAEF92C2B
				};


static gboolean
read_items (MsOleSummary *si, MsOlePropertySetID ps_id)
{
	gint sect;
	
	for (sect = 0; sect < si->sections->len; sect++) {
		MsOleSummarySection st;
		guint8 data[8];
		gint   i;
		
		st = g_array_index (si->sections, MsOleSummarySection, sect);

		if (st.ps_id != ps_id)
			continue;

		si->s->lseek (si->s, st.offset, MsOleSeekSet);
		if (!si->s->read_copy (si->s, data, 8))
			return FALSE;
		
		st.bytes = MS_OLE_GET_GUINT32 (data);
		st.props = MS_OLE_GET_GUINT32 (data + 4);

		if (st.props == 0)
			continue;
		
		for (i = 0; i < st.props; i++) {
			item_t item;
			if (!si->s->read_copy (si->s, data, 8))
				return FALSE;

			item.id     = MS_OLE_GET_GUINT32 (data);
			item.offset = MS_OLE_GET_GUINT32 (data + 4);
			item.offset = item.offset + st.offset;
			item.ps_id  = ps_id;
			g_array_append_val (si->items, item);
		}
	}
	return TRUE;
}

typedef struct {
	MsOleSummaryPID  id;
	guint32          len;
	guint8          *data;
} write_item_t;


#define PROPERTY_HDR_LEN	8
#define PROPERTY_DESC_LEN	8
static void
write_items (MsOleSummary *si)
{
	MsOlePos cur_pos;
	MsOlePos pos = 48; /* magic offset see: _create_stream */
	guint8   data[PROPERTY_DESC_LEN];
	guint8   fill_data[] = {0, 0, 0, 0};
	guint32  i, num;
	guint32  offset = 0;
	GList   *l;

	/*
	 *  Write out the property descriptors.
	 *  Keep track of the number of properties and number of bytes for the properties.
	 */
	si->s->lseek (si->s, pos + PROPERTY_HDR_LEN, MsOleSeekSet);

	l = si->write_items;
	num = g_list_length (l);
	i = 0;
	offset = PROPERTY_HDR_LEN + num * PROPERTY_DESC_LEN;
	while (l) {
		write_item_t *w = l->data;
		g_return_if_fail (w != NULL);

		/*
		 *  The offset is calculated from the start of the 
		 *  properties header.  The offset must be on a 
		 *  4-byte boundary.  Therefore all data written must be
		 *  in multiples of 4-bytes.
		 */
		MS_OLE_SET_GUINT32 (data + 0, w->id & 0xff);
		MS_OLE_SET_GUINT32 (data + 4, offset);
		si->s->write (si->s, data, PROPERTY_DESC_LEN);

		offset += w->len;
		if ((w->len & 0x3) > 0)
			offset += (4 - (w->len & 0x3));
			
		i++;

		l = g_list_next (l);
	}

	g_return_if_fail (i == num);
	
	/*
	 *  Write out the section header.
	 */
	si->s->lseek (si->s, pos, MsOleSeekSet);
	MS_OLE_SET_GUINT32 (data + 0, offset);
	MS_OLE_SET_GUINT32 (data + 4, i);
	si->s->write (si->s, data, PROPERTY_HDR_LEN);

	/*
	 *  Write out the property values.
	 *  Keep track of the last position written to.
	 */
	cur_pos = pos + PROPERTY_HDR_LEN + num*PROPERTY_DESC_LEN;
	si->s->lseek (si->s, cur_pos, MsOleSeekSet);
	l = si->write_items;
	while (l) {
		write_item_t *w = l->data;
		si->s->write (si->s, w->data, w->len);
		cur_pos += w->len;
		l = g_list_next (l);

		/*
		 * Write out any fill.
		 */
		if ((w->len & 0x3) > 0) {
		        cur_pos += (4 - (w->len & 0x3));
			si->s->write (si->s, fill_data, 4 - (w->len & 0x3));
		}

	}

	/*
	 * Pad it out to a BB file.
	 */
	{
		int     i;
		for (i = cur_pos; i < 0x1000; i+=4)
			si->s->write (si->s, fill_data, 4);
	}

}

/**
 * ms_ole_summary_open_stream:
 * @s: stream object
 * @psid: Property Set ID, indicates which property set to open
 * 
 * Opens @s as a summary stream, returns NULL on failure.
 * 
 * Return value: %NULL if unable to open summary stream or a pointer to the 
 * Summary Stream.
 **/
MsOleSummary *
ms_ole_summary_open_stream (MsOleStream *s, const MsOlePropertySetID psid)
{
	guint8              data[64];
	guint16             byte_order;
	gboolean            panic = FALSE;
	guint32             os_version;
	MsOleSummary       *si;
	gint                i, sections;

	g_return_val_if_fail (s != NULL, NULL);

	if (!s->read_copy (s, data, 28))
		return NULL;

	si                = g_new (MsOleSummary, 1);

	si->s             = s;
	si->write_items   = NULL;
	si->sections      = NULL;
	si->items         = NULL;
	si->read_mode     = TRUE;

	byte_order        = MS_OLE_GET_GUINT16(data);
	if (byte_order != 0xfffe)
		panic     = TRUE;

	if (MS_OLE_GET_GUINT16 (data + 2) != 0) /* Format */
		panic     = TRUE;

	os_version        = MS_OLE_GET_GUINT32 (data + 4);

	for (i = 0; i < 16; i++)
		si->class_id[i] = data[8 + i];

	sections          = MS_OLE_GET_GUINT32 (data + 24);

	if (panic) {
		ms_ole_summary_close (si);
		return NULL;
	}

	si->sections = g_array_new (FALSE, FALSE, sizeof (MsOleSummarySection));

	for (i = 0; i < sections; i++) {
		MsOleSummarySection sect;
		if (!s->read_copy (s, data, 16 + 4)) {
			ms_ole_summary_close (si);
			return NULL;
		}
		
		if (psid == MS_OLE_PS_SUMMARY_INFO) {
			if (MS_OLE_GET_GUINT32 (data +  0) == sum_fmtid[0] &&
			    MS_OLE_GET_GUINT32 (data +  4) == sum_fmtid[1] &&
			    MS_OLE_GET_GUINT32 (data +  8) == sum_fmtid[2] &&
			    MS_OLE_GET_GUINT32 (data + 12) == sum_fmtid[3]    ) {
				si->ps_id  = MS_OLE_PS_SUMMARY_INFO;
				sect.ps_id = MS_OLE_PS_SUMMARY_INFO;
			
			} else {
				ms_ole_summary_close (si);
				return NULL;
			}
			
		} else if (psid == MS_OLE_PS_DOCUMENT_SUMMARY_INFO) {
			if (MS_OLE_GET_GUINT32 (data +  0) == doc_fmtid[0] &&
		            MS_OLE_GET_GUINT32 (data +  4) == doc_fmtid[1] &&
		            MS_OLE_GET_GUINT32 (data +  8) == doc_fmtid[2] &&
		            MS_OLE_GET_GUINT32 (data + 12) == doc_fmtid[3]    ) {
				si->ps_id  = MS_OLE_PS_DOCUMENT_SUMMARY_INFO;
				sect.ps_id = MS_OLE_PS_DOCUMENT_SUMMARY_INFO;
			
			} else if (MS_OLE_GET_GUINT32 (data +  0) == user_fmtid[0] &&
		            	   MS_OLE_GET_GUINT32 (data +  4) == user_fmtid[1] &&
				   MS_OLE_GET_GUINT32 (data +  8) == user_fmtid[2] &&
				   MS_OLE_GET_GUINT32 (data + 12) == user_fmtid[3]    ) {
				si->ps_id  = MS_OLE_PS_DOCUMENT_SUMMARY_INFO;
				sect.ps_id = MS_OLE_PS_USER_DEFINED_SUMMARY_INFO;
			
			} else {
				ms_ole_summary_close (si);
				return NULL;
			}
		}

		sect.offset = MS_OLE_GET_GUINT32 (data + 16);
		g_array_append_val (si->sections, sect);
		/* We want to read the offsets of the items here into si->items */
	}

	si->items = g_array_new (FALSE, FALSE, sizeof (item_t));

	for (i = 0; i < sections; i++) {
		MsOleSummarySection st;

		st = g_array_index (si->sections, MsOleSummarySection, i);
		if (!read_items (si, st.ps_id)) {
			g_warning ("Serious error reading items");
			ms_ole_summary_close (si);
			return NULL;
		}
	}

	return si;
}

/**
 * ms_ole_summary_open:
 * @f: filesystem object.
 * 
 * Opens the SummaryInformation stream, returns NULL on failure.
 * 
 * Return value: %NULL if unable to open summary stream or a pointer to the 
 * SummaryInformation Stream.
 **/
MsOleSummary *
ms_ole_summary_open (MsOle *f)
{
	MsOleStream *s;
	MsOleErr     result;
	g_return_val_if_fail (f != NULL, NULL);

	result = ms_ole_stream_open (&s, f, "/",
				     "\05SummaryInformation", 'r');
	if (result != MS_OLE_ERR_OK || !s)
		return NULL;

	return ms_ole_summary_open_stream (s, MS_OLE_PS_SUMMARY_INFO);
}


/**
 * ms_ole_docsummary_open:
 * @f: filesystem object.
 * 
 * Opens the DocumentSummaryInformation stream, returns NULL on failure.
 * 
 * Return value: %NULL if unable to open summary stream or a pointer to the 
 * DocumentSummaryInformation Stream.
 **/
MsOleSummary *
ms_ole_docsummary_open (MsOle *f)
{
	MsOleStream *s;
	MsOleErr     result;
	g_return_val_if_fail (f != NULL, NULL);

	result = ms_ole_stream_open (&s, f, "/",
	                             "\05DocumentSummaryInformation", 'r');
	if (result != MS_OLE_ERR_OK || !s)
		return NULL;

	return ms_ole_summary_open_stream (s, MS_OLE_PS_DOCUMENT_SUMMARY_INFO);
}


/*
 * Cheat by hard coding magic numbers and chaining on.
 */
/**
 * ms_ole_summary_create_stream:
 * @s: stream object
 * @psid: Property Set ID, indicates which property set to open
 * 
 * Creates @s as a summary stream (@psid determines which one), returns NULL on
 * failure.
 * 
 * Return value: %NULL if unable to create stream, otherwise a pointer to a new
 * summary stream.
 **/
MsOleSummary *
ms_ole_summary_create_stream (MsOleStream *s, const MsOlePropertySetID psid)
{
	guint8        data[78];
	MsOleSummary *si;
	g_return_val_if_fail (s != NULL, NULL);

	MS_OLE_SET_GUINT16 (data +  0, 0xfffe); /* byte order */
	MS_OLE_SET_GUINT16 (data +  2, 0x0000); /* format */
	MS_OLE_SET_GUINT16 (data +  4, 0x0001); /* OS version A */
	MS_OLE_SET_GUINT16 (data +  6, 0x0000); /* OS version B */

	MS_OLE_SET_GUINT32 (data +  8, 0x0000); /* class id */
	MS_OLE_SET_GUINT32 (data + 12, 0x0000);
	MS_OLE_SET_GUINT32 (data + 16, 0x0000);
	MS_OLE_SET_GUINT32 (data + 20, 0x0000);

	if (psid == MS_OLE_PS_SUMMARY_INFO) {
		MS_OLE_SET_GUINT32 (data + 24, 0x0001); /* Sections */

		MS_OLE_SET_GUINT32 (data + 28, sum_fmtid[0]); /* ID */
		MS_OLE_SET_GUINT32 (data + 32, sum_fmtid[1]);
		MS_OLE_SET_GUINT32 (data + 36, sum_fmtid[2]);
		MS_OLE_SET_GUINT32 (data + 40, sum_fmtid[3]);

		MS_OLE_SET_GUINT32 (data + 44, 0x30); /* Section offset = 48 */

		MS_OLE_SET_GUINT32 (data + 48,  0); /* bytes */
		MS_OLE_SET_GUINT32 (data + 52,  0); /* properties */

		s->write (s, data, 56);

	} else if (psid == MS_OLE_PS_DOCUMENT_SUMMARY_INFO) {
		MS_OLE_SET_GUINT32 (data + 24, 0x0001); /* Sections */

		MS_OLE_SET_GUINT32 (data + 28, doc_fmtid[0]); /* ID */
		MS_OLE_SET_GUINT32 (data + 32, doc_fmtid[1]);
		MS_OLE_SET_GUINT32 (data + 36, doc_fmtid[2]);
		MS_OLE_SET_GUINT32 (data + 40, doc_fmtid[3]);

		MS_OLE_SET_GUINT32 (data + 44, 0x30); /* Section offset = 48 */

		MS_OLE_SET_GUINT32 (data + 48,  0); /* bytes */
		MS_OLE_SET_GUINT32 (data + 52,  0); /* properties */

		s->write (s, data, 56);

	}

	s->lseek (s, 0, MsOleSeekSet);

	si = ms_ole_summary_open_stream (s, psid);
	si->read_mode = FALSE;

	return si;
}


/**
 * ms_ole_summary_create:
 * @f: filesystem object.
 * 
 * Create a SummaryInformation stream, returns NULL on failure.
 * 
 * Return value: %NULL if unable to create the stream, otherwise a pointer to a
 * new SummaryInformation stream.
 **/
MsOleSummary *
ms_ole_summary_create (MsOle *f)
{
	MsOleStream *s;
	MsOleErr     result;

	g_return_val_if_fail (f != NULL, NULL);

	result = ms_ole_stream_open (&s, f, "/",
				     "\05SummaryInformation", 'w');
	if (result != MS_OLE_ERR_OK || !s) {
		printf ("ms_ole_summary_create: Can't open stream for writing\n");
		return NULL;
	}

	return ms_ole_summary_create_stream (s, MS_OLE_PS_SUMMARY_INFO);
}


/**
 * ms_ole_docsummary_create:
 * @f: filesystem object.
 * 
 * Create a DocumentSummaryInformation stream, returns NULL on failure.
 * 
 * Return value: %NULL if unable to create the stream, otherwise a pointer to a
 * new DocumentSummaryInformation stream.
 **/
MsOleSummary *
ms_ole_docsummary_create (MsOle *f)
{
	MsOleStream *s;
	MsOleErr     result;

	g_return_val_if_fail (f != NULL, NULL);

	result = ms_ole_stream_open (&s, f, "/",
				     "\05DocumentSummaryInformation", 'w');
	if (result != MS_OLE_ERR_OK || !s) {
		printf ("ms_ole_docsummary_create: Can't open stream for writing\n");
		return NULL;
	}

	return ms_ole_summary_create_stream (s, MS_OLE_PS_DOCUMENT_SUMMARY_INFO);
}


/* FIXME: without the helpful type */
/**
 * ms_ole_summary_get_properties:
 * @si: summary stream
 * 
 * Returns an array of MsOleSummaryPID.
 * 
 * Return value: an array of property ids in the current summary stream or 
 * %NULL if either the summary stream is non-existent or the summary stream
 * contains no properties.
 **/
GArray *
ms_ole_summary_get_properties (MsOleSummary *si)
{
	GArray *ans;
	gint i;

	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (si->items != NULL, NULL);

	ans = g_array_new (FALSE, FALSE, sizeof (MsOleSummaryPID));
	g_array_set_size  (ans, si->items->len);
	for (i = 0; i < si->items->len; i++)
		g_array_index (ans, MsOleSummaryPID, i) = 
			g_array_index (si->items, item_t, i).id;

	return ans;
}

/**
 * ms_ole_summary_close:
 * @si: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_close (MsOleSummary *si)
{
	g_return_if_fail (si != NULL);
	g_return_if_fail (si->s != NULL);

	if (!si->read_mode)
		write_items (si);
	
	if (si->sections)
		g_array_free (si->sections, TRUE);
	si->sections = NULL;

	if (si->items)
		g_array_free (si->items, TRUE);
	si->items = NULL;

	if (si->s)
		ms_ole_stream_close (&si->s);
	si->s = NULL;

	g_free (si);
}


/*
 *                        Record handling code
 */
#define TYPE_SHORT	0x02		/*   2, VT_I2,		2-byte signed integer  */
#define TYPE_LONG       0x03		/*   3, VT_I4,		4-byte signed integer  */
#define TYPE_BOOLEAN	0x0b		/*  11, VT_BOOL,	Boolean value  */
#define TYPE_STRING     0x1e		/*  30, VT_LPSTR,	Pointer to null terminated ANSI string */
#define TYPE_TIME       0x40		/*  64, VT_FILETIME,	64-bit FILETIME structure  */
#define TYPE_PREVIEW    0x47		/*  71, VT_CF,		Pointer to a CLIPDATA structure  */

/* Seeks to the correct place, and returns a handle or NULL on failure */
static item_t *
seek_to_record (MsOleSummary *si, MsOleSummaryPID id)
{
	gint i;
	g_return_val_if_fail (si->items, FALSE);

	/* These should / could be sorted for speed */
	for (i = 0; i < si->items->len; i++) {
		item_t item = g_array_index (si->items, item_t, i);
		if (item.id == SUMMARY_ID(id)) {
			gboolean is_summary, is_doc_summary;

			is_summary     = ((si->ps_id == MS_OLE_PS_SUMMARY_INFO) && 
					  (item.ps_id == MS_OLE_PS_SUMMARY_INFO));
			is_doc_summary = ((si->ps_id == MS_OLE_PS_DOCUMENT_SUMMARY_INFO) && 
					  (item.ps_id == MS_OLE_PS_DOCUMENT_SUMMARY_INFO));
			if (is_summary || is_doc_summary) {
				si->s->lseek (si->s, item.offset, MsOleSeekSet);
				return &g_array_index (si->items, item_t, i);
			}
		}
	}
	return NULL;
}

/**
 * ms_ole_summary_get_string:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * Note: Ensure that you free returned value after use.
 * 
 * Return value: FIXME
 **/
char *
ms_ole_summary_get_string (MsOleSummary *si, MsOleSummaryPID id,
			   gboolean *available)
{
	guint8   data[8];
	guint32  type, len;
	gchar   *ans;
	item_t *item;

	g_return_val_if_fail (available != NULL, 0);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (si->read_mode, NULL);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_STRING, NULL);

	if (!(item = seek_to_record (si, id)))
		return NULL;

	if (!si->s->read_copy (si->s, data, 8))
		return NULL;

	type = MS_OLE_GET_GUINT32 (data);
	len  = MS_OLE_GET_GUINT32 (data + 4);

	if (type != TYPE_STRING) { /* Very odd */
		g_warning ("Summary string type mismatch");
		return NULL;
	}

	ans = g_new (gchar, len + 1);
	
	if (!si->s->read_copy (si->s, ans, len)) {
		g_free (ans);
		return NULL;
	}

	ans[len] = '\0';

	*available = TRUE;
	return ans;
}

/**
 * ms_ole_summary_get_short:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * 
 * Return value: FIXME
 **/
guint16
ms_ole_summary_get_short (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *available)
{
	guint8   data[8];
	guint32  type;
	guint32  value;
	item_t  *item;

	g_return_val_if_fail (available != NULL, 0);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, 0);
	g_return_val_if_fail (si->read_mode, 0);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_SHORT, 0);

	if (!(item = seek_to_record (si, id)))
		return 0;

	if (!si->s->read_copy (si->s, data, 8))
		return 0;

	type  = MS_OLE_GET_GUINT32 (data);
	value = MS_OLE_GET_GUINT16 (data + 4);

	if (type != TYPE_SHORT) { /* Very odd */
		g_warning ("Summary short type mismatch");
		return 0;
	}

	*available = TRUE;
	return value;
}

/**
 * ms_ole_summary_get_boolean:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * 
 * Return value: FIXME
 **/
gboolean
ms_ole_summary_get_boolean (MsOleSummary *si, MsOleSummaryPID id,
			    gboolean *available)
{
	guint8    data[8];
	guint32   type;
	gboolean  value;
	item_t   *item;

	g_return_val_if_fail (available != NULL, 0);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, 0);
	g_return_val_if_fail (si->read_mode, 0);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_BOOLEAN, 0);

	if (!(item = seek_to_record (si, id)))
		return 0;

	if (!si->s->read_copy (si->s, data, 8))
		return 0;

	type  = MS_OLE_GET_GUINT32  (data);
	value = MS_OLE_GET_GUINT16 (data + 4);

	if (type != TYPE_BOOLEAN) { /* Very odd */
		g_warning ("Summary boolean type mismatch");
		return 0;
	}

	*available = TRUE;
	return value;
}

/**
 * ms_ole_summary_get_long:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * 
 * Return value: FIXME
 **/
guint32
ms_ole_summary_get_long (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *available)
{
	guint8  data[8];
	guint32 type, value;
	item_t *item;

	g_return_val_if_fail (available != NULL, 0);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, 0);
	g_return_val_if_fail (si->read_mode, 0);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_LONG, 0);

	if (!(item = seek_to_record (si, id)))
		return 0;

	if (!si->s->read_copy (si->s, data, 8))
		return 0;

	type  = MS_OLE_GET_GUINT32 (data);
	value = MS_OLE_GET_GUINT32 (data + 4);

	if (type != TYPE_LONG) { /* Very odd */
		g_warning ("Summary long type mismatch");
		return 0;
	}

	*available = TRUE;
	return value;
}


/*
 *  filetime_to_unixtime
 *
 *  Convert a FILETIME format to unixtime
 *  FILETIME is the number of 100ns units since January 1, 1601.
 *  unixtime is the number of seconds since January 1, 1970.
 *
 *  The difference in 100ns units between the two dates is:
 *	116,444,736,000,000,000  (TIMEDIF)
 *  (I'll let you do the math)
 *  If we divide this into pieces,
 *    high 32-bits = 27111902 or  TIMEDIF / 16^8
 *    mid  16-bits =    54590 or (TIMEDIF - (high 32-bits * 16^8)) / 16^4
 *    low  16-bits =    32768 or (TIMEDIF - (high 32-bits * 16^8) - (mid 16-bits * 16^4)
 *
 *  where all math is integer.
 *
 *  Adapted from work in 'wv' by:
 *    Caolan McNamara (Caolan.McNamara@ul.ie)
 */
#define HIGH32_DELTA	27111902
#define MID16_DELTA	   54590
#define LOW16_DELTA        32768

/**
 * filetime_to_unixtime:
 * @low_time: FIXME
 * @high_time: FIXME
 * 
 * Converts a FILETIME format to unixtime. FILETIME is the number of 100ns units
 * since January 1, 1601. unixtime is the number of seconds since January 1,
 * 1970.
 * 
 * Return value: FIXME
 **/
glong filetime_to_unixtime (guint32 low_time, guint32 high_time);
glong
filetime_to_unixtime (guint32 low_time, guint32 high_time)
{
	guint32		 low16;		/* 16 bit, low    bits */
	guint32		 mid16;		/* 16 bit, medium bits */
	guint32		 hi32;		/* 32 bit, high   bits */
	unsigned int	 carry;		/* carry bit for subtraction */
	int		 negative;	/* whether a represents a negative value */

	/* Copy the time values to hi32/mid16/low16 */
	hi32  =  high_time;
	mid16 = low_time >> 16;
	low16 = low_time &  0xffff;

	/* Subtract the time difference */
	if (low16 >= LOW16_DELTA           )
		low16 -=             LOW16_DELTA        , carry = 0;
	else
		low16 += (1 << 16) - LOW16_DELTA        , carry = 1;

	if (mid16 >= MID16_DELTA    + carry)
		mid16 -=             MID16_DELTA + carry, carry = 0;
	else
		mid16 += (1 << 16) - MID16_DELTA - carry, carry = 1;

	hi32 -= HIGH32_DELTA + carry;

	/* If a is negative, replace a by (-1-a) */
	negative = (hi32 >= ((guint32)1) << 31);
	if (negative) {
		/* Set a to -a - 1 (a is hi32/mid16/low16) */
		low16 = 0xffff - low16;
		mid16 = 0xffff - mid16;
		hi32 = ~hi32;
	}

	/*
	 *  Divide a by 10000000 (a = hi32/mid16/low16), put the rest into r.
         * Split the divisor into 10000 * 1000 which are both less than 0xffff.
	 */
	mid16 += (hi32 % 10000) << 16;
	hi32  /=       10000;
	low16 += (mid16 % 10000) << 16;
	mid16 /=       10000;
	low16 /=       10000;

	mid16 += (hi32 % 1000) << 16;
	hi32  /=       1000;
	low16 += (mid16 % 1000) << 16;
	mid16 /=       1000;
	low16 /=       1000;

	/* If a was negative, replace a by (-1-a) and r by (9999999 - r) */
	if (negative) {
		/* Set a to -a - 1 (a is hi32/mid16/low16) */
		low16 = 0xffff - low16;
		mid16 = 0xffff - mid16;
		hi32 = ~hi32;
	}

	/*  Do not replace this by << 32, it gives a compiler warning and 
	 *  it does not work
	 */
	return ((((glong)hi32) << 16) << 16) + (mid16 << 16) + low16;

}


/**
 * unixtime_to_filetime:
 * @unix_time: FIXME
 * @time_high: FIXME
 * @time_low: FIXME
 * 
 * Converts a unixtime format to FILETIME. FILETIME is the number of 100ns units
 * since January 1, 1601. unixtime is the number of seconds since January 1,
 * 1970.
 **/
void unixtime_to_filetime (time_t unix_time, unsigned int *time_high,
			   unsigned int *time_low);
void
unixtime_to_filetime (time_t unix_time, unsigned int *time_high, unsigned int *time_low)
{
	unsigned int	 low_16;
	unsigned int	 mid_16;
	unsigned int	 high32;
	unsigned int	 carry;
	
	/*
	 *  First split unix_time up.
	 */
	high32 = (unix_time >> 16) >> 16;
	mid_16 =  unix_time >> 16;
	low_16 =  unix_time  & 0xffff;
	
	/*
	 *  Convert seconds to 100 ns units by multipling by 10,000,000.
	 *  Do this in two steps, 10,000 and 1,000.
	 */
	low_16 *= 10000;
	carry   = (low_16) >> 16;
	low_16  = low_16 & 0xffff;
	
	mid_16 *= 10000;
	mid_16 += carry;
	carry   = (mid_16 >> 16);
	mid_16  = mid_16 & 0xffff;
	
	high32 *= 10000;
	high32 += carry;
	
	
	low_16 *= 1000;
	carry   = (low_16) >> 16;
	low_16  = low_16 & 0xffff;
	
	mid_16 *= 1000;
	mid_16 += carry;
	carry   = (mid_16 >> 16);
	mid_16  = mid_16 & 0xffff;
	
	high32 *= 1000;
	high32 += carry;
	
	/*
	 *  Now add in the time difference.
	 */
	low_16 += LOW16_DELTA;
	mid_16 += (low_16 >> 16);
	low_16  =  low_16  & 0xffff;
	
	mid_16 += MID16_DELTA;
	high32 += (mid_16 >> 16);
	mid_16  =  mid_16  & 0xffff;
	
	high32 += HIGH32_DELTA;
	
	*time_high = high32;
	*time_low  = (mid_16 << 16) + low_16;
	
	return;
	
}


/**
 * ms_ole_summary_get_time:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * 
 * Return value: FIXME
 **/
GTimeVal
ms_ole_summary_get_time (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *available)
{
	guint8   data[12];
	guint32  type;
	guint32  low_time;
	guint32  high_time;
	item_t  *item;
	GTimeVal time;

	time.tv_sec  = 0;   /* Magic numbers */
	time.tv_usec = 0;
/*	g_date_set_dmy (&time.date, 18, 6, 1977); */

	g_return_val_if_fail (available != NULL, time);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, time);
	g_return_val_if_fail (si->read_mode, time);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_TIME, time);

	if (!(item = seek_to_record (si, id)))
		return time;

	if (!si->s->read_copy (si->s, data, 12))
		return time;

	type      = MS_OLE_GET_GUINT32 (data);
	low_time  = MS_OLE_GET_GUINT32 (data + 4);
	high_time = MS_OLE_GET_GUINT32 (data + 8);

	if (type != TYPE_TIME) { /* Very odd */
		g_warning ("Summary time type mismatch");
		return time;
	}

	time.tv_sec = filetime_to_unixtime (low_time, high_time);
	
	*available = TRUE;
	return time;
}

/**
 * ms_ole_summary_preview_destroy:
 * @d: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_preview_destroy (MsOleSummaryPreview d)
{
	if (d.data)
		g_free (d.data);
	d.data = NULL;
}

/**
 * ms_ole_summary_get_preview:
 * @si: FIXME
 * @id: FIXME
 * @available: FIXME
 * 
 * FIXME
 * 
 * Return value: FIXME
 **/
MsOleSummaryPreview
ms_ole_summary_get_preview (MsOleSummary *si, MsOleSummaryPID id,
			    gboolean *available)
{
	guint8  data[8];
	guint32 type;
	MsOleSummaryPreview ans;
	item_t *item;

	ans.len  = 0;
	ans.data = NULL;

	g_return_val_if_fail (available != NULL, ans);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, ans);
	g_return_val_if_fail (si->read_mode, ans);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_OTHER, ans);

	if (!(item = seek_to_record (si, id)))
		return ans;

	if (!si->s->read_copy (si->s, data, 8))
		return ans;

	type     = MS_OLE_GET_GUINT32 (data);
	ans.len  = MS_OLE_GET_GUINT32 (data + 4);

	if (type != TYPE_PREVIEW) { /* Very odd */
		g_warning ("Summary wmf type mismatch");
		return ans;
	}

	ans.data = g_new (guint8, ans.len + 1);
	
	if (!si->s->read_copy (si->s, ans.data, ans.len)) {
		g_free (ans.data);
		return ans;
	}

	*available = TRUE;
	return ans;
}

static write_item_t *
write_item_t_new (MsOleSummary *si, MsOleSummaryPID id)
{
	write_item_t *w = g_new (write_item_t, 1);

	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (!si->read_mode, NULL);

	w->id           = id;
	w->len          = 0;
	w->data         = NULL;
	si->write_items = g_list_append (si->write_items, w);

	return w;
}

/**
 * ms_ole_summary_set_preview:
 * @si: FIXME
 * @id: FIXME
 * @preview: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_preview (MsOleSummary *si, MsOleSummaryPID id,
			    const MsOleSummaryPreview *preview)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (!si->read_mode);
	g_return_if_fail (preview != NULL);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, preview->len + 8);

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_PREVIEW);
	MS_OLE_SET_GUINT32 (w->data + 4, preview->len);

	memcpy (w->data + 8, preview->data, preview->len);
	
	w->len = preview->len + 8;
}

/**
 * ms_ole_summary_set_time:
 * @si: FIXME
 * @id: FIXME
 * @time: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_time (MsOleSummary *si, MsOleSummaryPID id,
			 GTimeVal time)
{
	unsigned int	 time_high;
	unsigned int	 time_low;
	write_item_t	*w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (!si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 12);
	w->len  = 12;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_TIME);
	
        unixtime_to_filetime ((time_t)time.tv_sec, &time_high, &time_low);
	
	MS_OLE_SET_GUINT32 (w->data + 4, time_low);
	MS_OLE_SET_GUINT32 (w->data + 8, time_high);
}

/**
 * ms_ole_summary_set_boolean:
 * @si: FIXME
 * @id: FIXME
 * @bool: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_boolean (MsOleSummary *si, MsOleSummaryPID id,
			    gboolean bool)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (!si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 8);
	w->len  = 6;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_BOOLEAN);
	MS_OLE_SET_GUINT16 (w->data + 4, bool);
}



/**
 * ms_ole_summary_set_short:
 * @si: FIXME
 * @id: FIXME
 * @i: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_short (MsOleSummary *si, MsOleSummaryPID id,
			  guint16 i)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (!si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 8);
	w->len  = 6;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_SHORT);
	MS_OLE_SET_GUINT16 (w->data + 4, i);
}

/**
 * ms_ole_summary_set_long:
 * @si: FIXME
 * @id: FIXME
 * @i: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_long (MsOleSummary *si, MsOleSummaryPID id,
			 guint32 i)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (!si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 8);
	w->len  = 8;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_LONG);
	MS_OLE_SET_GUINT32 (w->data + 4, i);
}

/**
 * ms_ole_summary_set_string:
 * @si: FIXME
 * @id: FIXME
 * @str: FIXME
 * 
 * FIXME
 **/
void
ms_ole_summary_set_string (MsOleSummary *si, MsOleSummaryPID id,
			   const gchar *str)
{
	write_item_t *w;
	guint32 len;

	g_return_if_fail (si != NULL);
	g_return_if_fail (str != NULL);
	g_return_if_fail (!si->read_mode);

	w       = write_item_t_new (si, id);
	len     = strlen (str) + 1;
	w->len  = len + 8;
	w->data = g_new (guint8, len + 8);
	
	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_STRING);
	MS_OLE_SET_GUINT32 (w->data + 4, len);

	memcpy (w->data + 8, str, len);
}
