/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef GOD_TEXT_MODEL_H
#define GOD_TEXT_MODEL_H

/**
 * god-text-model.h: MS Office Graphic Object support
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

G_BEGIN_DECLS

#define GOD_TEXT_MODEL_TYPE		(god_text_model_get_type ())
#define GOD_TEXT_MODEL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GOD_TEXT_MODEL_TYPE, GodTextModel))
#define GOD_TEXT_MODEL_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GOD_TEXT_MODEL_TYPE, GodTextModelClass))
#define GOD_TEXT_MODEL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS((o), GOD_TEXT_MODEL_TYPE, GodTextModelClass))
#define IS_GOD_TEXT_MODEL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOD_TEXT_MODEL_TYPE))
#define IS_GOD_TEXT_MODEL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GOD_TEXT_MODEL_TYPE))

typedef struct GodTextModelPrivate_ GodTextModelPrivate;

typedef struct {
	GObject parent;
	GodTextModelPrivate *priv;
} GodTextModel;

typedef struct {
	GObjectClass parent_class;

	const char    *(*get_text)  (GodTextModel *text);
	void           (*set_text)  (GodTextModel *text, const char    *text_value);
} GodTextModelClass;

GType         god_text_model_get_type  (void);

GodTextModel *god_text_model_new       (void);
const char   *god_text_model_get_text  (GodTextModel *text);
void          god_text_model_set_text  (GodTextModel *text,
					const char   *text_value);



G_END_DECLS

#endif /* GOD_TEXT_MODEL_H */
