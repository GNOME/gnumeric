#ifndef GNUMERIC_MSTYLE_H
#define GNUMERIC_MSTYLE_H

#include "gnumeric.h"
#include "style.h"
#include "style-condition.h"

/*
 * Keep element_size up to date.
 */
typedef enum _MStyleElementType {
	/* Delimiter */
	MSTYLE_ELEMENT_UNSET = 0,
	/* When there is a conflict in a merge */
	MSTYLE_ELEMENT_CONFLICT,
	/* Types that are visible in blank cells */
		MSTYLE_COLOR_BACK,
		MSTYLE_COLOR_PATTERN,

	        MSTYLE_BORDER_TOP,
	        MSTYLE_BORDER_BOTTOM,
	        MSTYLE_BORDER_LEFT,
	        MSTYLE_BORDER_RIGHT,
	        MSTYLE_BORDER_REV_DIAGONAL,
	        MSTYLE_BORDER_DIAGONAL,

		MSTYLE_PATTERN,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX_BLANK,
	/* Normal types */
	        MSTYLE_COLOR_FORE,
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
		MSTYLE_FONT_UNDERLINE,
		MSTYLE_FONT_STRIKETHROUGH,
	        MSTYLE_FONT_SIZE,

		MSTYLE_FORMAT,

	        MSTYLE_ALIGN_V,
	        MSTYLE_ALIGN_H,
	        MSTYLE_INDENT,

		MSTYLE_ORIENTATION,

		MSTYLE_WRAP_TEXT,

	        MSTYLE_VALIDATION,
	        MSTYLE_VALIDATION_STYLE,
	        MSTYLE_VALIDATION_MSG,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX
} MStyleElementType;

MStyle     *mstyle_new           (void);
MStyle     *mstyle_new_default   (void);
MStyle     *mstyle_copy          (const MStyle *st);
MStyle	   *mstyle_copy_merge	 (const MStyle *orig, const MStyle *overlay);
void        mstyle_ref           (MStyle *st);
void        mstyle_ref_multiple  (MStyle *st, int count);
void        mstyle_unref         (MStyle *st);
void        mstyle_destroy       (MStyle *st);
gboolean    mstyle_equal         (const MStyle *a, const MStyle *b);
gboolean    mstyle_verify        (const MStyle *st);
guint       mstyle_hash          (gconstpointer st);
gboolean    mstyle_empty         (const MStyle *st);

/*
 * Wafer thin element access functions.
 */
gboolean            mstyle_is_element_set  (const MStyle *st, MStyleElementType t);
gboolean            mstyle_is_element_conflict (const MStyle *st, MStyleElementType t);
void                mstyle_compare             (MStyle *a, const MStyle *b);
void                mstyle_unset_element   (MStyle *st, MStyleElementType t);
void                mstyle_replace_element (MStyle *src, MStyle *dst, MStyleElementType t);
void                mstyle_set_color       (MStyle *st, MStyleElementType t,
					    StyleColor *col);
StyleColor         *mstyle_get_color       (const MStyle *st, MStyleElementType t);
void                mstyle_set_border      (MStyle *st, MStyleElementType t,
					    StyleBorder *border);
StyleBorder	   *mstyle_get_border      (const MStyle *st, MStyleElementType t);
void                mstyle_set_pattern     (MStyle *st, int pattern);
int                 mstyle_get_pattern     (const MStyle *st);
void                mstyle_set_font_name   (MStyle *st, const char *name);
const char         *mstyle_get_font_name   (const MStyle *st);
void                mstyle_set_font_bold   (MStyle *st, gboolean bold);
gboolean            mstyle_get_font_bold   (const MStyle *st);
void                mstyle_set_font_italic (MStyle *st, gboolean italic);
gboolean            mstyle_get_font_italic (const MStyle *st);
void                mstyle_set_font_uline  (MStyle *st, StyleUnderlineType const t);
StyleUnderlineType  mstyle_get_font_uline  (const MStyle *st);
void                mstyle_set_font_strike (MStyle *st, gboolean strikethrough);
gboolean            mstyle_get_font_strike (const MStyle *st);
void                mstyle_set_font_size   (MStyle *st, double size);
double              mstyle_get_font_size   (const MStyle *st);

/* this font must be unrefd after use */
StyleFont          *mstyle_get_font        (const MStyle *st, double zoom);
void                mstyle_set_format      (MStyle *st, StyleFormat *);
void                mstyle_set_format_text (MStyle *st, const char *format);
StyleFormat        *mstyle_get_format      (const MStyle *st);
void                mstyle_set_align_h     (MStyle *st, StyleHAlignFlags a);
StyleHAlignFlags    mstyle_get_align_h     (const MStyle *st);
void                mstyle_set_align_v     (MStyle *st, StyleVAlignFlags a);
StyleVAlignFlags    mstyle_get_align_v     (const MStyle *st);
void                mstyle_set_indent	   (MStyle *st, int i);
int		    mstyle_get_indent	   (const MStyle *st);
void                mstyle_set_orientation (MStyle *st, StyleOrientation o);
StyleOrientation    mstyle_get_orientation (const MStyle *st);
void                mstyle_set_wrap_text   (MStyle *st, gboolean f);
gboolean            mstyle_get_wrap_text   (const MStyle *st);

void                mstyle_set_validation       (MStyle *st, StyleCondition *sc);
StyleCondition     *mstyle_get_validation       (const MStyle *st);
void                mstyle_set_validation_style (MStyle *st, ValidationStyle vs);
ValidationStyle     mstyle_get_validation_style (const MStyle *st);
void                mstyle_set_validation_msg   (MStyle *st, const char *msg);
const char         *mstyle_get_validation_msg   (const MStyle *st);

gboolean            mstyle_visible_in_blank(const MStyle *st);

MStyle     *mstyle_merge       (const MStyle *master, MStyle *slave);
char       *mstyle_to_string   (const MStyle *st); /* Debug only ! leaks like a sieve */
void        mstyle_dump        (const MStyle *st);

#endif /* GNUMERIC_MSTYLE_H */
