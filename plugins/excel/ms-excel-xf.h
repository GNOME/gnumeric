/**
 * ms-excel-xf.h: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks, Jody Goldberg
 **/
#ifndef GNUMERIC_MS_EXCEL_XF_H
#define GNUMERIC_MS_EXCEL_XF_H

#include "style.h"
#include "style-border.h"
#include "style-color.h"

#define STYLE_TOP		(MSTYLE_BORDER_TOP	    - MSTYLE_BORDER_TOP)
#define STYLE_BOTTOM		(MSTYLE_BORDER_BOTTOM	    - MSTYLE_BORDER_TOP)
#define STYLE_LEFT		(MSTYLE_BORDER_LEFT	    - MSTYLE_BORDER_TOP)
#define STYLE_RIGHT		(MSTYLE_BORDER_RIGHT	    - MSTYLE_BORDER_TOP)
#define STYLE_DIAGONAL		(MSTYLE_BORDER_DIAGONAL     - MSTYLE_BORDER_TOP)
#define STYLE_REV_DIAGONAL	(MSTYLE_BORDER_REV_DIAGONAL - MSTYLE_BORDER_TOP)

#define STYLE_ORIENT_MAX 6

typedef struct _BiffXFData {
	guint16 font_idx;
	guint16 format_idx;
	StyleFormat *style_format;
	gboolean hidden;
	gboolean locked;
	MsBiffXfType xftype;	/*  -- Very important field... */
	MsBiffFormat format;
	guint16 parentstyle;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	gboolean wrap_text;
	gboolean shrink_to_fit;
	guint8 rotation;
	int indent;
	StyleOrientation orientation;
	MsBiffEastern eastern;
	guint8 border_color[STYLE_ORIENT_MAX];
	StyleBorderType border_type[STYLE_ORIENT_MAX];
	guint8 fill_pattern_idx;
	guint8 pat_foregnd_col;
	guint8 pat_backgnd_col;
	guint16 differences;

	MStyle *mstyle;
} BiffXFData;

#endif
