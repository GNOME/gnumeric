/*
 * libgnumeric.c: global initialization and management code
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1997-1999 Miguel de Icaza (miguel@kernel.org)
 * Copyright (C) 2006-2009 Morten Welinder (terra@gnome.org)
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
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <gutils.h>

#include <application.h>
#include <stf.h>
#include <gnm-format.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <workbook.h>
#include <sheet-object.h>
#include <number-match.h>
#include <expr-name.h>
#include <func.h>
#include <print-info.h>
#include <style-font.h>
#include <mstyle.h>
#include <style-border.h>
#include <style-color.h>
#include <print.h>
#include <dependent.h>
#include <sheet-autofill.h>
#include <sheet-private.h>
#include <xml-sax.h>
#include <clipboard.h>
#include <gui-clipboard.h>
#include <value.h>
#include <expr.h>
#include <expr-deriv.h>
#include <parse-util.h>
#include <rendered-value.h>
#include <gnumeric-conf.h>
#include <gnm-plugin.h>
#include <mathfunc.h>
#include <hlink.h>
#include <wbc-gtk-impl.h>
#include <gnmresources.h>
#include <goffice/goffice.h>

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#include <locale.h>

#ifndef HAVE_GTK_ICON_THEME_ADD_RESOURCE_PATH
#define gtk_icon_theme_add_resource_path fake_gtk_icon_theme_add_resource_path


static void
walk_resource_path (const char *path, int level, int size)
{
	char **children = g_resources_enumerate_children (path, 0, NULL);
	int i;

	if (!children)
		return;

	for (i = 0; children[i]; i++) {
		const char *child = children[i];
		char *subpath;
		GBytes *data;

		if (level == 0) {
			size = atol (child);
			if (size <= 0)
				continue;
		}

		subpath = g_build_path ("/", path, child, NULL);

		data = g_resources_lookup_data (subpath, 0, NULL);
		if (data) {
			GdkPixbuf *pixbuf = gdk_pixbuf_new_from_resource (subpath, NULL);
			if (pixbuf && size > 0 && strchr (child, '.')) {
				char *iconname = g_strdup (child);
				strchr(iconname, '.')[0] = 0;
				if (gnm_debug_flag ("icons"))
					g_printerr ("Defining icon %s at size %d\n", iconname, size);
				gtk_icon_theme_add_builtin_icon (iconname,
								 size,
								 pixbuf);

				g_object_unref (pixbuf);
				g_free (iconname);
			}

			g_bytes_unref (data);
		} else
			walk_resource_path (subpath, level + 1, size);
		g_free (subpath);
	}

	g_strfreev (children);
}

static void
fake_gtk_icon_theme_add_resource_path (GtkIconTheme G_GNUC_UNUSED *theme,
				       const char *path)
{
	walk_resource_path (path, 0, -1);
}

#endif

static gboolean param_show_version = FALSE;
static char *param_lib_dir  = NULL;
static char *param_data_dir = NULL;

static GOptionEntry const libspreadsheet_options [] = {
	/*********************************
	 * Public Actions */
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &param_show_version,
		N_("Display Gnumeric's version"),
		NULL
	},

	/*********************************
	 * Public Variables */
	{
		"lib-dir", 'L',
		0, G_OPTION_ARG_FILENAME, &param_lib_dir,
		N_("Set the root library directory"),
		N_("DIR")
	},
	{
		"data-dir", 'D',
		0, G_OPTION_ARG_FILENAME, &param_data_dir,
		N_("Adjust the root data directory"),
		N_("DIR")
	},

	/**************************************
	 * Hidden debugging flags */

	{ NULL }
};

static gboolean
cb_gnm_option_group_post_parse (GOptionContext *context,
				GOptionGroup   *group,
				gpointer        data,
				GError        **error)
{
	if (param_show_version) {
		g_print (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			 GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		exit (0);
	}
	return TRUE;
}

/**
 * gnm_get_option_group: (skip)
 *
 * Returns a #GOptionGroup for the commandline arguments recognized
 * by libspreadsheet. You should add this group to your #GOptionContext with
 * g_option_context_add_group(), if you are using g_option_context_parse() to
 * parse your commandline arguments.
 *
 * Returns a #GOptionGroup for the commandline arguments recognized
 *   by libspreadsheet
 *
 * Since: 1.8
 **/
GOptionGroup *
gnm_get_option_group (void)
{
	GOptionGroup *group = g_option_group_new ("libspreadsheet",
		_("Gnumeric Options"), _("Show Gnumeric Options"), NULL, NULL);
	g_option_group_add_entries (group, libspreadsheet_options);
	g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);
	g_option_group_set_parse_hooks (group, NULL,
		&cb_gnm_option_group_post_parse);
	return group;
}

// Determine whether help is requested.
static gboolean
help_requested (gchar const **argv)
{
	for (int i = 1; argv[i]; i++) {
		const char *a = argv[i];
		if (g_str_equal (a, "-h") ||
		    g_str_has_prefix (a, "--help"))
			return TRUE;
	}
	return FALSE;
}

static char *argv0;
char const *
gnm_get_argv0 (void)
{
	return argv0;
}


/**
 * gnm_pre_parse_init:
 * @argc:
 * @argv:
 *
 * Initialization to be done before cmd line arguments are handled.
 * Needs to be called first, before any other initialization.
 *
 * Returns: (transfer none): the arguments in UTF-8 encoding.
 **/
gchar const **
gnm_pre_parse_init (int argc, gchar const **argv)
{
/*
 * NO CODE BEFORE THIS POINT, PLEASE!
 *
 * Using threads (by way of libraries) makes our stack too small in some
 * circumstances.  It is hard to control directly, but setting the stack
 * limit to something not unlimited seems to work.
 *
 * See http://bugzilla.gnome.org/show_bug.cgi?id=92131
 */
#ifdef HAVE_SYS_RESOURCE_H
	struct rlimit rlim;

	if (getrlimit (RLIMIT_STACK, &rlim) == 0) {
		rlim_t our_lim = 64 * 1024 * 1024;
		if (rlim.rlim_max != RLIM_INFINITY)
			our_lim = MIN (our_lim, rlim.rlim_max);
		if (rlim.rlim_cur != RLIM_INFINITY &&
		    rlim.rlim_cur < our_lim) {
			rlim.rlim_cur = our_lim;
			(void)setrlimit (RLIMIT_STACK, &rlim);
		}
	}
#endif

	/* Default value is bogus, see 732184.  */
	if (!getenv ("G_ENABLE_DIAGNOSTIC")) {
#ifdef HAVE_SETENV
		setenv ("G_ENABLE_DIAGNOSTIC", "0", FALSE);
#elif defined(HAVE_PUTENV)
		putenv ("G_ENABLE_DIAGNOSTIC=0");
#else
		/* No big deal.  */
#endif
	}

	/* On win32 argv contains 'ansi' encoded args.  We need to manually
	 * pull in the real versions and convert them to utf-8 */
	argv = go_shell_argv_to_glib_encoding (argc, argv);

	argv0 = g_strdup (argv[0]);

	// This is a mess, see #677.  Basically there are conflicting uses
	// for the program name set with g_set_prgname.
	if (help_requested (argv)) {
		// We want the full path in help messages
		g_set_prgname (argv[0]);
	} else {
		// We want "gnumeric" or "ssconvert" because it, someone, is
		// used for icon discovery in some cases.
		char *basename = g_path_get_basename (argv[0]);
		g_set_prgname (basename);
		g_free (basename);
	}

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

	gutils_init ();

	bindtextdomain (GETTEXT_PACKAGE, gnm_locale_dir ());
	bindtextdomain (GETTEXT_PACKAGE "-functions", gnm_locale_dir ());
	textdomain (GETTEXT_PACKAGE);

	/* Force all of the locale segments to update from the environment.
	 * Unless we do this they will default to C
	 */
	setlocale (LC_ALL, "");

	return argv;
}

void
gnm_pre_parse_shutdown (void)
{
	go_shell_argv_to_glib_encoding_free ();
	g_free (argv0);
	argv0 = NULL;
}

void
gnm_init (void)
{
	static gboolean inited = FALSE;
	if (inited)
		return;
	inited = TRUE;

	// Might be duplicate call, but that's ok
	gutils_init ();

	libgoffice_init ();
	_gnm_register_resource ();
	if (gdk_screen_get_default ()) {
		/* Only when we have a gui.  */
		gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
						  "/org/gnumeric/gnumeric/icons");
	}

	gnm_plugins_service_init ();

	g_object_new (GNM_APP_TYPE, NULL);
	mathfunc_init ();

	gnm_style_init ();
	gnm_conf_init ();
	gnm_color_init ();
	gnm_font_init ();	/* requires config */

	value_init ();
	parse_util_init ();
	gnm_expr_init_ ();
	gnm_sheet_cell_init ();
	clipboard_init ();
	gui_clipboard_init ();
	dependent_types_init ();
	gnm_rendered_value_init ();
	gnm_func_init_ ();
	print_init ();
	gnm_autofill_init ();
	sheet_objects_init ();
	gnm_hlink_init_ ();

	/* The statically linked in file formats */
	gnm_xml_sax_read_init ();
	gnm_xml_sax_write_init ();
	stf_init ();

	/* Make sure that images will be displayed with the correct
	 resolution, see #628472 */
	go_image_set_default_dpi (gnm_app_display_dpi_get (TRUE),
	                          gnm_app_display_dpi_get (FALSE));
}

void
gnm_shutdown (void)
{
	GSList *plugin_states;

	gnm_app_clipboard_clear (TRUE);

	plugin_states = go_plugins_shutdown ();
	if (NULL != plugin_states) {
		gnm_conf_set_plugins_file_states (plugin_states);
		g_slist_free_full (plugin_states, g_free);
	}

	stf_shutdown ();
	gnm_xml_sax_write_shutdown ();
	gnm_xml_sax_read_shutdown ();

	sheet_objects_shutdown ();
	gnm_autofill_shutdown ();
	print_shutdown ();
	gnm_func_shutdown_ ();

	gnm_rendered_value_shutdown ();
	dependent_types_shutdown ();
	gui_clipboard_shutdown ();
	clipboard_shutdown ();
	gnm_sheet_cell_shutdown ();
	gnm_expr_deriv_shutdown_ ();
	gnm_expr_shutdown_ ();
	parse_util_shutdown ();
	value_shutdown ();

	// The style leak printer may access font/border/color info so
	// shut styles down first.
	gnm_style_shutdown ();
	gnm_font_shutdown ();
	gnm_border_shutdown ();
	gnm_color_shutdown ();

	gnm_conf_shutdown ();

	_gnm_unregister_resource ();
	libgoffice_shutdown ();
	go_plugin_services_shutdown ();
	g_object_unref (gnm_app_get_app ());
	gutils_shutdown ();
}
