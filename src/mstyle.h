#ifndef _GNM_MSTYLE_H_
# define _GNM_MSTYLE_H_

#include <gnumeric.h>
#include <style.h>

G_BEGIN_DECLS

/*
 * Keep element_size up to date.
 * Keep header_style_equal when adding new types that would create an implicit header
 * when sorting
 */
typedef enum {
	/* Types that are visible in blank cells */
		MSTYLE_COLOR_BACK,		/* marks a header */
		MSTYLE_COLOR_PATTERN,		/* marks a header */

	        MSTYLE_BORDER_TOP,
	        MSTYLE_BORDER_BOTTOM,		/* marks a vertical header */
	        MSTYLE_BORDER_LEFT,
	        MSTYLE_BORDER_RIGHT,		/* marks a horizontal header */
	        MSTYLE_BORDER_REV_DIAGONAL,
	        MSTYLE_BORDER_DIAGONAL,

		MSTYLE_PATTERN,			/* marks a header */
	/* Delimiter */
	MSTYLE_ELEMENT_MAX_BLANK = MSTYLE_PATTERN,

	/* Normal types */
	        MSTYLE_FONT_COLOR,		/* marks a header */
		MSTYLE_FONT_NAME,		/* marks a header */
		MSTYLE_FONT_BOLD,		/* marks a header */
		MSTYLE_FONT_ITALIC,		/* marks a header */
		MSTYLE_FONT_UNDERLINE,		/* marks a header */
		MSTYLE_FONT_STRIKETHROUGH,	/* marks a header */
		MSTYLE_FONT_SCRIPT,		/* marks a header */
	        MSTYLE_FONT_SIZE,		/* marks a header */

		MSTYLE_FORMAT,			/* marks a header */

	        MSTYLE_ALIGN_V,			/* marks a header */
	        MSTYLE_ALIGN_H,			/* marks a header */
	        MSTYLE_INDENT,			/* marks a header */
		MSTYLE_ROTATION,		/* marks a header */
		MSTYLE_TEXT_DIR,		/* marks a header */
		MSTYLE_WRAP_TEXT,		/* marks a header */
		MSTYLE_SHRINK_TO_FIT,		/* marks a header */

	        MSTYLE_CONTENTS_LOCKED,
	        MSTYLE_CONTENTS_HIDDEN,

	/* Things not in MS Excel's Style */
	        MSTYLE_VALIDATION,
	        MSTYLE_HLINK,		/* patch equal_XL if this is changed */
	        MSTYLE_INPUT_MSG,	/* patch equal_XL if this is changed */
	        MSTYLE_CONDITIONS,	/* patch equal_XL if this is changed */
	/* Delimiter */
	MSTYLE_ELEMENT_MAX
} GnmStyleElement;

GType       gnm_style_get_type      (void);
GnmStyle   *gnm_style_new           (void);
GnmStyle   *gnm_style_new_default   (void);
GnmStyle   *gnm_style_new_merged    (GnmStyle const *base, GnmStyle const *overlay);
GnmStyle   *gnm_style_dup	    (GnmStyle const *src);
void        gnm_style_merge	    (GnmStyle *base, GnmStyle const *overlay);
void        gnm_style_merge_element (GnmStyle *dst, GnmStyle const *src,
				     GnmStyleElement elem);
GnmStyle   *gnm_style_ref           (GnmStyle const *style);
void        gnm_style_unref         (GnmStyle const *style);

GnmStyle   *gnm_style_link_sheet    (GnmStyle *style, Sheet *sheet);
void        gnm_style_link          (GnmStyle *style);
void        gnm_style_unlink        (GnmStyle *style);
void        gnm_style_abandon_link  (GnmStyle *style);

gboolean    gnm_style_eq            (GnmStyle const *a, GnmStyle const *b);
gboolean    gnm_style_equal         (GnmStyle const *a, GnmStyle const *b);
gboolean    gnm_style_equal_XL	    (GnmStyle const *a, GnmStyle const *b);
gboolean    gnm_style_equal_header  (GnmStyle const *a, GnmStyle const *b,
				     gboolean top);
gboolean    gnm_style_equal_elem    (GnmStyle const *a, GnmStyle const *b, GnmStyleElement e);

guint       gnm_style_hash          (gconstpointer style);
guint       gnm_style_hash_XL	    (gconstpointer style);

int         gnm_style_cmp           (GnmStyle const *a, GnmStyle const *b);

unsigned int gnm_style_find_conflicts      (GnmStyle *accum, GnmStyle const *overlay,
					    unsigned int conflicts);
unsigned int gnm_style_find_differences    (GnmStyle const *a, GnmStyle const *b,
					    gboolean relax_sheet);

gboolean     gnm_style_is_complete	   (GnmStyle const *style);
gboolean     gnm_style_is_element_set	   (GnmStyle const *style, GnmStyleElement elem);
void         gnm_style_unset_element	   (GnmStyle *style, GnmStyleElement elem);
void         gnm_style_set_font_color	   (GnmStyle *style, GnmColor *col);
void         gnm_style_set_back_color	   (GnmStyle *style, GnmColor *col);
void         gnm_style_set_pattern_color   (GnmStyle *style, GnmColor *col);
GnmColor    *gnm_style_get_font_color	   (GnmStyle const *style);
GnmColor    *gnm_style_get_back_color	   (GnmStyle const *style);
GnmColor    *gnm_style_get_pattern_color   (GnmStyle const *style);
void         gnm_style_set_border	   (GnmStyle *style, GnmStyleElement elem,
					    GnmBorder *border);
GnmBorder   *gnm_style_get_border	   (GnmStyle const *style, GnmStyleElement elem);
void         gnm_style_set_pattern	   (GnmStyle *style, int pattern);
int          gnm_style_get_pattern	   (GnmStyle const *style);
void         gnm_style_set_font_name	   (GnmStyle *style, char const *name);
char const  *gnm_style_get_font_name	   (GnmStyle const *style);
void         gnm_style_set_font_bold	   (GnmStyle *style, gboolean bold);
gboolean     gnm_style_get_font_bold	   (GnmStyle const *style);
void         gnm_style_set_font_italic	   (GnmStyle *style, gboolean italic);
gboolean     gnm_style_get_font_italic	   (GnmStyle const *style);
void         gnm_style_set_font_uline	   (GnmStyle *style, GnmUnderline ul);
GnmUnderline gnm_style_get_font_uline	   (GnmStyle const *style);
void         gnm_style_set_font_strike	   (GnmStyle *style, gboolean strike);
gboolean     gnm_style_get_font_strike	   (GnmStyle const *style);
void         gnm_style_set_font_script	   (GnmStyle *style, GOFontScript script);
GOFontScript gnm_style_get_font_script	   (GnmStyle const *style);
void         gnm_style_set_font_size	   (GnmStyle *style, double size);
double       gnm_style_get_font_size	   (GnmStyle const *style);

GnmFont     *gnm_style_get_font		   (GnmStyle const *style,
					    PangoContext *context);
void         gnm_style_set_format	   (GnmStyle *style, GOFormat const *fmt);
void         gnm_style_set_format_text	   (GnmStyle *style, char const *fmt);
GOFormat const*gnm_style_get_format	   (GnmStyle const *style);
void         gnm_style_set_align_h	   (GnmStyle *style, GnmHAlign a);
GnmHAlign    gnm_style_get_align_h	   (GnmStyle const *style);
void         gnm_style_set_align_v	   (GnmStyle *style, GnmVAlign a);
GnmVAlign    gnm_style_get_align_v	   (GnmStyle const *style);
void         gnm_style_set_indent	   (GnmStyle *style, int i);
int	     gnm_style_get_indent	   (GnmStyle const *style);

/* -1 == vertical, 0..359 == rotation */
void		 gnm_style_set_rotation	   (GnmStyle *style, int r);
int		 gnm_style_get_rotation    (GnmStyle const *style);
void		 gnm_style_set_text_dir    (GnmStyle *style, GnmTextDir text_dir);
GnmTextDir	 gnm_style_get_text_dir	   (GnmStyle const *style);

void		 gnm_style_set_wrap_text   (GnmStyle *style, gboolean f);
gboolean	 gnm_style_get_wrap_text   (GnmStyle const *style);
gboolean	 gnm_style_get_effective_wrap_text   (GnmStyle const *style);
void		 gnm_style_set_shrink_to_fit (GnmStyle *style, gboolean f);
gboolean	 gnm_style_get_shrink_to_fit (GnmStyle const *style);

void		 gnm_style_set_contents_locked (GnmStyle *style, gboolean f);
gboolean	 gnm_style_get_contents_locked (GnmStyle const *style);
void		 gnm_style_set_contents_hidden (GnmStyle *style, gboolean f);
gboolean	 gnm_style_get_contents_hidden (GnmStyle const *style);

void		 gnm_style_set_validation	(GnmStyle *style, GnmValidation *v);
GnmValidation const *
		 gnm_style_get_validation	(GnmStyle const *style);

void		 gnm_style_set_hlink		(GnmStyle *style, GnmHLink *lnk);
GnmHLink	*gnm_style_get_hlink		(GnmStyle const *style);

void		 gnm_style_set_input_msg	(GnmStyle *style, GnmInputMsg *msg);
GnmInputMsg	*gnm_style_get_input_msg	(GnmStyle const *style);

void		 gnm_style_set_conditions	(GnmStyle *style, GnmStyleConditions *sc);
GnmStyleConditions *gnm_style_get_conditions	(GnmStyle const *style);
GnmStyle const * gnm_style_get_cond_style       (GnmStyle const *style, int ix);

void             gnm_style_link_dependents      (GnmStyle *style,
						 GnmRange const *r);
void             gnm_style_unlink_dependents    (GnmStyle *style,
						 GnmRange const *r);

gboolean	 gnm_style_visible_in_blank (GnmStyle const *style);

PangoAttrList	*gnm_style_generate_attrs_full (GnmStyle const *style);
PangoAttrList	*gnm_style_get_pango_attrs     (GnmStyle const *style,
						PangoContext *context,
						double zoom);
int              gnm_style_get_pango_height    (GnmStyle const *style,
						PangoContext *context,
						double zoom);
void	    gnm_style_set_from_pango_attribute (GnmStyle *style,
						PangoAttribute const *attr);

void        gnm_style_init (void);
void        gnm_style_shutdown (void);

/* debug util */
void gnm_style_dump (GnmStyle const *style);

G_END_DECLS

#endif /* _GNM_MSTYLE_H_ */
