/*
 * Copyright (C) 2026 Niccolò Costantini
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Side pane that shows a large thumbnail and the main details of the
 * selected file, like the preview pane of Finder and Windows Explorer. */

#include "nautilus-preview-pane.h"

#include <glib/gi18n.h>

#include "nautilus-file.h"
#include "nautilus-window-slot.h"

#define PREVIEW_SIZE 256

struct _NautilusPreviewPane
{
    AdwBin parent_instance;

    NautilusWindowSlot *window_slot;
    NautilusFile *file;

    GtkWidget *picture;
    GtkWidget *name;
    GtkWidget *type;
    GtkWidget *size;
    GtkWidget *modified;
};

G_DEFINE_FINAL_TYPE (NautilusPreviewPane, nautilus_preview_pane, ADW_TYPE_BIN)

enum
{
    PROP_0,
    PROP_WINDOW_SLOT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
set_label (GtkWidget *label,
           char      *text)
{
    gtk_label_set_text (GTK_LABEL (label), text != NULL ? text : "");
    gtk_widget_set_visible (label, text != NULL && *text != '\0');
    g_free (text);
}

/* The largest thumbnail already in the freedesktop cache, if any.
 * The views fill this cache, so it is usually there. */
static GdkPaintable *
load_cached_thumbnail (NautilusFile *file)
{
    static const char *sizes[] = { "xx-large", "x-large", "large", "normal" };
    g_autofree char *uri = nautilus_file_get_uri (file);
    g_autofree char *md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);
    g_autofree char *name = g_strconcat (md5, ".png", NULL);

    for (guint i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
        g_autofree char *path = g_build_filename (g_get_user_cache_dir (), "thumbnails",
                                                  sizes[i], name, NULL);
        GdkTexture *texture = gdk_texture_new_from_filename (path, NULL);

        if (texture != NULL)
        {
            return GDK_PAINTABLE (texture);
        }
    }

    return NULL;
}

static void
show_file (NautilusPreviewPane *self)
{
    NautilusFile *file = self->file;
    int scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    g_autoptr (GdkPaintable) paintable = load_cached_thumbnail (file);

    if (paintable == NULL)
    {
        paintable = nautilus_file_get_icon_paintable (file, PREVIEW_SIZE, scale,
                                                      NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS);
    }

    gtk_picture_set_paintable (GTK_PICTURE (self->picture), paintable);
    gtk_widget_set_visible (self->picture, TRUE);
    set_label (self->name, g_strdup (nautilus_file_get_display_name (file)));
    set_label (self->type, nautilus_file_get_string_attribute (file, "detailed_type"));
    set_label (self->size, nautilus_file_get_string_attribute (file, "size"));
    set_label (self->modified, nautilus_file_get_string_attribute (file, "date_modified_full"));
}

static void
show_message (NautilusPreviewPane *self,
              char                *message)
{
    gtk_picture_set_paintable (GTK_PICTURE (self->picture), NULL);
    gtk_widget_set_visible (self->picture, FALSE);
    set_label (self->name, message);
    set_label (self->type, NULL);
    set_label (self->size, NULL);
    set_label (self->modified, NULL);
}

static void
on_file_changed (NautilusPreviewPane *self)
{
    /* A thumbnail or size may arrive after the selection */
    show_file (self);
}

static void
set_file (NautilusPreviewPane *self,
          NautilusFile        *file)
{
    if (self->file == file)
    {
        return;
    }

    if (self->file != NULL)
    {
        g_signal_handlers_disconnect_by_data (self->file, self);
    }
    g_set_object (&self->file, file);
    if (self->file != NULL)
    {
        g_signal_connect_object (self->file, "changed",
                                 G_CALLBACK (on_file_changed), self, G_CONNECT_SWAPPED);
    }
}

static void
update (NautilusPreviewPane *self)
{
    GList *selection;  /* owned by the slot */
    guint n;

    if (!gtk_widget_get_visible (GTK_WIDGET (self)) || self->window_slot == NULL)
    {
        set_file (self, NULL);
        return;
    }

    selection = nautilus_window_slot_get_selection (self->window_slot);
    n = g_list_length (selection);

    if (n == 1)
    {
        set_file (self, selection->data);
        show_file (self);
        return;
    }

    set_file (self, NULL);
    if (n == 0)
    {
        show_message (self, g_strdup (_("No Item Selected")));
    }
    else
    {
        show_message (self, g_strdup_printf (ngettext ("%u Item Selected",
                                                       "%u Items Selected", n), n));
    }
}

static void
set_window_slot (NautilusPreviewPane *self,
                 NautilusWindowSlot  *slot)
{
    if (self->window_slot == slot)
    {
        return;
    }

    if (self->window_slot != NULL)
    {
        g_signal_handlers_disconnect_by_data (self->window_slot, self);
    }
    g_set_weak_pointer (&self->window_slot, slot);
    if (slot != NULL)
    {
        g_signal_connect_object (slot, "notify::selection",
                                 G_CALLBACK (update), self, G_CONNECT_SWAPPED);
    }

    update (self);
}

static GtkWidget *
add_label (GtkWidget  *box,
           const char *style)
{
    GtkWidget *label = gtk_label_new (NULL);

    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 30);
    gtk_widget_add_css_class (label, style);
    gtk_box_append (GTK_BOX (box), label);

    return label;
}

static void
nautilus_preview_pane_init (NautilusPreviewPane *self)
{
    GtkWidget *scroll = gtk_scrolled_window_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (GTK_WIDGET (self), 260, -1);
    gtk_widget_set_margin_start (box, 18);
    gtk_widget_set_margin_end (box, 18);
    gtk_widget_set_margin_top (box, 24);
    gtk_widget_set_margin_bottom (box, 24);

    self->picture = gtk_picture_new ();
    gtk_picture_set_content_fit (GTK_PICTURE (self->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink (GTK_PICTURE (self->picture), TRUE);
    gtk_widget_set_size_request (self->picture, -1, 200);
    gtk_widget_set_margin_bottom (self->picture, 12);
    gtk_box_append (GTK_BOX (box), self->picture);

    self->name = add_label (box, "heading");
    self->type = add_label (box, "dim-label");
    self->size = add_label (box, "dim-label");
    self->modified = add_label (box, "dim-label");

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), box);
    adw_bin_set_child (ADW_BIN (self), scroll);

    gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _("Preview Pane"),
                                    -1);

    /* Skip the work while hidden, refresh when shown */
    g_signal_connect (self, "notify::visible", G_CALLBACK (update), NULL);
}

static void
nautilus_preview_pane_dispose (GObject *object)
{
    NautilusPreviewPane *self = NAUTILUS_PREVIEW_PANE (object);

    set_file (self, NULL);
    set_window_slot (self, NULL);

    G_OBJECT_CLASS (nautilus_preview_pane_parent_class)->dispose (object);
}

static void
nautilus_preview_pane_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    NautilusPreviewPane *self = NAUTILUS_PREVIEW_PANE (object);

    switch (prop_id)
    {
        case PROP_WINDOW_SLOT:
        {
            set_window_slot (self, g_value_get_object (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_preview_pane_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    NautilusPreviewPane *self = NAUTILUS_PREVIEW_PANE (object);

    switch (prop_id)
    {
        case PROP_WINDOW_SLOT:
        {
            g_value_set_object (value, self->window_slot);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_preview_pane_class_init (NautilusPreviewPaneClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = nautilus_preview_pane_dispose;
    object_class->set_property = nautilus_preview_pane_set_property;
    object_class->get_property = nautilus_preview_pane_get_property;

    properties[PROP_WINDOW_SLOT] =
        g_param_spec_object ("window-slot", NULL, NULL,
                             NAUTILUS_TYPE_WINDOW_SLOT,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}
