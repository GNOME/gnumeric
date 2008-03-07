/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STYLE_H_
# define _GNM_STYLE_H_

#include "gnumeric.h"
#include "libgnumeric.h"
#include <pango/pango-context.h>

G_BEGIN_DECLS

#define DEFAULT_FONT "Sans"
#define DEFAULT_SIZE 10.0

/* Alignment definitions */
/* Do not change these flags they are used as keys in the 1.0.x xml format.  */
typedef enum {
	HALIGN_GENERAL =  0x01,
	HALIGN_LEFT    =  0x02,
	HALIGN_RIGHT   =  0x04,
	HALIGN_CENTER  =  0x08,
	HALIGN_FILL    =  0x10,
	HALIGN_JUSTIFY =  0x20,
	HALIGN_CENTER_ACROSS_SELECTION =  0x40,
	HALIGN_DISTRIBUTED = 0x80
} GnmHAlign;

typedef enum {
	VALIGN_TOP     = 1,
	VALIGN_BOTTOM  = 2,
	VALIGN_CENTER  = 4,
	VALIGN_JUSTIFY = 8,
	VALIGN_DISTRIBUTED = 16
} GnmVAlign;

typedef enum {
	UNDERLINE_NONE   = 0,
	UNDERLINE_SINGLE = 1,
	UNDERLINE_DOUBLE = 2
} GnmUnderline;

typedef enum {
	GO_FONT_SCRIPT_SUB	= -1,
	GO_FONT_SCRIPT_STANDARD =  0,
	GO_FONT_SCRIPT_SUPER	=  1
} GOFontScript;

typedef enum {
	GNM_TEXT_DIR_RTL	= -1,
	GNM_TEXT_DIR_CONTEXT	=  0,
	GNM_TEXT_DIR_LTR	=  1
} GnmTextDir;

#include "mstyle.h"

GnmSpanCalcFlags gnm_style_required_spanflags (GnmStyle const *style);
GnmHAlign	 gnm_style_default_halign     (GnmStyle const *style,
					       GnmCell const *c);

G_END_DECLS

#endif /* _GNM_STYLE_H_ */
