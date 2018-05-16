/**
 * ms-excel-xf.h: MS Excel support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#ifndef GNM_MS_EXCEL_XF_H
#define GNM_MS_EXCEL_XF_H

#include <style.h>
#include <style-border.h>
#include <style-color.h>

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
	GOFormat const *style_format;
	gboolean   is_simple_format;

	gboolean hidden;
	gboolean locked;
	MsBiffXfType xftype;	/*  -- Very important field... */
	MsBiffFormat format;
	guint16 parentstyle;
	GnmHAlign halign;
	GnmVAlign valign;
	gboolean wrap_text;
	gboolean shrink_to_fit;
	int rotation;
	int indent;
	GnmTextDir text_dir;
	guint16 border_color[STYLE_ORIENT_MAX];
	GnmStyleBorderType border_type[STYLE_ORIENT_MAX];
	guint16 fill_pattern_idx;
	guint16 pat_foregnd_col;
	guint16 pat_backgnd_col;
	guint16 differences;

	GnmStyle *mstyle;
} BiffXFData;

#endif /* GNM_MS_EXCEL_XF_H */
