/**
 * ms-excel-xf.h: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1998, 1999 Michael Meeks, Jody Goldberg
 **/
#ifndef GNUMERIC_MS_EXCEL_XF_H
#define GNUMERIC_MS_EXCEL_XF_H

#include "border.h"

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
	eBiff_hidden hidden;
	eBiff_locked locked;
	eBiff_xftype xftype;	/*  -- Very important field... */
	eBiff_format format;
	guint16 parentstyle;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	gboolean wrap;
	guint8 rotation;
	StyleOrientation orientation;
	eBiff_eastern eastern;
	guint8 border_color[STYLE_ORIENT_MAX];
	StyleBorderType border_type[STYLE_ORIENT_MAX];
	guint8 fill_pattern_idx;
	guint8 pat_foregnd_col;
	guint8 pat_backgnd_col;
	guint16 differences;

	MStyle *mstyle[3];
} BiffXFData;
#endif
