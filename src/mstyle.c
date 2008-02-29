/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnm-style.c: Storing a style
 *
 * Authors:
 *   Michael Meeks <mmeeks@gnu.org>
 *   Almer S. Tigelaar <almer@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Morten Welinder <terra@gnome.org>
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "style.h"

#include "gnm-style-impl.h"
#include "sheet-style.h"
#include "style-conditions.h"
#include "application.h"
#include "gutils.h"
#include "gnumeric-gconf.h"
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-font.h>

#define DEBUG_STYLES
#ifndef USE_MSTYLE_POOL
#define USE_MSTYLE_POOL 1
#endif

#if USE_MSTYLE_POOL
/* Memory pool for GnmStyles.  */
static GOMemChunk *gnm_style_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_ALLOC0(T,p) ((T*)go_mem_chunk_alloc0 (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_ALLOC0(T,c) g_new0 (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

static char const * const
gnm_style_element_name[MSTYLE_ELEMENT_MAX] = {
	"Color.Back",
	"Color.Pattern",
	"Border.Top",
	"Border.Bottom",
	"Border.Left",
	"Border.Right",
	"Border.RevDiagonal",
	"Border.Diagonal",
	"Pattern",
	"Color.Fore",
	"Font.Name",
	"Font.Bold",
	"Font.Italic",
	"Font.Underline",
	"Font.Strikethrough",
	"Font.Script",
	"Font.Size",
	"Format",
	"Align.v",
	"Align.h",
	"Indent",
	"Rotation",
	"WrapText",
	"ShrinkToFit",
	"Contents.Locked",
	"Contents.Hidden",
	"Validation",
	"Hyper Link",
	"Input Msg"
};

/* Some ref/link count debugging */
#if 0
#define d(arg)	printf arg
#else
#define d(arg)	do { } while (0)
#endif

static void
clear_conditional_merges (GnmStyle *style)
{
	if (style->cond_styles) {
		unsigned i = style->cond_styles->len;
		while (i-- > 0)
			gnm_style_unref (g_ptr_array_index (style->cond_styles, i));
		g_ptr_array_free (style->cond_styles, TRUE);
		style->cond_styles = NULL;
	}
}

static void
gnm_style_update (GnmStyle *style)
{
	guint32 hash = 0;
	int i;

	g_return_if_fail (style->changed);

	style->changed = 0;

	clear_conditional_merges (style);
	if (style->conditions != NULL)
		style->cond_styles = gnm_style_conditions_overlay (style->conditions, style);

	if (!style->color.back->is_auto)
		hash ^= GPOINTER_TO_UINT (style->color.back);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	if (!style->color.pattern->is_auto)
		hash ^= GPOINTER_TO_UINT (style->color.pattern);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	if (!style->color.font->is_auto)
		hash ^= GPOINTER_TO_UINT (style->color.font);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));

	for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_DIAG; i++) {
		hash ^= GPOINTER_TO_UINT (style->borders[i]);
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->pattern;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= GPOINTER_TO_UINT (style->font_detail.name);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	if (style->font_detail.bold) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	if (style->font_detail.italic) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	hash ^= style->font_detail.underline;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	if (style->font_detail.strikethrough) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	hash ^= ((int)(style->font_detail.size * 97));
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= GPOINTER_TO_UINT (style->format);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->h_align;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->v_align;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->indent;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->rotation;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= style->text_dir;
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	if (style->wrap_text) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	if (style->shrink_to_fit) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	if (style->contents_locked) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	if (style->contents_hidden) {
		hash ^= 0x1379;
		hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	}
	style->hash_key_xl = hash;

	/* not in MS XL */
	hash ^= GPOINTER_TO_UINT (style->validation);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= GPOINTER_TO_UINT (style->hlink);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= GPOINTER_TO_UINT (style->input_msg);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	hash ^= GPOINTER_TO_UINT (style->conditions);
	hash = (hash << 7) ^ (hash >> (sizeof (hash) * 8 - 7));
	style->hash_key = hash;
}

guint
gnm_style_hash_XL (gconstpointer style)
{
	if (((GnmStyle const *)style)->changed)
		gnm_style_update ((GnmStyle *)style);
	return ((GnmStyle const *)style)->hash_key_xl;
}

guint
gnm_style_hash (gconstpointer style)
{
	if (((GnmStyle const *)style)->changed)
		gnm_style_update ((GnmStyle *)style);
	return ((GnmStyle const *)style)->hash_key;
}

static gboolean
elem_is_eq (GnmStyle const *a, GnmStyle const *b, GnmStyleElement elem)
{
	switch (elem) {
	case MSTYLE_COLOR_BACK :
		return a->color.back == b->color.back ||
			(a->color.back->is_auto && b->color.back->is_auto);
	case MSTYLE_COLOR_PATTERN :
		return a->color.pattern == b->color.pattern ||
			(a->color.pattern->is_auto && b->color.pattern->is_auto);
	case MSTYLE_ANY_BORDER:
		elem -= MSTYLE_BORDER_TOP;
		return a->borders[elem] == b->borders[elem];
	case MSTYLE_PATTERN:		return a->pattern == b->pattern;
	case MSTYLE_FONT_COLOR :
		return a->color.font == b->color.font ||
			(a->color.font->is_auto && b->color.font->is_auto);
	case MSTYLE_FONT_NAME:		return a->font_detail.name == b->font_detail.name;
	case MSTYLE_FONT_BOLD:		return a->font_detail.bold == b->font_detail.bold;
	case MSTYLE_FONT_ITALIC:	return a->font_detail.italic == b->font_detail.italic;
	case MSTYLE_FONT_UNDERLINE:	return a->font_detail.underline == b->font_detail.underline;
	case MSTYLE_FONT_STRIKETHROUGH:	return a->font_detail.strikethrough == b->font_detail.strikethrough;
	case MSTYLE_FONT_SCRIPT:	return a->font_detail.script == b->font_detail.script;
	case MSTYLE_FONT_SIZE:		return a->font_detail.size == b->font_detail.size;
	case MSTYLE_FORMAT:		return a->format == b->format;
	case MSTYLE_ALIGN_V:		return a->v_align == b->v_align;
	case MSTYLE_ALIGN_H:		return a->h_align == b->h_align;
	case MSTYLE_INDENT:		return a->indent == b->indent;
	case MSTYLE_ROTATION:		return a->rotation == b->rotation;
	case MSTYLE_TEXT_DIR:		return a->text_dir == b->text_dir;
	case MSTYLE_WRAP_TEXT:		return a->wrap_text == b->wrap_text;
	case MSTYLE_SHRINK_TO_FIT:	return a->shrink_to_fit == b->shrink_to_fit;
	case MSTYLE_CONTENTS_LOCKED:	return a->contents_locked == b->contents_locked;
	case MSTYLE_CONTENTS_HIDDEN:	return a->contents_hidden == b->contents_hidden;
	case MSTYLE_VALIDATION:		return a->validation == b->validation;
	case MSTYLE_HLINK:		return a->hlink == b->hlink;
	case MSTYLE_INPUT_MSG:		return a->input_msg == b->input_msg;
	case MSTYLE_CONDITIONS:		return a->conditions == b->conditions;
	default:
					return FALSE;
	}
}

static void
elem_assign_contents (GnmStyle *dst, GnmStyle const *src, GnmStyleElement elem)
{
#ifdef DEBUG_STYLES
	g_return_if_fail (src != dst);
	g_return_if_fail (elem_is_set (src, elem));
#endif
	switch (elem) {
	case MSTYLE_COLOR_BACK :	style_color_ref (dst->color.back = src->color.back); return;
	case MSTYLE_COLOR_PATTERN :	style_color_ref (dst->color.pattern = src->color.pattern); return;
	case MSTYLE_ANY_BORDER:
		elem -= MSTYLE_BORDER_TOP;
		gnm_style_border_ref (dst->borders[elem] = src->borders[elem]);
		return;
	case MSTYLE_PATTERN:		dst->pattern = src->pattern; return;
	case MSTYLE_FONT_COLOR :	style_color_ref (dst->color.font = src->color.font); return;
	case MSTYLE_FONT_NAME:		gnm_string_ref (dst->font_detail.name = src->font_detail.name); return;
	case MSTYLE_FONT_BOLD:		dst->font_detail.bold = src->font_detail.bold; return;
	case MSTYLE_FONT_ITALIC:	dst->font_detail.italic = src->font_detail.italic; return;
	case MSTYLE_FONT_UNDERLINE:	dst->font_detail.underline = src->font_detail.underline; return;
	case MSTYLE_FONT_STRIKETHROUGH: dst->font_detail.strikethrough = src->font_detail.strikethrough; return;
	case MSTYLE_FONT_SCRIPT:	dst->font_detail.script = src->font_detail.script; return;
	case MSTYLE_FONT_SIZE:		dst->font_detail.size = src->font_detail.size; return;
	case MSTYLE_FORMAT:		go_format_ref (dst->format = src->format); return;
	case MSTYLE_ALIGN_V:		dst->v_align = src->v_align; return;
	case MSTYLE_ALIGN_H:		dst->h_align = src->h_align; return;
	case MSTYLE_INDENT:		dst->indent = src->indent; return;
	case MSTYLE_ROTATION:		dst->rotation = src->rotation; return;
	case MSTYLE_TEXT_DIR:		dst->text_dir = src->text_dir; return;
	case MSTYLE_WRAP_TEXT:		dst->wrap_text = src->wrap_text; return;
	case MSTYLE_SHRINK_TO_FIT:	dst->shrink_to_fit = src->shrink_to_fit; return;
	case MSTYLE_CONTENTS_LOCKED:	dst->contents_locked = src->contents_locked; return;
	case MSTYLE_CONTENTS_HIDDEN:	dst->contents_hidden = src->contents_hidden; return;
	case MSTYLE_VALIDATION:
		if ((dst->validation = src->validation))
			validation_ref (dst->validation);
		return;
	case MSTYLE_HLINK:
		if ((dst->hlink = src->hlink))
			g_object_ref (G_OBJECT (dst->hlink));
		return;
	case MSTYLE_INPUT_MSG:
		if ((dst->input_msg = src->input_msg))
			g_object_ref (G_OBJECT (dst->input_msg));
		return;
	case MSTYLE_CONDITIONS:
		if ((dst->conditions = src->conditions))
			g_object_ref (G_OBJECT (dst->conditions));
		return;
	default:
		;
	}
}

static void
elem_clear_contents (GnmStyle *style, GnmStyleElement elem)
{
#ifdef DEBUG_STYLES
	g_return_if_fail (style != NULL);
#endif
	if (!elem_is_set (style, elem))
		return;

	switch (elem) {
	case MSTYLE_COLOR_BACK :	style_color_unref (style->color.back); return;
	case MSTYLE_COLOR_PATTERN :	style_color_unref (style->color.pattern); return;
	case MSTYLE_ANY_BORDER:
		gnm_style_border_unref (style->borders[elem - MSTYLE_BORDER_TOP]);
		return;
	case MSTYLE_FONT_COLOR :	style_color_unref (style->color.font); return;
	case MSTYLE_FONT_NAME:		gnm_string_unref (style->font_detail.name); return;
	case MSTYLE_FORMAT:		go_format_unref (style->format); return;
	case MSTYLE_VALIDATION:
		if (style->validation)
			validation_unref (style->validation);
		return;
	case MSTYLE_HLINK:
		if (style->hlink)
			g_object_unref (G_OBJECT (style->hlink));
		return;
	case MSTYLE_INPUT_MSG:
		if (style->input_msg)
			g_object_unref (G_OBJECT (style->input_msg));
		return;
	case MSTYLE_CONDITIONS:
		if (style->conditions) {
			clear_conditional_merges (style);
			g_object_unref (G_OBJECT (style->conditions));
		}
		return;
	default:
		;
	}
}

/**
 * gnm_style_find_conflicts :
 * @accum : accumulator #GnmStyle
 * @overlay : #GnmStyle
 * @conflicts : flags
 *
 * Copy any items from @overlay that do not conflict with the values in @accum.
 * If an element had a previous conflict (flagged via @conflicts) it is ignored.
 *
 * Returns @conflicts with any new conflicts added.
 **/
unsigned int
gnm_style_find_conflicts (GnmStyle *accum, GnmStyle const *overlay,
			  unsigned int conflicts)
{
	int i;

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (conflicts & (1 << i) ||
		    !elem_is_set (overlay, i))
			continue;
		if (!elem_is_set (accum, i)) {
			elem_assign_contents (accum, overlay, i);
			elem_set (accum, i);
			elem_changed (accum, i);
		} else if (!elem_is_eq (accum, overlay, i))
			conflicts |= (1 << i);
	}
	return conflicts;
}

static inline void
gnm_style_clear_pango (GnmStyle *style)
{
	if (style->pango_attrs) {
		pango_attr_list_unref (style->pango_attrs);
		style->pango_attrs = NULL;
	}
}


static inline void
gnm_style_clear_font (GnmStyle *style)
{
	if (style->font) {
		gnm_font_unref (style->font);
		style->font = NULL;
	}
}

/**
 * gnm_style_new :
 *
 * Caller is responsible for unrefing the result.
 *
 * Returns a new style with _no_ elements set.
 **/
GnmStyle *
gnm_style_new (void)
{
	GnmStyle *style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);

	style->ref_count = 1;
	style->link_count = 0;
	style->linked_sheet = NULL;
	style->pango_attrs = NULL;
	style->font = NULL;
	style->validation = NULL;

	style->set = style->changed = 0;
	style->validation = NULL;
	style->hlink = NULL;
	style->input_msg = NULL;
	style->conditions = NULL;

	d(("new %p\n", style));

	return style;
}

/**
 * gnm_style_new_default:
 *
 * Caller is responsible for unrefing the result.
 *
 * Return value: a new style initialized to the default state.
 **/
GnmStyle *
gnm_style_new_default (void)
{
	GnmStyle *new_style = gnm_style_new ();
	int i;

	gnm_style_set_font_name	  (new_style, gnm_app_prefs->default_font.name);
	gnm_style_set_font_size	  (new_style, gnm_app_prefs->default_font.size);
	gnm_style_set_font_bold	  (new_style, gnm_app_prefs->default_font.is_bold);
	gnm_style_set_font_italic (new_style, gnm_app_prefs->default_font.is_italic);

	gnm_style_set_format      (new_style, go_format_general ());
	gnm_style_set_align_v     (new_style, VALIGN_BOTTOM);
	gnm_style_set_align_h     (new_style, HALIGN_GENERAL);
	gnm_style_set_indent      (new_style, 0);
	gnm_style_set_rotation    (new_style, 0);
	gnm_style_set_text_dir    (new_style, GNM_TEXT_DIR_CONTEXT);
	gnm_style_set_wrap_text   (new_style, FALSE);
	gnm_style_set_shrink_to_fit (new_style, FALSE);
	gnm_style_set_contents_locked (new_style, TRUE);
	gnm_style_set_contents_hidden (new_style, FALSE);
	gnm_style_set_font_uline  (new_style, UNDERLINE_NONE);
	gnm_style_set_font_strike (new_style, FALSE);
	gnm_style_set_font_script (new_style, GO_FONT_SCRIPT_STANDARD);

	gnm_style_set_validation (new_style, NULL);
	gnm_style_set_hlink      (new_style, NULL);
	gnm_style_set_input_msg  (new_style, NULL);
	gnm_style_set_conditions (new_style, NULL);

	gnm_style_set_font_color (new_style, style_color_black ());
	gnm_style_set_back_color (new_style, style_color_white ());
	gnm_style_set_pattern_color (new_style, style_color_black ());

	for (i = MSTYLE_BORDER_TOP ; i <= MSTYLE_BORDER_DIAGONAL ; ++i)
		gnm_style_set_border (new_style, i,
			gnm_style_border_ref (gnm_style_border_none ()));
	gnm_style_set_pattern (new_style, 0);

	return new_style;
}

GnmStyle *
gnm_style_dup (GnmStyle const *src)
{
	GnmStyle *new_style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);
	int i;

	new_style->ref_count = 1;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (elem_is_set (src, i)) {
			elem_assign_contents (new_style, src, i);
			elem_set (new_style, i);
			elem_changed (new_style, i);
		}
	if ((new_style->pango_attrs = src->pango_attrs))
		pango_attr_list_ref (new_style->pango_attrs);
	if ((new_style->font = src->font)) {
		gnm_font_ref (new_style->font);
		new_style->font_zoom = src->font_zoom;
	}

	d(("dup %p\n", new_style));
	return new_style;
}

/**
 * gnm_style_new_merged :
 * @src     : #GnmStyle
 * @overlay : #GnmStyle
 *
 * A new GnmStyle that contains any elements of @overlay that are set, and uses
 * @src for anything that is not set in @overlay.
 *
 * Returns A ref to a new GnmStyle.
 **/
GnmStyle *
gnm_style_new_merged (GnmStyle const *base, GnmStyle const *overlay)
{
	GnmStyle *new_style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);
	int i;

	new_style->ref_count = 1;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		elem_assign_contents (new_style, elem_is_set (overlay, i) ? overlay : base, i);
		elem_set (new_style, i);
		elem_changed (new_style, i);
	}
	d(("copy merge %p\n", new_style));
	return new_style;
}

void
gnm_style_ref (GnmStyle const *style)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (style->ref_count > 0);

	((GnmStyle *)style)->ref_count++;
	d(("ref %p = %d\n", style, style->ref_count));
}

/**
 * gnm_style_unref :
 * @style : #GnmStyle const
 *
 * Unrefs and _potentially frees_ @style.
 * Takes a _const_ pointer to facilitate life cycles.  The const indicates that
 * the content can not be changed, mainly when handling styles that are in the
 * style hash.
 **/
void
gnm_style_unref (GnmStyle const *style)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (style->ref_count > 0);

	d(("unref %p = %d\n", style, style->ref_count-1));
	if (((GnmStyle *)style)->ref_count-- <= 1) {
		GnmStyle *unconst = (GnmStyle *)style;
		int i;

		g_return_if_fail (style->link_count == 0);
		g_return_if_fail (style->linked_sheet == NULL);

		for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
			elem_clear_contents (unconst, i);
		unconst->set = 0;
		clear_conditional_merges (unconst);
		gnm_style_clear_pango (unconst);
		gnm_style_clear_font (unconst);

		CHUNK_FREE (gnm_style_pool, unconst);
	}
}

/**
 * Replace auto pattern color in style with sheet's auto pattern color.
 * make_copy tells if we are allowed to modify the style in place or we must
 * make a copy first.
 */
static GnmStyle *
link_pattern_color (GnmStyle *style, GnmColor *auto_color, gboolean make_copy)
{
	GnmColor *pattern_color = style->color.pattern;

	if (pattern_color->is_auto && auto_color != pattern_color) {
		style_color_ref (auto_color);
		if (make_copy) {
			GnmStyle *orig = style;
			style = gnm_style_dup (style);
			gnm_style_unref (orig);
		}
		gnm_style_set_pattern_color (style, auto_color);
	}
	return style;
}

/**
 * Replace auto border colors in style with sheet's auto pattern
 * color. (pattern is *not* a typo.)
 * make_copy tells if we are allowed to modify the style in place or we must
 * make a copy first.
 *
 * FIXME: We conjecture that XL color 64 in border should change with the
 * pattern, but not color 127. That distinction is not yet represented in
 * our data structures.
 */
static GnmStyle *
link_border_colors (GnmStyle *style, GnmColor *auto_color, gboolean make_copy)
{
	int i;

	for (i = MSTYLE_BORDER_TOP ; i <= MSTYLE_BORDER_DIAGONAL ; ++i) {
		if (elem_is_set (style, i)) {
			GnmBorder *border =
				style->borders[i- MSTYLE_BORDER_TOP];
			GnmColor *color;

			if (!border)
				continue;

			color = border->color;
			if (color->is_auto && auto_color != color) {
				GnmBorder *new_border;
				GnmStyleBorderOrientation orientation;

				switch (i) {
				case MSTYLE_BORDER_LEFT:
				case MSTYLE_BORDER_RIGHT:
					orientation = GNM_STYLE_BORDER_VERTICAL;
					break;
				case MSTYLE_BORDER_REV_DIAGONAL:
				case MSTYLE_BORDER_DIAGONAL:
					orientation = GNM_STYLE_BORDER_DIAGONAL;
					break;
				case MSTYLE_BORDER_TOP:
				case MSTYLE_BORDER_BOTTOM:
				default:
					orientation = GNM_STYLE_BORDER_HORIZONTAL;
					break;
				}
				style_color_ref (auto_color);
				new_border = gnm_style_border_fetch (
					border->line_type, auto_color,
					orientation);

				if (make_copy) {
					GnmStyle *orig = style;
					style = gnm_style_dup (style);
					gnm_style_unref (orig);
					make_copy = FALSE;
				}
				gnm_style_set_border (style, i, new_border);
			}
		}
	}
	return style;
}

/**
 * gnm_style_link_sheet :
 * @style :
 * @sheet :
 *
 * ABSORBS a reference to the style and sets the link count to 1.
 *
 * Where auto pattern color occurs in the style (it may for pattern and
 * borders), it is replaced with the sheet's auto pattern color. We make
 * sure that we do not modify the style which was passed in to us, but also
 * that we don't copy more than once. The final argument to the
 * link_xxxxx_color functions tell whether or not to copy.
 */
GnmStyle *
gnm_style_link_sheet (GnmStyle *style, Sheet *sheet)
{
	GnmColor *auto_color;
	gboolean style_is_orig = TRUE;

	if (style->linked_sheet != NULL) {
		GnmStyle *orig = style;
		style = gnm_style_dup (style);
		gnm_style_unref (orig);
		style_is_orig = FALSE;

		/* safety test */
		g_return_val_if_fail (style->linked_sheet != sheet, style);
	}

	g_return_val_if_fail (style->link_count == 0, style);
	g_return_val_if_fail (style->linked_sheet == NULL, style);

	auto_color = sheet_style_get_auto_pattern_color (sheet);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		style = link_pattern_color (style, auto_color, style_is_orig);
	style = link_border_colors (style, auto_color, style_is_orig);
	style_color_unref (auto_color);

	style->linked_sheet = sheet;
	style->link_count = 1;

	d(("link sheet %p = 1\n", style));
	return style;
}

void
gnm_style_link (GnmStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	style->link_count++;
	d(("link %p = %d\n", style, style->link_count));
}

void
gnm_style_link_multiple (GnmStyle *style, int count)
{
	g_return_if_fail (style->link_count > 0);

	style->link_count += count;
	d(("multiple link %p + %d = %d\n", style, count, style->link_count));
}

void
gnm_style_unlink (GnmStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	d(("unlink %p = %d\n", style, style->link_count-1));
	if (style->link_count-- == 1) {
		sheet_style_unlink (style->linked_sheet, style);
		style->linked_sheet = NULL;
		gnm_style_unref (style);
	}
}

gboolean
gnm_style_equal (GnmStyle const *a, GnmStyle const *b)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;
	for (i = MSTYLE_COLOR_BACK; i < MSTYLE_ELEMENT_MAX; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	return TRUE;
}

gboolean
gnm_style_equal_XL (GnmStyle const *a, GnmStyle const *b)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;
	for (i = MSTYLE_COLOR_BACK; i < MSTYLE_VALIDATION; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	return TRUE;
}

/**
 * gnm_style_equal_header :
 * @a : #GnmStyle
 * @b : #GnmStyle
 * @top : is this a header vertically or horizontally
 *
 * Check to see if @a is different enough from @b to make us think that @a is
 * from a header.
 **/
gboolean
gnm_style_equal_header (GnmStyle const *a, GnmStyle const *b, gboolean top)
{
	int i = top ? MSTYLE_BORDER_BOTTOM : MSTYLE_BORDER_RIGHT;

	if (!elem_is_eq (a, b, i))
		return FALSE;
	for (i = MSTYLE_COLOR_BACK; i <= MSTYLE_COLOR_PATTERN ; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	for (i = MSTYLE_FONT_COLOR; i <= MSTYLE_SHRINK_TO_FIT ; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	return TRUE;
}


gboolean
gnm_style_is_element_set (GnmStyle const *style, GnmStyleElement elem)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (MSTYLE_COLOR_BACK <= elem && elem < MSTYLE_ELEMENT_MAX, FALSE);
	return elem_is_set (style, elem);
}

/**
 * gnm_style_is_complete :
 * @style : #GnmStyle
 *
 * Returns TRUE if all elements are set.
 **/
gboolean
gnm_style_is_complete (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);

	return style->set == ((1u << MSTYLE_ELEMENT_MAX) - 1);
}

void
gnm_style_unset_element (GnmStyle *style, GnmStyleElement elem)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (MSTYLE_COLOR_BACK <= elem && elem < MSTYLE_ELEMENT_MAX);

	if (elem_is_set (style, elem)) {
		elem_clear_contents (style, elem);
		elem_unset (style, elem);
	}
}

/**
 * gnm_style_merge :
 * @base    : #GnmStyle
 * @overlay : #GnmStyle
 *
 * Applies any elements of @overlay that are set to @base.
 **/
void
gnm_style_merge (GnmStyle *base, GnmStyle const *overlay)
{
	unsigned i;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (elem_is_set (overlay, i)) {
			elem_assign_contents (base, overlay, i);
			elem_changed (base, i);
		}
}

/**
 * gnm_style_merge_element:
 * @dst: Destination style
 * @src: Source style
 * @elem: Element to replace
 *
 * This function replaces element @elem in style @dst with element @elem
 * in style @src. (If element @elem was already set in style @dst then
 * the element will first be unset)
 **/
void
gnm_style_merge_element (GnmStyle *dst, GnmStyle const *src, GnmStyleElement elem)
{
	g_return_if_fail (src != NULL);
	g_return_if_fail (dst != NULL);
	g_return_if_fail (src != dst);

	if (elem_is_set (src, elem)) {
		elem_clear_contents (dst, elem);
		elem_assign_contents (dst, src, elem);
		elem_set (dst, elem);
		elem_changed (dst, elem);
	}
}

void
gnm_style_set_font_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_FONT_COLOR);
	if (elem_is_set (style, MSTYLE_FONT_COLOR))
		style_color_unref (style->color.font);
	else
		elem_set (style, MSTYLE_FONT_COLOR);
	elem_changed (style, MSTYLE_FONT_COLOR);
	style->color.font = col;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_set_back_color :
 * @style : #GnmStyle
 * @col : #GnmColor
 *
 * Assigns @col as the background of @style.
 *
 * NOTE : the background colour is only visibile if
 *	GnmStyle::pattern > 0
 **/
void
gnm_style_set_back_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_COLOR_BACK);
	if (elem_is_set (style, MSTYLE_COLOR_BACK))
		style_color_unref (style->color.back);
	else
		elem_set (style, MSTYLE_COLOR_BACK);
	style->color.back = col;
	gnm_style_clear_pango (style);
}
void
gnm_style_set_pattern_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_COLOR_PATTERN);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		style_color_unref (style->color.pattern);
	else
		elem_set (style, MSTYLE_COLOR_PATTERN);
	style->color.pattern = col;
	gnm_style_clear_pango (style);
}

GnmColor *
gnm_style_get_font_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_COLOR), NULL);
	return style->color.font;
}

GnmColor *
gnm_style_get_back_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_COLOR_BACK), NULL);
	return style->color.back;
}

GnmColor *
gnm_style_get_pattern_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_COLOR_PATTERN), NULL);
	return style->color.pattern;
}

void
gnm_style_set_border (GnmStyle *style, GnmStyleElement elem,
		      GnmBorder *border)
{
	g_return_if_fail (style != NULL);

	/* NOTE : It is legal for border to be NULL */
	switch (elem) {
	case MSTYLE_ANY_BORDER:
		elem_changed (style, elem);
		elem_set (style, elem);
		elem -= MSTYLE_BORDER_TOP;
		if (style->borders[elem])
			gnm_style_border_unref (style->borders[elem]);
		style->borders[elem] = border;
		break;
	default:
		g_warning ("Not a border element");
		break;
	}

}

GnmBorder *
gnm_style_get_border (GnmStyle const *style, GnmStyleElement elem)
{
	g_return_val_if_fail (style != NULL, NULL);

	switch (elem) {
	case MSTYLE_ANY_BORDER:
		return style->borders[elem - MSTYLE_BORDER_TOP ];

	default:
		g_warning ("Not a border element");
		return NULL;
	}
}

void
gnm_style_set_pattern (GnmStyle *style, int pattern)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (pattern >= 0);
	g_return_if_fail (pattern <= GNUMERIC_SHEET_PATTERNS);

	elem_changed (style, MSTYLE_PATTERN);
	elem_set (style, MSTYLE_PATTERN);
	style->pattern = pattern;
}

int
gnm_style_get_pattern (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_PATTERN), 0);

	return style->pattern;
}

GnmFont *
gnm_style_get_font (GnmStyle const *style, PangoContext *context, float zoom)
{
	g_return_val_if_fail (style != NULL, NULL);

	if (!style->font || style->font_zoom != zoom) {
		char const *name;
		gboolean bold, italic;
		float size;

		gnm_style_clear_font ((GnmStyle *)style);

		if (elem_is_set (style, MSTYLE_FONT_NAME))
			name = gnm_style_get_font_name (style);
		else
			name = DEFAULT_FONT;

		if (elem_is_set (style, MSTYLE_FONT_BOLD))
			bold = gnm_style_get_font_bold (style);
		else
			bold = FALSE;

		if (elem_is_set (style, MSTYLE_FONT_ITALIC))
			italic = gnm_style_get_font_italic (style);
		else
			italic = FALSE;

		if (elem_is_set (style, MSTYLE_FONT_SIZE))
			size = gnm_style_get_font_size (style);
		else
			size = DEFAULT_SIZE;

		((GnmStyle *)style)->font =
			gnm_font_new (context, name, size,
				      zoom, bold, italic);
		((GnmStyle *)style)->font_zoom = zoom;
	}

	return style->font;
}

void
gnm_style_set_font_name (GnmStyle *style, char const *name)
{
	g_return_if_fail (name != NULL);
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_NAME);
	if (elem_is_set (style, MSTYLE_FONT_NAME))
		gnm_string_unref (style->font_detail.name);
	else
		elem_set (style, MSTYLE_FONT_NAME);
	style->font_detail.name = gnm_string_get (name);
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

char const *
gnm_style_get_font_name (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_NAME), NULL);

	return style->font_detail.name->str;
}

void
gnm_style_set_font_bold (GnmStyle *style, gboolean bold)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_BOLD);
	elem_set (style, MSTYLE_FONT_BOLD);
	style->font_detail.bold = bold;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

gboolean
gnm_style_get_font_bold (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_BOLD), FALSE);

	return style->font_detail.bold;
}

void
gnm_style_set_font_italic (GnmStyle *style, gboolean italic)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_ITALIC);
	elem_set (style, MSTYLE_FONT_ITALIC);
	style->font_detail.italic = italic;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

gboolean
gnm_style_get_font_italic (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_ITALIC), FALSE);

	return style->font_detail.italic;
}

void
gnm_style_set_font_uline (GnmStyle *style, GnmUnderline const underline)
{
	g_return_if_fail (style != NULL);
	elem_changed (style, MSTYLE_FONT_UNDERLINE);
	elem_set (style, MSTYLE_FONT_UNDERLINE);
	style->font_detail.underline = underline;
	gnm_style_clear_pango (style);
}

GnmUnderline
gnm_style_get_font_uline (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, UNDERLINE_NONE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_UNDERLINE), UNDERLINE_NONE);

	return style->font_detail.underline;
}

void
gnm_style_set_font_strike (GnmStyle *style, gboolean const strikethrough)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_STRIKETHROUGH);
	elem_set (style, MSTYLE_FONT_STRIKETHROUGH);
	style->font_detail.strikethrough = strikethrough;
	gnm_style_clear_pango (style);
}

gboolean
gnm_style_get_font_strike (GnmStyle const *style)
{
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_STRIKETHROUGH), FALSE);

	return style->font_detail.strikethrough;
}

void
gnm_style_set_font_script (GnmStyle *style, GOFontScript script)
{
	g_return_if_fail (style != NULL);
	elem_changed (style, MSTYLE_FONT_SCRIPT);
	elem_set (style, MSTYLE_FONT_SCRIPT);
	style->font_detail.script = script;
	gnm_style_clear_pango (style);
}

GOFontScript
gnm_style_get_font_script (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GO_FONT_SCRIPT_STANDARD);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_SCRIPT), GO_FONT_SCRIPT_STANDARD);

	return style->font_detail.script;
}

void
gnm_style_set_font_size (GnmStyle *style, float size)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (size >= 1.);
	elem_changed (style, MSTYLE_FONT_SIZE);
	elem_set (style, MSTYLE_FONT_SIZE);
	style->font_detail.size = size;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

float
gnm_style_get_font_size (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 12.0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_SIZE), 12.0);

	return style->font_detail.size;
}

void
gnm_style_set_format (GnmStyle *style, GOFormat *format)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	elem_changed (style, MSTYLE_FORMAT);
	go_format_ref (format);
	elem_clear_contents (style, MSTYLE_FORMAT);
	elem_set (style, MSTYLE_FORMAT);
	style->format = format;
}

/*
 * gnm_style_set_format_text:
 *
 * @style: mstyle to change.
 * @format: An *untranslated* format string.
 */
void
gnm_style_set_format_text (GnmStyle *style, char const *format)
{
	GOFormat *sf;

	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	sf = go_format_new_from_XL (format);
	gnm_style_set_format (style, sf);
	go_format_unref (sf);
}

GOFormat *
gnm_style_get_format (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FORMAT), NULL);

	return style->format;
}

void
gnm_style_set_align_h (GnmStyle *style, GnmHAlign a)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ALIGN_H);
	elem_set (style, MSTYLE_ALIGN_H);
	style->h_align = a;
}

GnmHAlign
gnm_style_get_align_h (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, HALIGN_LEFT);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_H), HALIGN_LEFT);

	return style->h_align;
}

void
gnm_style_set_align_v (GnmStyle *style, GnmVAlign a)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ALIGN_V);
	elem_set (style, MSTYLE_ALIGN_V);
	style->v_align = a;
}

GnmVAlign
gnm_style_get_align_v (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, VALIGN_TOP);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_V), VALIGN_TOP);

	return style->v_align;
}

void
gnm_style_set_indent (GnmStyle *style, int i)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_INDENT);
	elem_set (style, MSTYLE_INDENT);
	style->indent = i;
}

int
gnm_style_get_indent (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_INDENT), 0);

	return style->indent;
}

void
gnm_style_set_rotation (GnmStyle *style, int rot_deg)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ROTATION);
	elem_set (style, MSTYLE_ROTATION);
	style->rotation = rot_deg;
}

int
gnm_style_get_rotation (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ROTATION), 0);

	return style->rotation;
}

void
gnm_style_set_text_dir (GnmStyle *style, GnmTextDir text_dir)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_TEXT_DIR);
	elem_set (style, MSTYLE_TEXT_DIR);
	style->text_dir = text_dir;
}

GnmTextDir
gnm_style_get_text_dir (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GNM_TEXT_DIR_CONTEXT);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_TEXT_DIR), GNM_TEXT_DIR_CONTEXT);

	return style->text_dir;
}

void
gnm_style_set_wrap_text (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_WRAP_TEXT);
	elem_set (style, MSTYLE_WRAP_TEXT);
	style->wrap_text = f;
}

gboolean
gnm_style_get_wrap_text (GnmStyle const *style)
{
	g_return_val_if_fail (elem_is_set (style, MSTYLE_WRAP_TEXT), FALSE);

	return style->wrap_text;
}

/*
 * Same as gnm_style_get_wrap_text except that if either halign or valign
 * is _JUSTIFY, the result will be TRUE.
 */
gboolean
gnm_style_get_effective_wrap_text (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_WRAP_TEXT), FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_V), FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_H), FALSE);

	/* Note: HALIGN_GENERAL never expands to HALIGN_JUSTIFY.  */
	return (style->wrap_text ||
		style->v_align == VALIGN_JUSTIFY ||
		style->v_align == VALIGN_DISTRIBUTED ||
		style->h_align == HALIGN_JUSTIFY);
}

void
gnm_style_set_shrink_to_fit (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_SHRINK_TO_FIT);
	elem_set (style, MSTYLE_SHRINK_TO_FIT);
	style->shrink_to_fit = f;
}

gboolean
gnm_style_get_shrink_to_fit (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_SHRINK_TO_FIT), FALSE);

	return style->shrink_to_fit;
}

void
gnm_style_set_contents_locked (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_CONTENTS_LOCKED);
	elem_set (style, MSTYLE_CONTENTS_LOCKED);
	style->contents_locked = f;
}

gboolean
gnm_style_get_contents_locked (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONTENTS_LOCKED), FALSE);

	return style->contents_locked;
}

void
gnm_style_set_contents_hidden (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_CONTENTS_HIDDEN);
	elem_set (style, MSTYLE_CONTENTS_HIDDEN);
	style->contents_hidden = f;
}

gboolean
gnm_style_get_contents_hidden (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONTENTS_HIDDEN), FALSE);

	return style->contents_hidden;
}

void
gnm_style_set_validation (GnmStyle *style, GnmValidation *v)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_VALIDATION);
	elem_changed (style, MSTYLE_VALIDATION);
	elem_set (style, MSTYLE_VALIDATION);
	style->validation = v;
}

GnmValidation const *
gnm_style_get_validation (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_VALIDATION), NULL);

	return style->validation;
}

void
gnm_style_set_hlink (GnmStyle *style, GnmHLink *link)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_HLINK);
	elem_changed (style, MSTYLE_HLINK);
	elem_set (style, MSTYLE_HLINK);
	style->hlink = link;
}

GnmHLink *
gnm_style_get_hlink (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_HLINK), NULL);

	return style->hlink;
}

void
gnm_style_set_input_msg (GnmStyle *style, GnmInputMsg *msg)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_INPUT_MSG);
	elem_changed (style, MSTYLE_INPUT_MSG);
	elem_set (style, MSTYLE_INPUT_MSG);
	style->input_msg = msg;
}

GnmInputMsg *
gnm_style_get_input_msg (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_INPUT_MSG), NULL);

	return style->input_msg;
}

void
gnm_style_set_conditions (GnmStyle *style, GnmStyleConditions *sc)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_CONDITIONS);
	elem_changed (style, MSTYLE_CONDITIONS);
	elem_set (style, MSTYLE_CONDITIONS);
	style->conditions = sc;
}

GnmStyleConditions *
gnm_style_get_conditions (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONDITIONS), NULL);
	return style->conditions;
}

gboolean
gnm_style_visible_in_blank (GnmStyle const *style)
{
	GnmStyleElement i;

	g_return_val_if_fail (style != NULL, FALSE);

	if (elem_is_set (style, MSTYLE_PATTERN) &&
	    gnm_style_get_pattern (style) > 0)
		return TRUE;

	for (i = MSTYLE_BORDER_TOP ; i <= MSTYLE_BORDER_DIAGONAL ; ++i)
		if (elem_is_set (style, i) &&
		    gnm_style_border_visible_in_blank (gnm_style_get_border (style, i)))
			return TRUE;

	return FALSE;
}

static void
add_attr (PangoAttrList *attrs, PangoAttribute *attr)
{
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (attrs, attr);
}

/**
 * gnm_style_get_pango_attrs :
 * @style : #GnmStyle
 **/
PangoAttrList *
gnm_style_get_pango_attrs (GnmStyle const *style,
			   PangoContext *context,
			   float zoom)
{
	PangoAttrList *l;

	if (style->pango_attrs) {
		if (zoom == style->pango_attrs_zoom) {
			pango_attr_list_ref (style->pango_attrs);
			return style->pango_attrs;
		}
		pango_attr_list_unref (((GnmStyle *)style)->pango_attrs);
	}

	((GnmStyle *)style)->pango_attrs = l = pango_attr_list_new ();
	((GnmStyle *)style)->pango_attrs_zoom = zoom;
	((GnmStyle *)style)->pango_attrs_height = -1;

	/* Foreground colour.  */
	/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
	if (0) {
		GnmColor const *fore = style->color.font;
		add_attr (l, pango_attr_foreground_new (
			fore->gdk_color.red, fore->gdk_color.green, fore->gdk_color.blue));
	}

	/* Handle underlining.  */
	switch (gnm_style_get_font_uline (style)) {
	case UNDERLINE_SINGLE :
		add_attr (l, pango_attr_underline_new (PANGO_UNDERLINE_SINGLE));
		break;
	case UNDERLINE_DOUBLE :
		add_attr (l, pango_attr_underline_new (PANGO_UNDERLINE_DOUBLE));
		break;
	default :
		break;
	}

	if (gnm_style_get_font_strike (style))
		add_attr (l, pango_attr_strikethrough_new (TRUE));
	switch (gnm_style_get_font_script (style)) {
	default :
	case GO_FONT_SCRIPT_STANDARD :
		break;
	case GO_FONT_SCRIPT_SUB :
		add_attr (l, pango_attr_rise_new (-5000));
		zoom *= .5;
		break;
	case GO_FONT_SCRIPT_SUPER :
		add_attr (l, pango_attr_rise_new (5000));
		zoom *= .5;
	}

	{
		GnmFont *font = gnm_style_get_font (style, context, zoom);
		add_attr (l, pango_attr_font_desc_new (font->go.font->desc));
	}

	add_attr (l, pango_attr_scale_new (zoom));
	pango_attr_list_ref (l);
	return l;
}

PangoAttrList *
gnm_style_generate_attrs_full (GnmStyle const *style)
{
	GnmColor const *fore = style->color.font;
	PangoAttrList *l = pango_attr_list_new ();

	add_attr (l, pango_attr_family_new (gnm_style_get_font_name (style)));
	add_attr (l, pango_attr_size_new (gnm_style_get_font_size (style) * PANGO_SCALE));
	add_attr (l, pango_attr_style_new (gnm_style_get_font_italic (style)
		? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL));
	add_attr (l, pango_attr_weight_new (gnm_style_get_font_bold (style)
		? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL));
	add_attr (l, pango_attr_foreground_new (
		fore->gdk_color.red, fore->gdk_color.green, fore->gdk_color.blue));
	add_attr (l, pango_attr_strikethrough_new (gnm_style_get_font_strike (style)));
	switch (gnm_style_get_font_uline (style)) {
	case UNDERLINE_SINGLE :
		add_attr (l, pango_attr_underline_new (PANGO_UNDERLINE_SINGLE));
		break;
	case UNDERLINE_DOUBLE :
		add_attr (l, pango_attr_underline_new (PANGO_UNDERLINE_DOUBLE));
		break;
	default :
		add_attr (l, pango_attr_underline_new (PANGO_UNDERLINE_NONE));
		break;
	}

	return l;
}

int
gnm_style_get_pango_height (GnmStyle const *style,
			    PangoContext *context,
			    float zoom)
{
	PangoAttrList *attrs = gnm_style_get_pango_attrs (style, context, zoom);

	if (style->pango_attrs_height == -1) {
		int h;
		PangoLayout *layout = pango_layout_new (context);
		pango_layout_set_attributes (layout, attrs);
		pango_layout_set_text (layout, "+1.23456789E-01", -1);
		pango_layout_get_pixel_size (layout, NULL, &h);
		g_object_unref (layout);
		((GnmStyle *)style)->pango_attrs_height = h;
	}

	pango_attr_list_unref (attrs);
	return style->pango_attrs_height;
}


void
gnm_style_set_from_pango_attribute (GnmStyle *style, PangoAttribute const *attr)
{
	switch (attr->klass->type) {
	case PANGO_ATTR_FAMILY :
		gnm_style_set_font_name (style, ((PangoAttrString *)attr)->value);
		break;
	case PANGO_ATTR_SIZE :
		gnm_style_set_font_size (style,
			(float )(((PangoAttrInt *)attr)->value) / PANGO_SCALE);
		break;
	case PANGO_ATTR_STYLE :
		gnm_style_set_font_italic (style,
			((PangoAttrInt *)attr)->value == PANGO_STYLE_ITALIC);
		break;
	case PANGO_ATTR_WEIGHT :
		gnm_style_set_font_bold (style,
			((PangoAttrInt *)attr)->value >= PANGO_WEIGHT_BOLD);
		break;
	case PANGO_ATTR_FOREGROUND :
		gnm_style_set_font_color (style, style_color_new_pango (
			&((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_UNDERLINE :
		switch (((PangoAttrInt *)attr)->value) {
		case PANGO_UNDERLINE_NONE :
			gnm_style_set_font_uline (style, UNDERLINE_NONE);
			break;
		case PANGO_UNDERLINE_SINGLE :
			gnm_style_set_font_uline (style, UNDERLINE_SINGLE);
			break;
		case PANGO_UNDERLINE_DOUBLE :
			gnm_style_set_font_uline (style, UNDERLINE_DOUBLE);
			break;
		}
		break;
	case PANGO_ATTR_STRIKETHROUGH :
		gnm_style_set_font_strike (style,
			((PangoAttrInt *)attr)->value != 0);
		break;
	default :
		break; /* ignored */
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_style_dump_color (GnmColor *color, GnmStyleElement elem)
{
	if (color)
		g_printerr ("\t%s: %hx:%hx:%hx\n", gnm_style_element_name [elem],
			 color->gdk_color.red,
			 color->gdk_color.green,
			 color->gdk_color.blue);
	else
		g_printerr ("\t%s: (NULL)\n", gnm_style_element_name [elem]);
}

static void
gnm_style_dump_border (GnmBorder *border, GnmStyleElement elem)
{
	g_printerr ("\t%s: ", gnm_style_element_name[elem]);
	if (border)
		g_printerr ("%d\n", border->line_type);
	else
		g_printerr ("blank\n");
}

void
gnm_style_dump (GnmStyle const *style)
{
	int i;

	g_printerr ("Style Refs %d\n", style->ref_count);
	if (elem_is_set (style, MSTYLE_COLOR_BACK))
		gnm_style_dump_color (style->color.back, MSTYLE_COLOR_BACK);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		gnm_style_dump_color (style->color.pattern, MSTYLE_COLOR_PATTERN);

	for (i = MSTYLE_BORDER_TOP ; i <= MSTYLE_BORDER_DIAGONAL ; ++i)
		if (elem_is_set (style, i))
			gnm_style_dump_border (style->borders[i-MSTYLE_BORDER_TOP], i);

	if (elem_is_set (style, MSTYLE_PATTERN))
		g_printerr ("\tpattern %d\n", style->pattern);
	if (elem_is_set (style, MSTYLE_FONT_COLOR))
		gnm_style_dump_color (style->color.font, MSTYLE_FONT_COLOR);
	if (elem_is_set (style, MSTYLE_FONT_NAME))
		g_printerr ("\tname '%s'\n", style->font_detail.name->str);
	if (elem_is_set (style, MSTYLE_FONT_BOLD))
		g_printerr (style->font_detail.bold ? "\tbold\n" : "\tnot bold\n");
	if (elem_is_set (style, MSTYLE_FONT_ITALIC))
		g_printerr (style->font_detail.italic ? "\titalic\n" : "\tnot italic\n");
	if (elem_is_set (style, MSTYLE_FONT_UNDERLINE))
		switch (style->font_detail.underline) {
		default :
		case UNDERLINE_NONE :
			g_printerr ("\tno underline\n"); break;
		case UNDERLINE_SINGLE :
			g_printerr ("\tsingle underline\n"); break;
		case UNDERLINE_DOUBLE :
			g_printerr ("\tdouble underline\n"); break;
		}
	if (elem_is_set (style, MSTYLE_FONT_STRIKETHROUGH))
		g_printerr (style->font_detail.strikethrough ? "\tstrikethrough\n" : "\tno strikethrough\n");
	if (elem_is_set (style, MSTYLE_FONT_SCRIPT))
		switch (style->font_detail.script) {
		case GO_FONT_SCRIPT_SUB :
			g_printerr ("\tsubscript\n"); break;
		default :
		case GO_FONT_SCRIPT_STANDARD :
			g_printerr ("\tno super or sub\n"); break;
		case GO_FONT_SCRIPT_SUPER :
			g_printerr ("\tsuperscript\n"); break;
		}
	if (elem_is_set (style, MSTYLE_FONT_SIZE))
		g_printerr ("\tsize %f\n", style->font_detail.size);
	if (elem_is_set (style, MSTYLE_FORMAT)) {
		const char *fmt = go_format_as_XL (style->format);
		g_printerr ("\tformat '%s'\n", fmt);
	}
	if (elem_is_set (style, MSTYLE_ALIGN_V))
		g_printerr ("\tvalign %hd\n", style->v_align);
	if (elem_is_set (style, MSTYLE_ALIGN_H))
		g_printerr ("\thalign %hd\n", style->h_align);
	if (elem_is_set (style, MSTYLE_INDENT))
		g_printerr ("\tindent %d\n", style->indent);
	if (elem_is_set (style, MSTYLE_ROTATION))
		g_printerr ("\trotation %d\n", style->rotation);
	if (elem_is_set (style, MSTYLE_TEXT_DIR))
		g_printerr ("\ttext dir %d\n", style->text_dir);
	if (elem_is_set (style, MSTYLE_WRAP_TEXT))
		g_printerr ("\twrap text %d\n", style->wrap_text);
	if (elem_is_set (style, MSTYLE_SHRINK_TO_FIT))
		g_printerr ("\tshrink to fit %d\n", style->shrink_to_fit);
	if (elem_is_set (style, MSTYLE_CONTENTS_LOCKED))
		g_printerr ("\tlocked %d\n", style->contents_locked);
	if (elem_is_set (style, MSTYLE_CONTENTS_HIDDEN))
		g_printerr ("\thidden %d\n", style->contents_hidden);
	if (elem_is_set (style, MSTYLE_VALIDATION))
		g_printerr ("\tvalidation %p\n", style->validation);
	if (elem_is_set (style, MSTYLE_HLINK))
		g_printerr ("\thlink %p\n", style->hlink);
	if (elem_is_set (style, MSTYLE_INPUT_MSG))
		g_printerr ("\tinput msg %p\n", style->input_msg);
	if (elem_is_set (style, MSTYLE_CONDITIONS))
		g_printerr ("\tconditions %p\n", style->conditions);
}

/* ------------------------------------------------------------------------- */

void
gnm_style_init (void)
{
#if USE_MSTYLE_POOL
	gnm_style_pool =
		go_mem_chunk_new ("style pool",
				   sizeof (GnmStyle),
				   16 * 1024 - 128);
#endif
}

#if USE_MSTYLE_POOL
static void
cb_gnm_style_pool_leak (gpointer data, gpointer user)
{
	GnmStyle *style = data;
	g_printerr ("Leaking style at %p.\n", style);
	gnm_style_dump (style);
}
#endif

void
gnm_style_shutdown (void)
{
#if USE_MSTYLE_POOL
	go_mem_chunk_foreach_leak (gnm_style_pool, cb_gnm_style_pool_leak, NULL);
	go_mem_chunk_destroy (gnm_style_pool, FALSE);
	gnm_style_pool = NULL;
#endif
}
