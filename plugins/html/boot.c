/*
 * boot.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnome.h>
#include "config.h"
#include "boot.h"
#include "html.h"
#include "latex.h"
#include "roff.h"
#include "file.h"

#ifdef SUPPORT_OLD_EPSF
/* This is disabled and will probably be removed.  Printing support can produce
 * postscript and handles things like spanning cells much better.   There is no
 * reason to fix the breakage here.
 */
#include "epsf.h"
#endif

gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;

static FileOpenerId html_opener_id;
static FileSaverId html32_saver_id, html40_saver_id,
                   latex_saver_id, latex2e_saver_id,
                   dvi_saver_id, troff_saver_id,
                   pdf_saver_id;
#ifdef SUPPORT_OLD_EPSF
static FileSaverId eps_saver_id;
#endif

/*
 * We can unload
 */
gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	return TRUE;
}

/*
 * called when unloading the plugin
 */
gboolean
cleanup_plugin (PluginInfo *pinfo)
{
	file_format_unregister_save (html32_saver_id);
	file_format_unregister_save (html40_saver_id);
	file_format_unregister_save (latex_saver_id);
	file_format_unregister_save (latex2e_saver_id);
	file_format_unregister_save (dvi_saver_id);
	file_format_unregister_save (pdf_saver_id);
	file_format_unregister_save (troff_saver_id);
#ifdef SUPPORT_OLD_EPSF
	file_format_unregister_save (eps_saver_id);
#endif
	file_format_unregister_open (html_opener_id);

	set_default_file_saver_id (html32_saver_id);

	return TRUE;
}

/*
 * register all file formats
 */
static void
html_init (void)
{
	html32_saver_id = file_format_register_save (
	                  ".html", _("HTML 3.2 file format (*.html)"),
	                  FILE_FL_AUTO, html_write_wb_html32, NULL);

	html40_saver_id = file_format_register_save (
	                  ".html", _("HTML 4.0 file format (*.html)"),
	                  FILE_FL_AUTO, html_write_wb_html40, NULL);

	/* Register file format with priority 100 */
	html_opener_id = file_format_register_open (
	                 100, _("HTML file made by gnumeric"),
	                 NULL, html_read, NULL);

	latex_saver_id = file_format_register_save (
	               ".tex", _("LaTeX file format (*.tex)"),
	               FILE_FL_WRITE_ONLY, html_write_wb_latex, NULL);

	latex2e_saver_id = file_format_register_save (
	                   ".tex", _("LaTeX2e file format (*.tex)"),
	                   FILE_FL_WRITE_ONLY, html_write_wb_latex2e, NULL);

	dvi_saver_id = file_format_register_save (
	               ".dvi", _("DVI TeX file format (via groff)"),
	               FILE_FL_WRITE_ONLY, html_write_wb_roff_dvi, NULL);

	troff_saver_id = file_format_register_save (
	                 ".me", _("TROFF file format (*.me)"),
	                 FILE_FL_WRITE_ONLY, html_write_wb_roff, NULL);

	pdf_saver_id = file_format_register_save (
	               ".pdf", _("PDF file format (via groff/gs)"),
	               FILE_FL_WRITE_ONLY, html_write_wb_roff_pdf, NULL);

#ifdef SUPPORT_OLD_EPSF
	eps_saver_id = file_format_register_save (
	               ".eps", _("EPS file format (*.eps)"),
	               FILE_FL_WRITE_ONLY, epsf_write_wb, NULL);
#endif
}

/*
 * called by gnumeric to load the plugin
 */
gboolean
init_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	html_init ();

	return TRUE;
}
