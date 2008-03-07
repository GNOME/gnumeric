/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_IMPL_H_
# define _GNM_STYLE_IMPL_H_

#include "str.h"
#include "style-border.h"
#include "style-color.h"
#include "style-font.h"
#include "validation.h"
#include "pattern.h"
#include <goffice/utils/go-format.h>

G_BEGIN_DECLS

struct _GnmStyle {
	unsigned int	changed;
	unsigned int	set;

	unsigned int    hash_key;
	unsigned int    hash_key_xl;
	unsigned int    ref_count;
	unsigned int    link_count;
	Sheet	       *linked_sheet;

	PangoAttrList *pango_attrs;
	float          pango_attrs_zoom;
	int            pango_attrs_height;
	GnmFont       *font;

/* public */
	struct {
		GnmColor *font;
		GnmColor *back;
		GnmColor *pattern;
	}  color;
	GnmBorder	*borders [GNM_STYLE_BORDER_DIAG + 1];
	guint32          pattern;

#warning TODO use GOFont
	struct {
		GnmString *name;
		gboolean	bold;
		gboolean	italic;
		GnmUnderline	underline;
		gboolean	strikethrough;
		GOFontScript	script;
		float		size;
	} font_detail;
	float            font_zoom;

	GOFormat *format;
	GnmHAlign	 h_align;
	GnmVAlign	 v_align;
	int		 indent;
	int		 rotation;
	int		 text_dir;
	gboolean         wrap_text;
	gboolean         shrink_to_fit;
	gboolean         contents_locked;
	gboolean         contents_hidden;

	GnmValidation		*validation;
	GnmHLink		*hlink;
	GnmInputMsg		*input_msg;
	GnmStyleConditions	*conditions;
	GPtrArray		*cond_styles;
};

#define elem_changed(style, elem) { (style)->changed |= (1 << (elem)); }
#define elem_set(style, elem)	  { (style)->set |=  (1 << (elem)); }
#define elem_unset(style, elem)	  { (style)->set &= ~(1 << (elem)); }
#define elem_is_set(style, elem)  (((style)->set & (1 << (elem))) != 0)

#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL

G_END_DECLS

#endif /* _GNM_STYLE_IMPL_H_ */
