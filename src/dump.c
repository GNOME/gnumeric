#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include "gnumeric.h"
#include "dump.h"

static FILE *output_file;

static void
dump_func_help (gpointer key, gpointer value, gpointer user_data)
{
	Symbol *sym = value;
	FunctionDefinition *fd;
	
	if (sym->type != SYMBOL_FUNCTION)
		return;
	fd = sym->data;

	if (fd->help)
		fprintf (output_file, "%s\n\n", *(fd->help));
}

void
dump_functions (char *filename)
{
	if ((output_file = fopen (filename, "w")) == NULL){
		printf ("Can not create file %s\n", filename);
		exit (1);
	}

	g_hash_table_foreach (symbol_hash_table, dump_func_help, NULL);

	fclose (output_file);
}
