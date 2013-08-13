/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_IMPL_H_
# define _GNM_STYLE_IMPL_H_

#include "style-border.h"
#include "style-color.h"
#include "style-font.h"
#include "validation.h"
#include "pattern.h"
#include <goffice/goffice.h>

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
	double         pango_attrs_zoom;
	int            pango_attrs_height;

	GnmFont       *font;
	PangoContext  *font_context;

/* public */
	struct _GnmStyleColor {
		GnmColor *font;
		GnmColor *back;
		GnmColor *pattern;
	} color;
	GnmBorder	*borders[MSTYLE_BORDER_DIAGONAL - MSTYLE_BORDER_TOP + 1];
	guint32          pattern;

	/* FIXME: TODO use GOFont */
	struct _GnmStyleFontDetails {
		GOString	*name;
		gboolean	bold;
		gboolean	italic;
		GnmUnderline	underline;
		gboolean	strikethrough;
		GOFontScript	script;
		double		size;
	} font_detail;

	GOFormat const *format;
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

	GPtrArray *deps;
};

#define elem_changed(style, elem) do { (style)->changed |= (1u << (elem)); } while(0)
#define elem_set(style, elem)	  do { (style)->set |=  (1u << (elem)); } while(0)
#define elem_unset(style, elem)	  do { (style)->set &= ~(1u << (elem)); } while(0)
#define elem_is_set(style, elem)  (((style)->set & (1u << (elem))) != 0)

#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL

G_END_DECLS

#endif /* _GNM_STYLE_IMPL_H_ */
