#ifndef GNUMERIC_COMMAND_CONTEXT_GUI_H
#define GNUMERIC_COMMAND_CONTEXT_GUI_H

#include "gnumeric.h"
#include "command-context.h"

#define COMMAND_CONTEXT_GUI_TYPE        (command_context_gui_get_type ())
#define COMMAND_CONTEXT_GUI(o)          (GTK_CHECK_CAST ((o), COMMAND_CONTEXT_GUI_TYPE, CommandContextGui))
#define COMMAND_CONTEXT_GUI_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMMAND_CONTEXT_GUI_TYPE, CommandContextGuiClass))
#define IS_COMMAND_CONTEXT_GUI(o)       (GTK_CHECK_TYPE ((o), COMMAND_CONTEXT_GUI_TYPE))
#define IS_COMMAND_CONTEXT_GUI_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMMAND_CONTEXT_GUI_TYPE))

typedef struct {
	CommandContext parent;
	Workbook *wb;
} CommandContextGui;

typedef struct {
	CommandContextClass parent_class;
} CommandContextGuiClass;

GtkType         command_context_gui_get_type (void);
CommandContext *command_context_gui_new      (Workbook *wb);

#endif /* GNUMERIC_COMMAND_CONTEXT_H */

