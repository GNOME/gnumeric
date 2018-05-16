#ifndef _GNM_STYLE_H_
# define _GNM_STYLE_H_

#include <gnumeric.h>
#include <libgnumeric.h>
#include <pango/pango-context.h>

G_BEGIN_DECLS

#define DEFAULT_FONT "Sans"
#define DEFAULT_SIZE 10.0

/* Alignment definitions */
/* Do not change these flags they are used as keys in the 1.0.x xml format.  */
typedef enum {
	GNM_HALIGN_GENERAL =  0x01,
	GNM_HALIGN_LEFT    =  0x02,
	GNM_HALIGN_RIGHT   =  0x04,
	GNM_HALIGN_CENTER  =  0x08,
	GNM_HALIGN_FILL    =  0x10,
	GNM_HALIGN_JUSTIFY =  0x20,
	GNM_HALIGN_CENTER_ACROSS_SELECTION =  0x40,
	GNM_HALIGN_DISTRIBUTED = 0x80
} GnmHAlign;

typedef enum {
	GNM_VALIGN_TOP     = 1,
	GNM_VALIGN_BOTTOM  = 2,
	GNM_VALIGN_CENTER  = 4,
	GNM_VALIGN_JUSTIFY = 8,
	GNM_VALIGN_DISTRIBUTED = 16
} GnmVAlign;

typedef enum {
	UNDERLINE_NONE   = 0,
	UNDERLINE_SINGLE = 1,
	UNDERLINE_DOUBLE = 2,
	UNDERLINE_SINGLE_LOW = 3,
	UNDERLINE_DOUBLE_LOW = 4
} GnmUnderline;

typedef enum {
	GNM_TEXT_DIR_RTL	= -1,
	GNM_TEXT_DIR_CONTEXT	=  0,
	GNM_TEXT_DIR_LTR	=  1
} GnmTextDir;

#include <mstyle.h>

GType gnm_align_h_get_type (void);
#define GNM_ALIGN_H_TYPE (gnm_align_h_get_type ())

GType gnm_align_v_get_type (void);
#define GNM_ALIGN_V_TYPE (gnm_align_v_get_type ())

GnmSpanCalcFlags gnm_style_required_spanflags (GnmStyle const *style);
GnmHAlign	 gnm_style_default_halign     (GnmStyle const *style,
					       GnmCell const *c);
PangoUnderline   gnm_translate_underline_to_pango (GnmUnderline ul);
GnmUnderline   gnm_translate_underline_from_pango (PangoUnderline pul);

G_END_DECLS

#endif /* _GNM_STYLE_H_ */
