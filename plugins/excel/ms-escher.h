#ifndef GNM_MS_OFFICE_ESCHER_H
#define GNM_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#include "ms-excel-read.h"
#include "ms-container.h"
#include "ms-obj.h"
#include <sheet-object.h>

enum {
	MSEP_LOCKROTATION         = 0x0077,
	MSEP_LOCKASPECTRATIO      = 0x0078,
	MSEP_LOCKPOSITION         = 0x0079,
	MSEP_LOCKAGAINSTSELECT    = 0x007a,
	MSEP_LOCKCROPPING         = 0x007b,
	MSEP_LOCKVERTICES         = 0x007c,
	MSEP_LOCKTEXT             = 0x007d,
	MSEP_LOCKADJUSTHANDLES    = 0x007e,
	MSEP_LOCKAGAINSTGROUPING  = 0x007f,
	MSEP_TXID                 = 0x0080,
	MSEP_WRAPTEXT             = 0x0085,
	MSEP_TXDIR                = 0x008b,
	MSEP_SELECTTEXT           = 0x00bb,
	MSEP_AUTOTEXTMARGIN       = 0x00bc,
	MSEP_FITTEXTTOSHAPE       = 0x00bf,
	MSEP_BLIPINDEX            = 0x0104,
	MSEP_SHAPEPATH            = 0x0144,
	MSEP_SHADOWOK             = 0x017a,
	MSEP_LINEOK               = 0x017c,
	MSEP_FILLOK               = 0x017f,
	MSEP_FILLTYPE             = 0x0180,
	MSEP_FILLCOLOR            = 0x0181,
	MSEP_FILLBACKCOLOR        = 0x0183,
	MSEP_FILLED               = 0x01bb,
	MSEP_NOFILLHITTEST        = 0x01bf,
	MSEP_LINECOLOR            = 0x01c0,
	MSEP_LINEWIDTH            = 0x01cb,
	MSEP_LINEDASHING          = 0x01ce,
	MSEP_LINESTARTARROWHEAD   = 0x01d0,
	MSEP_LINEENDARROWHEAD     = 0x01d1,
	MSEP_LINESTARTARROWWIDTH  = 0x01d2,
	MSEP_LINESTARTARROWLENGTH = 0x01d3,
	MSEP_LINEENDARROWWIDTH    = 0x01d4,
	MSEP_LINEENDARROWLENGTH   = 0x01d5,
	MSEP_ARROWHEADSOK         = 0x01fb,
	MSEP_LINE                 = 0x01fc,
	MSEP_NOLINEDRAWDASH       = 0x01ff,
	MSEP_SHADOWOBSCURED       = 0x023f,
	MSEP_NAME                 = 0x0380,
	MSEP_ISBUTTON             = 0x03bc
};

GOLineDashType ms_escher_xl_to_line_type (guint16 pattern);
int ms_escher_line_type_to_xl (GOLineDashType ld);


struct _MSEscherBlip {
	char const   *type;
	guint8       *data;
	guint32	      data_len;
	gboolean      needs_free;
};

MSObjAttrBag *ms_escher_parse (BiffQuery  *q, MSContainer *container,
			       gboolean return_attrs);

void ms_escher_blip_free (MSEscherBlip *blip);

/******************************************************/

guint ms_escher_get_inst (GString *buf, gsize marker);
void ms_escher_set_inst (GString *buf, gsize marker, guint inst);

gsize ms_escher_spcontainer_start (GString *buf);
void ms_escher_spcontainer_end (GString *buf, gsize marker);

void ms_escher_sp (GString *buf, guint32 spid, guint16 shape, guint32 flags);

gsize ms_escher_opt_start (GString *buf);
void ms_escher_opt_add_simple (GString *buf, gsize marker,
			       guint16 pid, gint32 val);
void ms_escher_opt_add_bool (GString *buf, gsize marker,
			     guint16 pid, gboolean b);
void ms_escher_opt_add_color (GString *buf, gsize marker,
			      guint16 pid, GOColor c);
void ms_escher_opt_add_str_wchar (GString *buf, gsize marker, GString *extra,
				  guint16 pid, const char *str);
void ms_escher_opt_end (GString *buf, gsize marker);

void ms_escher_clientanchor (GString *buf, SheetObjectAnchor const *anchor);

void ms_escher_clientdata (GString *buf);

/******************************************************/

#endif /* GNM_MS_OFFICE_ESCHER_H */
