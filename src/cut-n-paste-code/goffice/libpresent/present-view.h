/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef PRESENT_VIEW_H
#define PRESENT_VIEW_H

/**
 * present-view.h: MS Office Graphic Object support
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
#include <goffice/libpresent/present-presentation.h>
#include <gtk/gtkeventbox.h>

G_BEGIN_DECLS

#define PRESENT_VIEW_TYPE		(present_view_get_type ())
#define PRESENT_VIEW(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PRESENT_VIEW_TYPE, PresentView))
#define PRESENT_VIEW_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), PRESENT_VIEW_TYPE, PresentViewClass))
#define IS_PRESENT_VIEW(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PRESENT_VIEW_TYPE))
#define IS_PRESENT_VIEW_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PRESENT_VIEW_TYPE))

typedef struct PresentViewPrivate_ PresentViewPrivate;

typedef struct {
	GtkEventBox parent;
	PresentViewPrivate *priv;
} PresentView;

typedef struct {
	GtkEventBoxClass parent_class;
} PresentViewClass;

GType                present_view_get_type          (void);
PresentView         *present_view_new               (PresentPresentation *presentation);

/* Return value is reffed. */
PresentPresentation *present_view_get_presentation  (PresentView         *view);
void                 present_view_set_presentation  (PresentView         *view,
						     PresentPresentation *presentation);

G_END_DECLS

#endif /* PRESENT_VIEW_H */
