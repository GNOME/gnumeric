/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef PRESENT_TEXT_H
#define PRESENT_TEXT_H

/**
 * present-text.h: MS Office Graphic Object support
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *    Christopher James Lahey <clahey@ximian.com>
 *
 * (C) 1998-2003 Michael Meeks, Jody Goldberg, Chris Lahey
 **/

#include <glib-object.h>
#include <glib.h>
#include <goffice/drawing/god-text-model.h>

G_BEGIN_DECLS

#define PRESENT_TEXT_TYPE		(present_text_get_type ())
#define PRESENT_TEXT(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PRESENT_TEXT_TYPE, PresentText))
#define PRESENT_TEXT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), PRESENT_TEXT_TYPE, PresentTextClass))
#define PRESENT_TEXT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS((o), PRESENT_TEXT_TYPE, PresentTextClass))
#define IS_PRESENT_TEXT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PRESENT_TEXT_TYPE))
#define IS_PRESENT_TEXT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PRESENT_TEXT_TYPE))

typedef struct PresentTextPrivate_ PresentTextPrivate;

typedef struct {
	GodTextModel parent;
	PresentTextPrivate *priv;
} PresentText;

typedef struct {
	GodTextModelClass parent_class;
} PresentTextClass;

typedef enum {
	PRESENT_TEXT_TITLE = 0,
	PRESENT_TEXT_BODY = 1,
	PRESENT_TEXT_NOTES = 2,
	PRESENT_TEXT_OTHER = 4,
	PRESENT_TEXT_CENTER_BODY = 5,
	PRESENT_TEXT_CENTER_TITLE = 6,
	PRESENT_TEXT_HALF_BODY = 7,
	PRESENT_TEXT_QUARTER_BODY = 8
} PresentTextType;

GType            present_text_get_type       (void);
GodTextModel    *present_text_new            (int              id,
					      PresentTextType  type);
void             present_text_set            (PresentText     *text,
					      int              id,
					      PresentTextType  type);
int              present_text_get_text_id    (PresentText     *text);
PresentTextType  present_text_get_text_type  (PresentText     *text);


G_END_DECLS

#endif /* PRESENT_TEXT_H */
