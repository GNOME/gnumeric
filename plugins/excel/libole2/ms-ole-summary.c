/**
 * ms-ole-summary.h: MS Office OLE support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 * From work by:
 *    Caolan McNamara (Caolan.McNamara@ul.ie)
 * Built on work by:
 *    Somar Software's CPPSUM (http://www.somar.com)
 **/

#include <glib.h>
#include "ms-ole.h"
#include "ms-ole-summary.h"

#define SUMMARY_ID(x) ((x) & 0xff)

typedef struct {
	/* Could store the FID, but why bother ? */
	guint32   offset;
	guint32   props;
	guint32   bytes;
} section_t;

typedef struct {
	guint32 offset;
	guint32 id;
} item_t;

static gboolean
read_items (MsOleSummary *si)
{
	gint sect;
	
	for (sect = 0; sect < si->sections->len; sect++) {
		section_t st = g_array_index (si->sections, section_t, sect);
		guint8 data[8];
		gint i;
		
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

static void
write_items (MsOleSummary *si)
{
	MsOlePos pos   = 48; /* magic offset see: _create_stream */
	guint32  bytes = 0;
	guint8   data[8];
	guint32  i, num;
	GList   *l;

	si->s->lseek (si->s, pos + 8, MsOleSeekSet);

	l = si->write_items;
	num = g_list_length (l);
	i = 0;
	while (l) {
		write_item_t *w = l->data;
		g_return_if_fail (w != NULL);
		
		MS_OLE_SET_GUINT32 (data + 0, w->id&0xff);
		MS_OLE_SET_GUINT32 (data + 4, bytes + num*8);
		si->s->write (si->s, data, 8);

		bytes+= w->len;
		i++;

		l = g_list_next (l);
	}

	g_return_if_fail (i != num);
	
	si->s->lseek (si->s, pos, MsOleSeekSet);
	MS_OLE_SET_GUINT32 (data + 0, bytes);
	MS_OLE_SET_GUINT32 (data + 4, i);
	si->s->write (si->s, data, 8);

	si->s->lseek (si->s, pos + 8 + num*8, MsOleSeekSet);
	l = si->write_items;
	while (l) {
		write_item_t *w = l->data;
		si->s->write (si->s, w->data, w->len);
		l = g_list_next (l);
	}
}

MsOleSummary *
ms_ole_summary_open_stream (MsOleStream *s)
{
	guint8        data[64];
	guint16       byte_order;
	gboolean      panic = FALSE;
	guint32       os_version;
	MsOleSummary *si;
	gint          i, sections;

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

	for (i=0;i<16;i++)
		si->class_id[i] = data[8 + i];

	sections          = MS_OLE_GET_GUINT32 (data + 24);

	if (panic) {
		ms_ole_summary_close (si);
		return NULL;
	}

	si->sections = g_array_new (FALSE, FALSE, sizeof (section_t));

	for (i = 0; i < sections; i++) {
		section_t sect;
		if (!s->read_copy (s, data, 16+4)) {
			ms_ole_summary_close (si);
			return NULL;
		}
		if (MS_OLE_GET_GUINT32 (data +  0) != 0XF29F85E0 ||
		    MS_OLE_GET_GUINT32 (data +  4) != 0X10684FF9 ||
		    MS_OLE_GET_GUINT32 (data +  8) != 0X000891AB ||
		    MS_OLE_GET_GUINT32 (data + 12) != 0XD9B3272B) {
			ms_ole_summary_close (si);
			return NULL;
		}
		sect.offset = MS_OLE_GET_GUINT32 (data + 16);
		g_array_append_val (si->sections, sect);
		/* We want to read the offsets of the items here into si->items */
	}

	si->items = g_array_new (FALSE, FALSE, sizeof (item_t));

	if (!read_items (si)) {
		g_warning ("Serious error reading items");
		ms_ole_summary_close (si);
		return NULL;
	}

	return si;
}

MsOleSummary *
ms_ole_summary_open (MsOle *f)
{
	MsOleStream *s;
	g_return_val_if_fail (f != NULL, NULL);

	s = ms_ole_stream_open_name (f, "SummaryInformation", 'r');
	if (!s)
		return NULL;

	return ms_ole_summary_open_stream (s);
}

/*
 * Cheat by hard coding magic numbers and chaining on.
 */
MsOleSummary *
ms_ole_summary_create_stream (MsOleStream *s)
{
	guint8        data[64];
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
       
	MS_OLE_SET_GUINT32 (data + 24, 0x0001); /* Sections */

	MS_OLE_SET_GUINT32 (data + 28, 0xF29F85E0); /* ID */
	MS_OLE_SET_GUINT32 (data + 32, 0x10684FF9);
	MS_OLE_SET_GUINT32 (data + 36, 0x000891AB);
	MS_OLE_SET_GUINT32 (data + 40, 0xD9B3272B);

	MS_OLE_SET_GUINT32 (data + 44, 48); /* Section offset */

	MS_OLE_SET_GUINT32 (data + 48,  0); /* bytes */
	MS_OLE_SET_GUINT32 (data + 52,  0); /* properties */
	s->write (s, data, 56);

	s->lseek (s, 0, MsOleSeekSet);

	si = ms_ole_summary_open_stream (s);
	si->read_mode = FALSE;

	return si;
}

MsOleSummary *
ms_ole_summary_create (MsOle *f)
{
	MsOleStream *s;
	g_return_val_if_fail (f != NULL, NULL);

	s = ms_ole_stream_open_name (f, "SummaryInformation", 'w');
	if (!s)
		return NULL;

	return ms_ole_summary_create_stream (s);
}

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

void ms_ole_summary_close (MsOleSummary *si)
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
		ms_ole_stream_close (si->s);
	si->s = NULL;

	g_free (si);
}


/*
 *                        Record handling code
 */

#define TYPE_STRING     0x1e
#define TYPE_LONG       0x03
#define TYPE_PREVIEW    0x47
#define TYPE_TIME       0x1e

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
			si->s->lseek (si->s, item.offset, MsOleSeekSet);
			return &g_array_index (si->items, item_t, i);
		}
	}
	return NULL;
}

/* Ensure that you free these pointers after use */
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

static void
demangle_datetime (guint32 low, guint32 high, MsOleSummaryTime *time)
{
	/* See 'wv' for details */
	g_warning ("FIXME: a vile mess...");
}

MsOleSummaryTime
ms_ole_summary_get_time (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *available)
{
	guint8  data[12];
	guint32 type, lowdate, highdate;
	item_t *item;
	MsOleSummaryTime time;

	time.time.tv_sec  = 0;   /* Magic numbers */
	time.time.tv_usec = 0;
	g_date_set_dmy (&time.date, 18, 6, 1977);

	g_return_val_if_fail (available != NULL, time);
	*available = FALSE;
	g_return_val_if_fail (si != NULL, time);
	g_return_val_if_fail (si->read_mode, time);
	g_return_val_if_fail (MS_OLE_SUMMARY_TYPE (id) ==
			      MS_OLE_SUMMARY_TYPE_TIME, time);

	if (!(item = seek_to_record (si, id)))
		return time;

	if (!si->s->read_copy (si->s, data, 8))
		return time;

	type = MS_OLE_GET_GUINT32 (data);
	lowdate  = MS_OLE_GET_GUINT32 (data + 4);
	highdate = MS_OLE_GET_GUINT32 (data + 8);

	if (type != TYPE_TIME) { /* Very odd */
		g_warning ("Summary string type mismatch");
		return time;
	}

	demangle_datetime (highdate, lowdate, &time);

	*available = TRUE;
	return time;
}

void
ms_ole_summary_preview_destroy (MsOleSummaryPreview d)
{
	if (d.data)
		g_free (d.data);
	d.data = NULL;
}

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

void
ms_ole_summary_set_preview (MsOleSummary *si, MsOleSummaryPID id,
			    const MsOleSummaryPreview *preview)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (si->read_mode);
	g_return_if_fail (preview != NULL);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, preview->len + 8);

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_PREVIEW);
	MS_OLE_SET_GUINT32 (w->data + 4, preview->len);

	memcpy (w->data + 8, preview->data, preview->len);
	
	w->len = preview->len + 8;
}

void
ms_ole_summary_set_time (MsOleSummary *si, MsOleSummaryPID id,
			 const MsOleSummaryTime *time)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (time != NULL);
	g_return_if_fail (si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 12);
	w->len  = 12;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_TIME);
	MS_OLE_SET_GUINT32 (w->data + 4, 0xdeadcafe);
	MS_OLE_SET_GUINT32 (w->data + 8, 0xcafedead);

	g_warning ("times not yet implemented");
}

void
ms_ole_summary_set_long (MsOleSummary *si, MsOleSummaryPID id,
			 guint32 i)
{
	write_item_t *w;

	g_return_if_fail (si != NULL);
	g_return_if_fail (si->read_mode);

	w = write_item_t_new (si, id);

	w->data = g_new (guint8, 8);
	w->len  = 8;

	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_LONG);
	MS_OLE_SET_GUINT32 (w->data + 4, i);
}

void
ms_ole_summary_set_string (MsOleSummary *si, MsOleSummaryPID id,
			   const gchar *str)
{
	write_item_t *w;
	guint32 len;

	g_return_if_fail (si != NULL);
	g_return_if_fail (str != NULL);
	g_return_if_fail (si->read_mode);

	w       = write_item_t_new (si, id);

	len     = strlen (str);
	w->len  = len + 8;
	w->data = g_new (guint8, len + 8);
	
	MS_OLE_SET_GUINT32 (w->data + 0, TYPE_STRING);
	MS_OLE_SET_GUINT32 (w->data + 4, len);

	memcpy (w->data + 8, str, len);
}

