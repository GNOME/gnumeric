/*
 * dialog-insert-cells.c: Insert a number of cells. 
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"

void
dialog_zoom (Workbook *wb, Sheet *sheet)
{
	char *custom;
	int  state [7];
	char *ret;
	char buffer [40];
	double zoom;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	memset (&state, 0, sizeof (state));

	sprintf (buffer, "%d", (int) sheet->last_zoom_factor_used * 100);
	
	if (sheet->last_zoom_factor_used == 4.0)
		state [0] = 1;
	else if (sheet->last_zoom_factor_used == 2.0)
		state [1] = 1;
	else if (sheet->last_zoom_factor_used == 1.0)
		state [2] = 1;
	else if (sheet->last_zoom_factor_used == 0.75)
		state [3] = 1;
	else if (sheet->last_zoom_factor_used == 0.50)
		state [4] = 1;
	else
		state [5] = 1;
	
	custom = buffer;

	ret = gtk_dialog_cauldron (
		_("Zoom"),
		GTK_CAULDRON_DIALOG,
		"( %[ ( %Rd / %Rd // %Rd // %Rd // %Rd // ( %R | %Ee | %Lfx )) ] /   (   %Bqrg || %Bqrg ) )",
		_("Zoom"),
		"400%",                   &state[0],
		"200%",           	  &state[1],
		"100%",           	  &state[2],
		"75%",            	  &state[3],
		"50%",            	  &state[4],
		_("Custom"),              &state[5],
		&custom,
		"%", 
		GNOME_STOCK_BUTTON_OK,
		GNOME_STOCK_BUTTON_CANCEL);

	if (strcmp (ret, GNOME_STOCK_BUTTON_CANCEL) == 0)
		return;

	if (state [0])
		zoom = 4.0;
	else if (state [1])
		zoom = 2.0;
	else if (state [2])
		zoom = 1.0;
	else if (state [3])
		zoom = 0.75;
	else if (state [4])
		zoom = 0.50;
	else if (state [5]){
		zoom = atof (custom) / 100;
	} else
		zoom = 1.0;
	
	g_free (custom);

	if (zoom < 0.25){
		gnumeric_notice (_("Zoom factor should be at least 50%"));
		return;
	}

	if (zoom > 9000){
		gnumeric_notice (_("Zoom factor should be at most 900%"));
		return;
	}

	sheet_set_zoom_factor (sheet, zoom);
}
