/* Mostly a copy of gtkfontbutton.c awaiting a fix for 695776.  */

#include <gnumeric-config.h>
#include <widgets/gnm-fontbutton.h>

typedef enum {
  GTK_FONT_CHOOSER_PROP_FIRST           = 0x4000,
  GTK_FONT_CHOOSER_PROP_FONT,
  GTK_FONT_CHOOSER_PROP_FONT_DESC,
  GTK_FONT_CHOOSER_PROP_PREVIEW_TEXT,
  GTK_FONT_CHOOSER_PROP_SHOW_PREVIEW_ENTRY,
#if GTK_CHECK_VERSION(3,24,0)
  GTK_FONT_CHOOSER_PROP_LEVEL,
  GTK_FONT_CHOOSER_PROP_LANGUAGE,
  GTK_FONT_CHOOSER_PROP_FONT_FEATURES,
#endif
  GTK_FONT_CHOOSER_PROP_LAST
} GtkFontChooserProp;

static void
_gtk_font_chooser_install_properties (GObjectClass *klass)
{
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_FONT,
                                    "font");
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_FONT_DESC,
                                    "font-desc");
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_PREVIEW_TEXT,
                                    "preview-text");
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_SHOW_PREVIEW_ENTRY,
                                    "show-preview-entry");
#if GTK_CHECK_VERSION(3,24,0)
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_LEVEL,
                                    "level");
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_LANGUAGE,
                                    "language");
  g_object_class_override_property (klass,
                                    GTK_FONT_CHOOSER_PROP_FONT_FEATURES,
                                    "font-features");
#endif
}

/*
 * GTK - The GIMP Toolkit
 * Copyright (C) 1998 David Abilleira Freijeiro <odaf@nexo.es>
 * All rights reserved.
 *
 * Based on gnome-color-picker by Federico Mena <federico@nuclecu.unam.mx>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Modified by the GTK+ Team and others 2003.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#include <string.h>
#include <glib/gi18n-lib.h>


/**
 * SECTION:gtkfontbutton
 * @Short_description: A button to launch a font chooser dialog
 * @Title: GnmFontButton
 * @See_also: #GtkFontChooserDialog, #GtkColorButton.
 *
 * The #GnmFontButton is a button which displays the currently selected
 * font an allows to open a font chooser dialog to change the font.
 * It is suitable widget for selecting a font in a preference dialog.
 */


struct _GnmFontButtonPrivate
{
  gchar         *title;

  gchar         *fontname;

  guint         use_font : 1;
  guint         use_size : 1;
  guint         show_style : 1;
  guint         show_size : 1;
  guint         show_preview_entry : 1;

  GtkWidget     *font_dialog;
  GtkWidget     *inside;
  GtkWidget     *font_label;
  GtkWidget     *size_label;

  PangoFontDescription *font_desc;
  PangoFontFamily      *font_family;
  PangoFontFace        *font_face;
  gint                  font_size;
  gchar                *preview_text;
  GtkFontFilterFunc     font_filter;
  gpointer              font_filter_data;
  GDestroyNotify        font_filter_data_destroy;

  GType dialog_type;
};

/* Signals */
enum
{
  FONT_SET,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_TITLE,
  PROP_FONT_NAME,
  PROP_USE_FONT,
  PROP_USE_SIZE,
  PROP_SHOW_STYLE,
  PROP_SHOW_SIZE,
  PROP_DIALOG_TYPE
};

/* Prototypes */
static void gnm_font_button_finalize               (GObject            *object);
static void gnm_font_button_get_property           (GObject            *object,
                                                    guint               param_id,
                                                    GValue             *value,
                                                    GParamSpec         *pspec);
static void gnm_font_button_set_property           (GObject            *object,
                                                    guint               param_id,
                                                    const GValue       *value,
                                                    GParamSpec         *pspec);

static void gnm_font_button_clicked                 (GtkButton         *button);

/* Dialog response functions */
static void response_cb                             (GtkDialog         *dialog,
                                                     gint               response_id,
                                                     gpointer           data);
static void dialog_destroy                          (GtkWidget         *widget,
                                                     gpointer           data);

/* Auxiliary functions */
static GtkWidget *gnm_font_button_create_inside     (GnmFontButton     *gfs);
static void gnm_font_button_label_use_font          (GnmFontButton     *gfs);
static void gnm_font_button_update_font_info        (GnmFontButton     *gfs);

static guint font_button_signals[LAST_SIGNAL] = { 0 };

static void
clear_font_data (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_family)
    g_object_unref (priv->font_family);
  priv->font_family = NULL;

  if (priv->font_face)
    g_object_unref (priv->font_face);
  priv->font_face = NULL;

  if (priv->font_desc)
    pango_font_description_free (priv->font_desc);
  priv->font_desc = NULL;

  g_free (priv->fontname);
  priv->fontname = NULL;
}

static void
clear_font_filter_data (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_filter_data_destroy)
    priv->font_filter_data_destroy (priv->font_filter_data);
  priv->font_filter = NULL;
  priv->font_filter_data = NULL;
  priv->font_filter_data_destroy = NULL;
}

static gboolean
font_description_style_equal (const PangoFontDescription *a,
                              const PangoFontDescription *b)
{
  return (pango_font_description_get_weight (a) == pango_font_description_get_weight (b) &&
          pango_font_description_get_style (a) == pango_font_description_get_style (b) &&
          pango_font_description_get_stretch (a) == pango_font_description_get_stretch (b) &&
          pango_font_description_get_variant (a) == pango_font_description_get_variant (b));
}

static void
gnm_font_button_update_font_data (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;
  PangoFontFamily **families;
  PangoFontFace **faces;
  gint n_families, n_faces, i;
  const gchar *family;

  g_assert (priv->font_desc != NULL);

  priv->fontname = pango_font_description_to_string (priv->font_desc);

  family = pango_font_description_get_family (priv->font_desc);
  if (family == NULL)
    return;

  n_families = 0;
  families = NULL;
  pango_context_list_families (gtk_widget_get_pango_context (GTK_WIDGET (font_button)),
                               &families, &n_families);
  n_faces = 0;
  faces = NULL;
  for (i = 0; i < n_families; i++)
    {
      const gchar *name = pango_font_family_get_name (families[i]);

      if (!g_ascii_strcasecmp (name, family))
        {
          priv->font_family = g_object_ref (families[i]);

          pango_font_family_list_faces (families[i], &faces, &n_faces);
          break;
        }
    }
  g_free (families);

  for (i = 0; i < n_faces; i++)
    {
      PangoFontDescription *tmp_desc = pango_font_face_describe (faces[i]);

      if (font_description_style_equal (tmp_desc, priv->font_desc))
        {
          priv->font_face = g_object_ref (faces[i]);

          pango_font_description_free (tmp_desc);
          break;
        }
      else
        pango_font_description_free (tmp_desc);
    }

  g_free (faces);
}

static gchar *
gnm_font_button_get_preview_text (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_dialog)
    return gtk_font_chooser_get_preview_text (GTK_FONT_CHOOSER (priv->font_dialog));

  return g_strdup (priv->preview_text);
}

static void
gnm_font_button_set_preview_text (GnmFontButton *font_button,
                                  const gchar   *preview_text)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_dialog)
    {
      gtk_font_chooser_set_preview_text (GTK_FONT_CHOOSER (priv->font_dialog),
                                         preview_text);
      return;
    }

  g_free (priv->preview_text);
  priv->preview_text = g_strdup (preview_text);
}


static gboolean
gnm_font_button_get_show_preview_entry (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_dialog)
    return gtk_font_chooser_get_show_preview_entry (GTK_FONT_CHOOSER (priv->font_dialog));

  return priv->show_preview_entry;
}

static void
gnm_font_button_set_show_preview_entry (GnmFontButton *font_button,
                                        gboolean       show)
{
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_dialog)
    gtk_font_chooser_set_show_preview_entry (GTK_FONT_CHOOSER (priv->font_dialog), show);
  else
    priv->show_preview_entry = show != FALSE;
}

static PangoFontFamily *
gnm_font_button_font_chooser_get_font_family (GtkFontChooser *chooser)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (chooser);
  GnmFontButtonPrivate *priv = font_button->priv;

  return priv->font_family;
}

static PangoFontFace *
gnm_font_button_font_chooser_get_font_face (GtkFontChooser *chooser)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (chooser);
  GnmFontButtonPrivate *priv = font_button->priv;

  return priv->font_face;
}

static int
gnm_font_button_font_chooser_get_font_size (GtkFontChooser *chooser)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (chooser);
  GnmFontButtonPrivate *priv = font_button->priv;

  return priv->font_size;
}

static void
gnm_font_button_font_chooser_set_filter_func (GtkFontChooser    *chooser,
                                              GtkFontFilterFunc  filter_func,
                                              gpointer           filter_data,
                                              GDestroyNotify     data_destroy)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (chooser);
  GnmFontButtonPrivate *priv = font_button->priv;

  if (priv->font_dialog)
    {
      gtk_font_chooser_set_filter_func (GTK_FONT_CHOOSER (priv->font_dialog),
                                        filter_func,
                                        filter_data,
                                        data_destroy);
      return;
    }

  clear_font_filter_data (font_button);
  priv->font_filter = filter_func;
  priv->font_filter_data = filter_data;
  priv->font_filter_data_destroy = data_destroy;
}

static void
gnm_font_button_take_font_desc (GnmFontButton        *font_button,
                                PangoFontDescription *font_desc)
{
  GnmFontButtonPrivate *priv = font_button->priv;
  GObject *object = G_OBJECT (font_button);

  if (priv->font_desc && font_desc &&
      pango_font_description_equal (priv->font_desc, font_desc))
    {
      pango_font_description_free (font_desc);
      return;
    }

  g_object_freeze_notify (object);

  clear_font_data (font_button);

  if (font_desc)
    priv->font_desc = font_desc; /* adopted */
  else
    priv->font_desc = pango_font_description_from_string (_("Sans 12"));

  if (pango_font_description_get_size_is_absolute (priv->font_desc))
    priv->font_size = pango_font_description_get_size (priv->font_desc);
  else
    priv->font_size = pango_font_description_get_size (priv->font_desc) / PANGO_SCALE;

  gnm_font_button_update_font_data (font_button);
  gnm_font_button_update_font_info (font_button);

  if (priv->font_dialog)
    gtk_font_chooser_set_font_desc (GTK_FONT_CHOOSER (priv->font_dialog),
                                    priv->font_desc);

  g_object_notify (G_OBJECT (font_button), "font");
  g_object_notify (G_OBJECT (font_button), "font-desc");
  g_object_notify (G_OBJECT (font_button), "font-name");

  g_object_thaw_notify (object);
}

static const PangoFontDescription *
gnm_font_button_get_font_desc (GnmFontButton *font_button)
{
  return font_button->priv->font_desc;
}

static void
gnm_font_button_font_chooser_notify (GObject    *object,
                                     GParamSpec *pspec,
                                     gpointer    user_data)
{
  /* We do not forward the notification of the "font" property to the dialog! */
    if (strcmp (pspec->name, ("preview-text")) == 0 ||
	strcmp (pspec->name, ("show-preview-entry")) == 0)
    g_object_notify_by_pspec (user_data, pspec);
}

static void
gnm_font_button_font_chooser_iface_init (GtkFontChooserIface *iface)
{
  iface->get_font_family = gnm_font_button_font_chooser_get_font_family;
  iface->get_font_face = gnm_font_button_font_chooser_get_font_face;
  iface->get_font_size = gnm_font_button_font_chooser_get_font_size;
  iface->set_filter_func = gnm_font_button_font_chooser_set_filter_func;
}

G_DEFINE_TYPE_WITH_CODE (GnmFontButton, gnm_font_button, GTK_TYPE_BUTTON,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_FONT_CHOOSER,
                                                gnm_font_button_font_chooser_iface_init))

static void
gnm_font_button_class_init (GnmFontButtonClass *klass)
{
  GObjectClass *gobject_class;
  GtkButtonClass *button_class;

  gnm_font_button_parent_class = g_type_class_peek_parent (klass);

  gobject_class = (GObjectClass *) klass;
  button_class = (GtkButtonClass *) klass;

  gobject_class->finalize = gnm_font_button_finalize;
  gobject_class->set_property = gnm_font_button_set_property;
  gobject_class->get_property = gnm_font_button_get_property;

  button_class->clicked = gnm_font_button_clicked;

  klass->font_set = NULL;

  _gtk_font_chooser_install_properties (gobject_class);

  g_object_class_install_property (gobject_class,
                                   PROP_DIALOG_TYPE,
                                   g_param_spec_gtype ("dialog-type",
						       _("Dialog Type"),
						       _("The type of the dialog"),
						       GTK_TYPE_FONT_CHOOSER,
						       (G_PARAM_READABLE |
							G_PARAM_WRITABLE)));


  /**
   * GnmFontButton:title:
   *
   * The title of the font chooser dialog.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        _("Title"),
                                                        _("The title of the font chooser dialog"),
                                                        _("Pick a Font"),
                                                        (G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE)));

  /**
   * GnmFontButton:font-name:
   *
   * The name of the currently selected font.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_FONT_NAME,
                                   g_param_spec_string ("font-name",
                                                        _("Font name"),
                                                        _("The name of the selected font"),
                                                        _("Sans 12"),
                                                        (G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE)));

  /**
   * GnmFontButton:use-font:
   *
   * If this property is set to %TRUE, the label will be drawn
   * in the selected font.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USE_FONT,
                                   g_param_spec_boolean ("use-font",
                                                         _("Use font in label"),
                                                         _("Whether the label is drawn in the selected font"),
                                                         FALSE,
                                                         G_PARAM_READWRITE));

  /**
   * GnmFontButton:use-size:
   *
   * If this property is set to %TRUE, the label will be drawn
   * with the selected font size.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USE_SIZE,
                                   g_param_spec_boolean ("use-size",
                                                         _("Use size in label"),
                                                         _("Whether the label is drawn with the selected font size"),
                                                         FALSE,
                                                         G_PARAM_READWRITE));

  /**
   * GnmFontButton:show-style:
   *
   * If this property is set to %TRUE, the name of the selected font style
   * will be shown in the label. For a more WYSIWYG way to show the selected
   * style, see the ::use-font property.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_SHOW_STYLE,
                                   g_param_spec_boolean ("show-style",
                                                         _("Show style"),
                                                         _("Whether the selected font style is shown in the label"),
                                                         TRUE,
                                                         G_PARAM_READWRITE));
  /**
   * GnmFontButton:show-size:
   *
   * If this property is set to %TRUE, the selected font size will be shown
   * in the label. For a more WYSIWYG way to show the selected size, see the
   * ::use-size property.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_SHOW_SIZE,
                                   g_param_spec_boolean ("show-size",
                                                         _("Show size"),
                                                         _("Whether selected font size is shown in the label"),
                                                         TRUE,
                                                         G_PARAM_READWRITE));

  /**
   * GnmFontButton::font-set:
   * @widget: the object which received the signal.
   *
   * The ::font-set signal is emitted when the user selects a font.
   * When handling this signal, use gnm_font_button_get_font_name()
   * to find out which font was just selected.
   *
   * Note that this signal is only emitted when the <emphasis>user</emphasis>
   * changes the font. If you need to react to programmatic font changes
   * as well, use the notify::font-name signal.
   *
   * Since: 2.4
   */
  font_button_signals[FONT_SET] = g_signal_new (("font-set"),
                                                G_TYPE_FROM_CLASS (gobject_class),
                                                G_SIGNAL_RUN_FIRST,
                                                G_STRUCT_OFFSET (GnmFontButtonClass, font_set),
                                                NULL, NULL,
                                                g_cclosure_marshal_VOID__VOID,
                                                G_TYPE_NONE, 0);

  g_type_class_add_private (gobject_class, sizeof (GnmFontButtonPrivate));
}

static void
gnm_font_button_init (GnmFontButton *font_button)
{
  font_button->priv = G_TYPE_INSTANCE_GET_PRIVATE (font_button,
                                                   GNM_TYPE_FONT_BUTTON,
                                                   GnmFontButtonPrivate);

  /* Initialize fields */
  font_button->priv->use_font = FALSE;
  font_button->priv->use_size = FALSE;
  font_button->priv->show_style = TRUE;
  font_button->priv->show_size = TRUE;
  font_button->priv->show_preview_entry = FALSE;
  font_button->priv->font_dialog = NULL;
  font_button->priv->font_family = NULL;
  font_button->priv->font_face = NULL;
  font_button->priv->font_size = -1;
  font_button->priv->title = g_strdup (_("Pick a Font"));
  font_button->priv->dialog_type = GTK_TYPE_FONT_CHOOSER_DIALOG;

  font_button->priv->inside = gnm_font_button_create_inside (font_button);
  gtk_container_add (GTK_CONTAINER (font_button), font_button->priv->inside);

  gnm_font_button_take_font_desc (font_button, NULL);
}

static void
gnm_font_button_finalize (GObject *object)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (object);

  if (font_button->priv->font_dialog != NULL)
    gtk_widget_destroy (font_button->priv->font_dialog);
  font_button->priv->font_dialog = NULL;

  g_free (font_button->priv->title);
  font_button->priv->title = NULL;

  clear_font_data (font_button);
  clear_font_filter_data (font_button);

  g_free (font_button->priv->preview_text);
  font_button->priv->preview_text = NULL;

  G_OBJECT_CLASS (gnm_font_button_parent_class)->finalize (object);
}

static void
gnm_font_button_set_property (GObject      *object,
                              guint         param_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (object);

  switch (param_id)
    {
    case GTK_FONT_CHOOSER_PROP_PREVIEW_TEXT:
      gnm_font_button_set_preview_text (font_button, g_value_get_string (value));
      break;
    case GTK_FONT_CHOOSER_PROP_SHOW_PREVIEW_ENTRY:
      gnm_font_button_set_show_preview_entry (font_button, g_value_get_boolean (value));
      break;
    case PROP_TITLE:
      gnm_font_button_set_title (font_button, g_value_get_string (value));
      break;
    case GTK_FONT_CHOOSER_PROP_FONT_DESC:
      gnm_font_button_take_font_desc (font_button, g_value_dup_boxed (value));
      break;
    case GTK_FONT_CHOOSER_PROP_FONT:
    case PROP_FONT_NAME:
      gnm_font_button_set_font_name (font_button, g_value_get_string (value));
      break;
    case PROP_USE_FONT:
      gnm_font_button_set_use_font (font_button, g_value_get_boolean (value));
      break;
    case PROP_USE_SIZE:
      gnm_font_button_set_use_size (font_button, g_value_get_boolean (value));
      break;
    case PROP_SHOW_STYLE:
      gnm_font_button_set_show_style (font_button, g_value_get_boolean (value));
      break;
    case PROP_SHOW_SIZE:
      gnm_font_button_set_show_size (font_button, g_value_get_boolean (value));
      break;
    case PROP_DIALOG_TYPE:
      font_button->priv->dialog_type = g_value_get_gtype (value);
      break;
#if GTK_CHECK_VERSION(3,24,0)
    case GTK_FONT_CHOOSER_PROP_LEVEL:
	  /* not supported, just to avoid criticals */
	  break;
    case GTK_FONT_CHOOSER_PROP_LANGUAGE:
	  /* not supported, just to avoid criticals */
	  break;
    case GTK_FONT_CHOOSER_PROP_FONT_FEATURES:
	  /* not supported, just to avoid criticals */
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}

static void
gnm_font_button_get_property (GObject    *object,
                              guint       param_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (object);

  switch (param_id)
    {
    case GTK_FONT_CHOOSER_PROP_PREVIEW_TEXT:
      g_value_set_string (value, gnm_font_button_get_preview_text (font_button));
      break;
    case GTK_FONT_CHOOSER_PROP_SHOW_PREVIEW_ENTRY:
      g_value_set_boolean (value, gnm_font_button_get_show_preview_entry (font_button));
      break;
    case PROP_TITLE:
      g_value_set_string (value, gnm_font_button_get_title (font_button));
      break;
    case GTK_FONT_CHOOSER_PROP_FONT_DESC:
      g_value_set_boxed (value, gnm_font_button_get_font_desc (font_button));
      break;
    case GTK_FONT_CHOOSER_PROP_FONT:
    case PROP_FONT_NAME:
      g_value_set_string (value, gnm_font_button_get_font_name (font_button));
      break;
    case PROP_USE_FONT:
      g_value_set_boolean (value, gnm_font_button_get_use_font (font_button));
      break;
    case PROP_USE_SIZE:
      g_value_set_boolean (value, gnm_font_button_get_use_size (font_button));
      break;
    case PROP_SHOW_STYLE:
      g_value_set_boolean (value, gnm_font_button_get_show_style (font_button));
      break;
    case PROP_SHOW_SIZE:
      g_value_set_boolean (value, gnm_font_button_get_show_size (font_button));
      break;
    case PROP_DIALOG_TYPE:
      g_value_set_gtype (value, font_button->priv->dialog_type);
      break;
#if GTK_CHECK_VERSION(3,24,0)
    case GTK_FONT_CHOOSER_PROP_LEVEL:
      g_value_set_int (value, GTK_FONT_CHOOSER_LEVEL_FAMILY |
                              GTK_FONT_CHOOSER_LEVEL_STYLE |
                              GTK_FONT_CHOOSER_LEVEL_SIZE);
      break;
    case GTK_FONT_CHOOSER_PROP_LANGUAGE:
      g_value_set_string (value, "");
      break;
    case GTK_FONT_CHOOSER_PROP_FONT_FEATURES:
      g_value_set_string (value, "");
      break;
#endif
	  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}


/**
 * gnm_font_button_new:
 *
 * Creates a new font picker widget.
 *
 * Returns: a new font picker widget.
 *
 * Since: 2.4
 */
GtkWidget *
gnm_font_button_new (void)
{
  return g_object_new (GTK_TYPE_FONT_BUTTON, NULL);
}

/**
 * gnm_font_button_new_with_font:
 * @fontname: Name of font to display in font chooser dialog
 *
 * Creates a new font picker widget.
 *
 * Returns: a new font picker widget.
 *
 * Since: 2.4
 */
GtkWidget *
gnm_font_button_new_with_font (const gchar *fontname)
{
  return g_object_new (GTK_TYPE_FONT_BUTTON, "font-name", fontname, NULL);
}

/**
 * gnm_font_button_set_title:
 * @font_button: a #GnmFontButton
 * @title: a string containing the font chooser dialog title
 *
 * Sets the title for the font chooser dialog.
 *
 * Since: 2.4
 */
void
gnm_font_button_set_title (GnmFontButton *font_button,
                           const gchar   *title)
{
  gchar *old_title;
  g_return_if_fail (GNM_IS_FONT_BUTTON (font_button));

  old_title = font_button->priv->title;
  font_button->priv->title = g_strdup (title);
  g_free (old_title);

  if (font_button->priv->font_dialog)
    gtk_window_set_title (GTK_WINDOW (font_button->priv->font_dialog),
                          font_button->priv->title);

  g_object_notify (G_OBJECT (font_button), "title");
}

/**
 * gnm_font_button_get_title:
 * @font_button: a #GnmFontButton
 *
 * Retrieves the title of the font chooser dialog.
 *
 * Returns: an internal copy of the title string which must not be freed.
 *
 * Since: 2.4
 */
const gchar*
gnm_font_button_get_title (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), NULL);

  return font_button->priv->title;
}

/**
 * gnm_font_button_get_use_font:
 * @font_button: a #GnmFontButton
 *
 * Returns whether the selected font is used in the label.
 *
 * Returns: whether the selected font is used in the label.
 *
 * Since: 2.4
 */
gboolean
gnm_font_button_get_use_font (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), FALSE);

  return font_button->priv->use_font;
}

/**
 * gnm_font_button_set_use_font:
 * @font_button: a #GnmFontButton
 * @use_font: If %TRUE, font name will be written using font chosen.
 *
 * If @use_font is %TRUE, the font name will be written using the selected font.
 *
 * Since: 2.4
 */
void
gnm_font_button_set_use_font (GnmFontButton *font_button,
			      gboolean       use_font)
{
  g_return_if_fail (GNM_IS_FONT_BUTTON (font_button));

  use_font = (use_font != FALSE);

  if (font_button->priv->use_font != use_font)
    {
      font_button->priv->use_font = use_font;

      gnm_font_button_label_use_font (font_button);

      g_object_notify (G_OBJECT (font_button), "use-font");
    }
}


/**
 * gnm_font_button_get_use_size:
 * @font_button: a #GnmFontButton
 *
 * Returns whether the selected size is used in the label.
 *
 * Returns: whether the selected size is used in the label.
 *
 * Since: 2.4
 */
gboolean
gnm_font_button_get_use_size (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), FALSE);

  return font_button->priv->use_size;
}

/**
 * gnm_font_button_set_use_size:
 * @font_button: a #GnmFontButton
 * @use_size: If %TRUE, font name will be written using the selected size.
 *
 * If @use_size is %TRUE, the font name will be written using the selected size.
 *
 * Since: 2.4
 */
void
gnm_font_button_set_use_size (GnmFontButton *font_button,
                              gboolean       use_size)
{
  g_return_if_fail (GNM_IS_FONT_BUTTON (font_button));

  use_size = (use_size != FALSE);
  if (font_button->priv->use_size != use_size)
    {
      font_button->priv->use_size = use_size;

      gnm_font_button_label_use_font (font_button);

      g_object_notify (G_OBJECT (font_button), "use-size");
    }
}

/**
 * gnm_font_button_get_show_style:
 * @font_button: a #GnmFontButton
 *
 * Returns whether the name of the font style will be shown in the label.
 *
 * Return value: whether the font style will be shown in the label.
 *
 * Since: 2.4
 **/
gboolean
gnm_font_button_get_show_style (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), FALSE);

  return font_button->priv->show_style;
}

/**
 * gnm_font_button_set_show_style:
 * @font_button: a #GnmFontButton
 * @show_style: %TRUE if font style should be displayed in label.
 *
 * If @show_style is %TRUE, the font style will be displayed along with name of the selected font.
 *
 * Since: 2.4
 */
void
gnm_font_button_set_show_style (GnmFontButton *font_button,
                                gboolean       show_style)
{
  g_return_if_fail (GNM_IS_FONT_BUTTON (font_button));

  show_style = (show_style != FALSE);
  if (font_button->priv->show_style != show_style)
    {
      font_button->priv->show_style = show_style;

      gnm_font_button_update_font_info (font_button);

      g_object_notify (G_OBJECT (font_button), "show-style");
    }
}


/**
 * gnm_font_button_get_show_size:
 * @font_button: a #GnmFontButton
 *
 * Returns whether the font size will be shown in the label.
 *
 * Return value: whether the font size will be shown in the label.
 *
 * Since: 2.4
 **/
gboolean
gnm_font_button_get_show_size (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), FALSE);

  return font_button->priv->show_size;
}

/**
 * gnm_font_button_set_show_size:
 * @font_button: a #GnmFontButton
 * @show_size: %TRUE if font size should be displayed in dialog.
 *
 * If @show_size is %TRUE, the font size will be displayed along with the name of the selected font.
 *
 * Since: 2.4
 */
void
gnm_font_button_set_show_size (GnmFontButton *font_button,
                               gboolean       show_size)
{
  g_return_if_fail (GNM_IS_FONT_BUTTON (font_button));

  show_size = (show_size != FALSE);

  if (font_button->priv->show_size != show_size)
    {
      font_button->priv->show_size = show_size;

      gtk_container_remove (GTK_CONTAINER (font_button), font_button->priv->inside);
      font_button->priv->inside = gnm_font_button_create_inside (font_button);
      gtk_container_add (GTK_CONTAINER (font_button), font_button->priv->inside);

      gnm_font_button_update_font_info (font_button);

      g_object_notify (G_OBJECT (font_button), "show-size");
    }
}


/**
 * gnm_font_button_get_font_name:
 * @font_button: a #GnmFontButton
 *
 * Retrieves the name of the currently selected font. This name includes
 * style and size information as well. If you want to render something
 * with the font, use this string with pango_font_description_from_string() .
 * If you're interested in peeking certain values (family name,
 * style, size, weight) just query these properties from the
 * #PangoFontDescription object.
 *
 * Returns: an internal copy of the font name which must not be freed.
 *
 * Since: 2.4
 */
const gchar *
gnm_font_button_get_font_name (GnmFontButton *font_button)
{
  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), NULL);

  return font_button->priv->fontname;
}

/**
 * gnm_font_button_set_font_name:
 * @font_button: a #GnmFontButton
 * @fontname: Name of font to display in font chooser dialog
 *
 * Sets or updates the currently-displayed font in font picker dialog.
 *
 * Returns: %TRUE
 *
 * Since: 2.4
 */
gboolean
gnm_font_button_set_font_name (GnmFontButton *font_button,
                               const gchar    *fontname)
{
  PangoFontDescription *font_desc;

  g_return_val_if_fail (GNM_IS_FONT_BUTTON (font_button), FALSE);
  g_return_val_if_fail (fontname != NULL, FALSE);

  font_desc = pango_font_description_from_string (fontname);
  gnm_font_button_take_font_desc (font_button, font_desc);

  return TRUE;
}

static void
gnm_font_button_clicked (GtkButton *button)
{
  GtkFontChooser *font_dialog;
  GnmFontButton  *font_button = GNM_FONT_BUTTON (button);
  GnmFontButtonPrivate *priv = font_button->priv;

  if (!font_button->priv->font_dialog)
    {
      GtkWidget *parent;

      parent = gtk_widget_get_toplevel (GTK_WIDGET (font_button));

      priv->font_dialog = g_object_new (priv->dialog_type, NULL);
      font_dialog = GTK_FONT_CHOOSER (font_button->priv->font_dialog);

      gtk_font_chooser_set_show_preview_entry (font_dialog, priv->show_preview_entry);

      if (priv->preview_text)
        {
          gtk_font_chooser_set_preview_text (font_dialog, priv->preview_text);
          g_free (priv->preview_text);
          priv->preview_text = NULL;
        }

      if (priv->font_filter)
        {
          gtk_font_chooser_set_filter_func (font_dialog,
                                            priv->font_filter,
                                            priv->font_filter_data,
                                            priv->font_filter_data_destroy);
          priv->font_filter = NULL;
          priv->font_filter_data = NULL;
          priv->font_filter_data_destroy = NULL;
        }

      if (gtk_widget_is_toplevel (parent) && GTK_IS_WINDOW (parent))
        {
          if (GTK_WINDOW (parent) != gtk_window_get_transient_for (GTK_WINDOW (font_dialog)))
            gtk_window_set_transient_for (GTK_WINDOW (font_dialog), GTK_WINDOW (parent));

          gtk_window_set_modal (GTK_WINDOW (font_dialog),
                                gtk_window_get_modal (GTK_WINDOW (parent)));
        }

      g_signal_connect (font_dialog, "notify",
                        G_CALLBACK (gnm_font_button_font_chooser_notify), button);

      g_signal_connect (font_dialog, "response",
                        G_CALLBACK (response_cb), font_button);

      g_signal_connect (font_dialog, "destroy",
                        G_CALLBACK (dialog_destroy), font_button);
    }

  if (!gtk_widget_get_visible (font_button->priv->font_dialog))
    {
      font_dialog = GTK_FONT_CHOOSER (font_button->priv->font_dialog);
      gtk_font_chooser_set_font_desc (font_dialog, font_button->priv->font_desc);
    }

  gtk_window_present (GTK_WINDOW (font_button->priv->font_dialog));
}


static void
response_cb (GtkDialog *dialog,
             gint       response_id,
             gpointer   data)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (data);
  GnmFontButtonPrivate *priv = font_button->priv;
  GtkFontChooser *font_chooser;
  GObject *object;

  gtk_widget_hide (font_button->priv->font_dialog);

  if (response_id != GTK_RESPONSE_OK)
    return;

  font_chooser = GTK_FONT_CHOOSER (priv->font_dialog);
  object = G_OBJECT (font_chooser);

  g_object_freeze_notify (object);

  clear_font_data (font_button);

  priv->font_desc = gtk_font_chooser_get_font_desc (font_chooser);
  if (priv->font_desc)
    priv->fontname = pango_font_description_to_string (priv->font_desc);
  priv->font_family = gtk_font_chooser_get_font_family (font_chooser);
  if (priv->font_family)
    g_object_ref (priv->font_family);
  priv->font_face = gtk_font_chooser_get_font_face (font_chooser);
  if (priv->font_face)
    g_object_ref (priv->font_face);
  priv->font_size = gtk_font_chooser_get_font_size (font_chooser);

  /* Set label font */
  gnm_font_button_update_font_info (font_button);

  g_object_notify (G_OBJECT (font_button), "font");
  g_object_notify (G_OBJECT (font_button), "font-desc");
  g_object_notify (G_OBJECT (font_button), "font-name");

  g_object_thaw_notify (object);

  /* Emit font_set signal */
  g_signal_emit (font_button, font_button_signals[FONT_SET], 0);
}

static void
dialog_destroy (GtkWidget *widget,
                gpointer   data)
{
  GnmFontButton *font_button = GNM_FONT_BUTTON (data);

  /* Dialog will get destroyed so reference is not valid now */
  font_button->priv->font_dialog = NULL;
}

static GtkWidget *
gnm_font_button_create_inside (GnmFontButton *font_button)
{
  GtkWidget *widget;

  //gtk_widget_push_composite_child ();

  widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  font_button->priv->font_label = gtk_label_new (_("Font"));

  gtk_label_set_justify (GTK_LABEL (font_button->priv->font_label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start (GTK_BOX (widget), font_button->priv->font_label, TRUE, TRUE, 5);

  if (font_button->priv->show_size)
    {
      gtk_box_pack_start (GTK_BOX (widget), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
      font_button->priv->size_label = gtk_label_new ("14");
      gtk_box_pack_start (GTK_BOX (widget), font_button->priv->size_label, FALSE, FALSE, 5);
    }

  gtk_widget_show_all (widget);

  //gtk_widget_pop_composite_child ();

  return widget;
}

static void
gnm_font_button_label_use_font (GnmFontButton *font_button)
{
  PangoFontDescription *desc;

  if (font_button->priv->use_font)
    {
      desc = pango_font_description_copy (font_button->priv->font_desc);

      if (!font_button->priv->use_size)
        pango_font_description_unset_fields (desc, PANGO_FONT_MASK_SIZE);
    }
  else
    desc = NULL;

  gtk_widget_override_font (font_button->priv->font_label, desc);

  if (desc)
    pango_font_description_free (desc);
}

static void
gnm_font_button_update_font_info (GnmFontButton *font_button)
{
  GnmFontButtonPrivate *priv = font_button->priv;
  gchar *family_style;

  g_assert (priv->font_desc != NULL);

  if (priv->show_style)
    {
      PangoFontDescription *desc = pango_font_description_copy_static (priv->font_desc);

      pango_font_description_unset_fields (desc, PANGO_FONT_MASK_SIZE);
      family_style = pango_font_description_to_string (desc);
      pango_font_description_free (desc);
    }
  else
    family_style = g_strdup (pango_font_description_get_family (priv->font_desc));

  gtk_label_set_text (GTK_LABEL (font_button->priv->font_label), family_style);
  g_free (family_style);

  if (font_button->priv->show_size)
    {
      /* mirror Pango, which doesn't translate this either */
      gchar *size = g_strdup_printf ("%g%s",
                                     pango_font_description_get_size (priv->font_desc) / (double)PANGO_SCALE,
                                     pango_font_description_get_size_is_absolute (priv->font_desc) ? "px" : "");

      gtk_label_set_text (GTK_LABEL (font_button->priv->size_label), size);

      g_free (size);
    }

  gnm_font_button_label_use_font (font_button);
}
