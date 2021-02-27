/*
 * gnm-command-impl.h :
 *
 * Copyright (C) 1999-2008 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2002-2008 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#ifndef GNM_COMMAND_IMPL_H
#define GNM_COMMAND_IMPL_H

#include <gnumeric.h>
#include <gsf/gsf-impl-utils.h>
#include <glib-object.h>

G_BEGIN_DECLS
typedef struct {
	GObject parent;

	/* primary sheet associated with command, or NULL.  */
	Sheet *sheet;

	/* See truncate_undo_info.  */
	int size;

	/* A string to put in the menu */
	char const *cmd_descriptor;

	/* State of workbook before the commands was undo.  */
	guint64 state_before_do;
} GnmCommand;

typedef gboolean (* UndoCmd)   (GnmCommand *self, WorkbookControl *wbc);
typedef gboolean (* RedoCmd)   (GnmCommand *self, WorkbookControl *wbc);
typedef void	 (* RepeatCmd) (GnmCommand const *orig, WorkbookControl *wbc);

typedef struct {
	GObjectClass parent_class;

	UndoCmd		undo_cmd;
	RedoCmd		redo_cmd;
	RepeatCmd	repeat_cmd;
} GnmCommandClass;

GType	 gnm_command_get_type  (void);
void	 gnm_command_finalize  (GObject *obj);
gboolean gnm_command_push_undo (WorkbookControl *wbc, GObject *obj);

#define GNM_COMMAND_TYPE (gnm_command_get_type ())
#define MAKE_GNM_COMMAND(type, func, repeat)				\
static gboolean								\
func ## _undo (GnmCommand *me, WorkbookControl *wbc);			\
static gboolean								\
func ## _redo (GnmCommand *me, WorkbookControl *wbc);			\
static void								\
func ## _finalize (GObject *object);					\
static void								\
func ## _class_init (GnmCommandClass *parent)				\
{									\
	parent->undo_cmd   = (UndoCmd)& func ## _undo;			\
	parent->redo_cmd   = (RedoCmd)& func ## _redo;			\
	parent->repeat_cmd = repeat;					\
	parent->parent_class.finalize = & func ## _finalize;		\
}									\
typedef GnmCommandClass type ## Class;					\
static GSF_CLASS (type, func,						\
		  func ## _class_init, NULL, GNM_COMMAND_TYPE)

G_END_DECLS

#endif /* GNM_COMMAND_IMPL_H */
