/*
 * Copyright (C) 2026 Niccolò Costantini
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Side pane with a preview and the main details of the selected file,
 * like the preview pane of Finder and Windows Explorer. PDFs and Office
 * documents are shown page by page, text files as text, images in full
 * quality; other files show their thumbnail. */

#include "nautilus-preview-pane.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glycin.h>
#include <glycin-gtk4.h>
/* shortcut: Papers' C API is unstable; rebuild Naughty after a Papers update if previews break */
#define I_KNOW_THE_PAPERS_LIBS_ARE_UNSTABLE_AND_HAVE_TALKED_WITH_THE_AUTHORS
#include <papers-document.h>
#include <papers-view.h>

#include "nautilus-file.h"
#include "nautilus-window-slot.h"

#define PREVIEW_SIZE 256
#define TEXT_PREVIEW_BYTES (256 * 1024)

struct _NautilusPreviewPane
{
    AdwBin parent_instance;

    NautilusWindowSlot *window_slot;
    NautilusFile *file;
    GCancellable *cancellable;

    GtkWidget *stack;
    GtkWidget *picture;
    GtkWidget *document_view;
    /* Created once, like Sushi's template objects; only the document changes */
    PpsDocumentModel *document_model;
    PpsUndoContext *undo_context;
    PpsAnnotationsContext *annotations_context;
    PpsSearchContext *search_context;
    GtkWidget *text_view;
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

static const char *office_types[] =
{
    "application/msword",
    "application/rtf",
    "application/vnd.ms-excel",
    "application/vnd.ms-powerpoint",
    "application/vnd.oasis.opendocument.presentation",
    "application/vnd.oasis.opendocument.spreadsheet",
    "application/vnd.oasis.opendocument.text",
    "application/vnd.openxmlformats-officedocument.presentationml.presentation",
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
};

static const char *document_types[] =
{
    "application/pdf",
    "application/postscript",
    "application/x-cbz",
    "application/vnd.comicbook+zip",
    "image/vnd.djvu",
};

static gboolean
mime_in (const char  *mime,
         const char **list,
         gsize        n)
{
    for (gsize i = 0; i < n; i++)
    {
        if (g_content_type_is_a (mime, list[i]))
        {
            return TRUE;
        }
    }

    return FALSE;
}

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
show_thumbnail (NautilusPreviewPane *self)
{
    int scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    g_autoptr (GdkPaintable) paintable = load_cached_thumbnail (self->file);

    if (paintable == NULL)
    {
        paintable = nautilus_file_get_icon_paintable (self->file, PREVIEW_SIZE, scale,
                                                      NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS);
    }

    gtk_picture_set_paintable (GTK_PICTURE (self->picture), paintable);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "picture");
}

static void
show_details (NautilusPreviewPane *self)
{
    NautilusFile *file = self->file;

    set_label (self->name, g_strdup (nautilus_file_get_display_name (file)));
    set_label (self->type, nautilus_file_get_string_attribute (file, "detailed_type"));
    set_label (self->size, nautilus_file_get_string_attribute (file, "size"));
    set_label (self->modified, nautilus_file_get_string_attribute (file, "date_modified_full"));
}

/* ---- Documents (PDF and friends) ---- */

static void
on_document_loaded (PpsJob              *job,
                    NautilusPreviewPane *self)
{
    g_autoptr (PpsDocument) document = NULL;

    if (job != g_object_get_data (G_OBJECT (self), "document-job") ||
        !pps_job_is_succeeded (job, NULL))
    {
        return;
    }
    /* transfer full */
    document = pps_job_load_get_loaded_document (PPS_JOB_LOAD (job));
    if (document == NULL || pps_document_get_n_pages (document) == 0)
    {
        return;
    }

    pps_document_model_set_document (self->document_model, document);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "document");
}

static void
load_document (NautilusPreviewPane *self,
               GFile               *location)
{
    g_autofree char *uri = g_file_get_uri (location);
    g_autoptr (PpsJob) job = pps_job_load_new ();

    pps_job_load_set_uri (PPS_JOB_LOAD (job), uri);
    /* Only the latest job may update the view */
    g_object_set_data_full (G_OBJECT (self), "document-job", g_object_ref (job), g_object_unref);
    g_signal_connect_object (job, "finished",
                             G_CALLBACK (on_document_loaded), self, G_CONNECT_DEFAULT);
    pps_job_scheduler_push_job (job, PPS_JOB_PRIORITY_NONE);
}

/* ---- Office documents: converted to PDF, then shown as documents ---- */

typedef struct
{
    NautilusPreviewPane *self;
    GCancellable *cancellable;
    GFile *pdf;
    char *params_path;
} ConvertData;

static void
convert_data_free (ConvertData *data)
{
    g_object_unref (data->self);
    g_object_unref (data->cancellable);
    g_object_unref (data->pdf);
    if (data->params_path != NULL)
    {
        g_unlink (data->params_path);
        g_free (data->params_path);
    }
    g_free (data);
}

static void
on_converted (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    ConvertData *data = user_data;
    g_autoptr (GError) error = NULL;

    if (g_subprocess_wait_finish (G_SUBPROCESS (source), result, &error) &&
        !g_cancellable_is_cancelled (data->cancellable) &&
        g_file_query_exists (data->pdf, NULL))
    {
        load_document (data->self, data->pdf);
    }
    else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        g_subprocess_force_exit (G_SUBPROCESS (source));
    }

    convert_data_free (data);
}

/* OnlyOffice's headless converter; needs the font index the app writes
 * on its first start. Same lookup as naughty-sushi. */
static GSubprocess *
spawn_onlyoffice (const char  *doc_path,
                  const char  *pdf_path,
                  char       **params_path)
{
    static const char *x2t_paths[] =
    {
        "/snap/onlyoffice-desktopeditors/current/opt/onlyoffice/desktopeditors/converter/x2t",
        "/opt/onlyoffice/desktopeditors/converter/x2t",
    };
    g_autofree char *snap_fonts = g_build_filename (g_get_home_dir (), "snap", "onlyoffice-desktopeditors",
                                                    "current", ".local", "share", "onlyoffice",
                                                    "desktopeditors", "data", "fonts", NULL);
    g_autofree char *native_fonts = g_build_filename (g_get_user_data_dir (), "onlyoffice",
                                                      "desktopeditors", "data", "fonts", NULL);
    const char *font_dirs[] = { snap_fonts, native_fonts };
    const char *x2t = NULL;
    const char *fonts = NULL;
    g_autofree char *all_fonts = NULL;
    g_autofree char *temp_dir = NULL;
    g_autofree char *params = NULL;

    for (gsize i = 0; i < G_N_ELEMENTS (x2t_paths) && x2t == NULL; i++)
    {
        if (g_file_test (x2t_paths[i], G_FILE_TEST_IS_EXECUTABLE))
        {
            x2t = x2t_paths[i];
        }
    }
    for (gsize i = 0; i < G_N_ELEMENTS (font_dirs) && fonts == NULL; i++)
    {
        g_autofree char *index = g_build_filename (font_dirs[i], "AllFonts.js", NULL);

        if (g_file_test (index, G_FILE_TEST_IS_REGULAR))
        {
            fonts = font_dirs[i];
        }
    }
    if (x2t == NULL || fonts == NULL)
    {
        return NULL;
    }

    all_fonts = g_build_filename (fonts, "AllFonts.js", NULL);
    temp_dir = g_strconcat (pdf_path, ".tmp", NULL);
    g_mkdir_with_parents (temp_dir, 0700);
    /* 513 is x2t's code for PDF output */
    params = g_markup_printf_escaped ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                      "<TaskQueueDataConvert>"
                                      "<m_sFileFrom>%s</m_sFileFrom>"
                                      "<m_sFileTo>%s</m_sFileTo>"
                                      "<m_nFormatTo>513</m_nFormatTo>"
                                      "<m_sTempDir>%s</m_sTempDir>"
                                      "<m_sAllFontsPath>%s</m_sAllFontsPath>"
                                      "<m_sFontDir>%s</m_sFontDir>"
                                      "</TaskQueueDataConvert>",
                                      doc_path, pdf_path, temp_dir, all_fonts, fonts);
    *params_path = g_strconcat (pdf_path, ".xml", NULL);
    if (!g_file_set_contents (*params_path, params, -1, NULL))
    {
        return NULL;
    }

    return g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL, x2t, *params_path, NULL);
}

/* LibreOffice writes <name>.pdf into the output folder */
static GSubprocess *
spawn_libreoffice (const char *doc_path,
                   const char *out_dir)
{
    g_autofree char *soffice = g_find_program_in_path ("soffice");

    if (soffice == NULL)
    {
        return NULL;
    }

    return g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL, soffice, "--headless", "--convert-to", "pdf",
                             "--outdir", out_dir, doc_path, NULL);
}

static void
load_office (NautilusPreviewPane *self,
             GFile               *location)
{
    g_autofree char *doc_path = g_file_get_path (location);
    g_autofree char *uri = g_file_get_uri (location);
    g_autofree char *key = g_strdup_printf ("%s-%" G_GUINT64_FORMAT, uri,
                                            (guint64) nautilus_file_get_mtime (self->file));
    g_autofree char *md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, key, -1);
    g_autofree char *out_dir = g_build_filename (g_get_user_cache_dir (), "naughty", "previews", md5, NULL);
    g_autofree char *pdf_path = g_build_filename (out_dir, "preview.pdf", NULL);
    g_autoptr (GSubprocess) process = NULL;
    ConvertData *data;

    /* shortcut: converted PDFs stay in ~/.cache/naughty/previews, keyed by URI and mtime; add cleanup if it grows */
    g_mkdir_with_parents (out_dir, 0700);
    data = g_new0 (ConvertData, 1);
    data->self = g_object_ref (self);
    data->cancellable = g_object_ref (self->cancellable);
    data->pdf = g_file_new_for_path (pdf_path);

    if (g_file_test (pdf_path, G_FILE_TEST_IS_REGULAR))
    {
        load_document (self, data->pdf);
        convert_data_free (data);
        return;
    }

    process = spawn_onlyoffice (doc_path, pdf_path, &data->params_path);
    if (process == NULL)
    {
        g_autofree char *base = g_file_get_basename (location);
        char *dot = strrchr (base, '.');
        g_autofree char *lo_pdf = NULL;

        if (dot != NULL)
        {
            *dot = '\0';
        }
        lo_pdf = g_strconcat (base, ".pdf", NULL);
        g_object_unref (data->pdf);
        data->pdf = g_file_new_build_filename (out_dir, lo_pdf, NULL);
        process = spawn_libreoffice (doc_path, out_dir);
    }
    if (process == NULL)
    {
        convert_data_free (data);
        return;
    }

    g_subprocess_wait_async (process, data->cancellable, on_converted, data);
}

/* ---- Text ---- */

static void
on_text_loaded (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (NautilusPreviewPane) self = user_data;
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), result, NULL);
    gsize length;
    const char *text;
    const char *end;

    if (bytes == NULL)
    {
        return;
    }

    text = g_bytes_get_data (bytes, &length);
    /* Show only the valid UTF-8 part, a cut may land mid-character */
    g_utf8_validate_len (text, length, &end);
    gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view)),
                              text, end - text);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "text");
}

static void
on_text_opened (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (NautilusPreviewPane) self = user_data;
    g_autoptr (GFileInputStream) stream = g_file_read_finish (G_FILE (source), result, NULL);

    if (stream == NULL)
    {
        return;
    }

    g_input_stream_read_bytes_async (G_INPUT_STREAM (stream), TEXT_PREVIEW_BYTES,
                                     G_PRIORITY_DEFAULT, self->cancellable,
                                     on_text_loaded, g_object_ref (self));
}

/* ---- Images in full quality ---- */

static void
on_frame_loaded (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
    g_autoptr (NautilusPreviewPane) self = user_data;
    g_autoptr (GlyFrame) frame = gly_image_next_frame_finish (GLY_IMAGE (source), result, NULL);
    g_autoptr (GdkTexture) texture = NULL;

    if (frame == NULL)
    {
        return;
    }

    texture = gly_gtk_frame_get_texture (frame);
    gtk_picture_set_paintable (GTK_PICTURE (self->picture), GDK_PAINTABLE (texture));
}

static void
on_image_loaded (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
    g_autoptr (NautilusPreviewPane) self = user_data;
    g_autoptr (GlyImage) image = gly_loader_load_finish (GLY_LOADER (source), result, NULL);

    if (image == NULL)
    {
        return;
    }

    gly_image_next_frame_async (image, self->cancellable, on_frame_loaded, g_object_ref (self));
}

/* ---- Selection ---- */

static void
show_file (NautilusPreviewPane *self)
{
    const char *mime = nautilus_file_get_mime_type (self->file);
    g_autoptr (GFile) location = nautilus_file_get_location (self->file);

    show_details (self);
    /* Until a richer preview is ready, and for every other type */
    show_thumbnail (self);

    /* Rich previews read local files only; others keep the thumbnail */
    if (mime == NULL || !g_file_is_native (location) || nautilus_file_is_directory (self->file) ||
        nautilus_file_get_size (self->file) == 0)
    {
        return;
    }

    if (mime_in (mime, document_types, G_N_ELEMENTS (document_types)))
    {
        load_document (self, location);
    }
    else if (mime_in (mime, office_types, G_N_ELEMENTS (office_types)))
    {
        load_office (self, location);
    }
    else if (g_content_type_is_a (mime, "text/plain"))
    {
        g_file_read_async (location, G_PRIORITY_DEFAULT, self->cancellable,
                           on_text_opened, g_object_ref (self));
    }
    else if (g_str_has_prefix (mime, "image/"))
    {
        g_autoptr (GlyLoader) loader = gly_loader_new (location);

        gly_loader_load_async (loader, self->cancellable, on_image_loaded, g_object_ref (self));
    }
}

static void
show_message (NautilusPreviewPane *self,
              char                *message)
{
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
    set_label (self->name, message);
    set_label (self->type, NULL);
    set_label (self->size, NULL);
    set_label (self->modified, NULL);
}

static void
on_file_changed (NautilusPreviewPane *self)
{
    /* Size or details may arrive after the selection */
    show_details (self);
}

static void
set_file (NautilusPreviewPane *self,
          NautilusFile        *file)
{
    if (self->file == file)
    {
        return;
    }

    /* Drop the work started for the previous file */
    PpsJob *old_job = g_object_get_data (G_OBJECT (self), "document-job");

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    self->cancellable = g_cancellable_new ();
    if (old_job != NULL)
    {
        pps_job_cancel (old_job);
        g_object_set_data (G_OBJECT (self), "document-job", NULL);
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
        if (self->file != selection->data)
        {
            set_file (self, selection->data);
            show_file (self);
        }
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

static GtkWidget *
scrolled (GtkWidget *child)
{
    GtkWidget *scroll = gtk_scrolled_window_new ();

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), child);
    return scroll;
}

static void
nautilus_preview_pane_init (NautilusPreviewPane *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *details = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

    if (!pps_init ())
    {
        g_warning ("Papers could not start: documents show thumbnails only");
    }

    self->cancellable = g_cancellable_new ();
    gtk_widget_set_size_request (GTK_WIDGET (self), 220, -1);

    self->stack = gtk_stack_new ();
    gtk_widget_set_vexpand (self->stack, TRUE);
    gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    gtk_stack_add_named (GTK_STACK (self->stack), gtk_box_new (GTK_ORIENTATION_VERTICAL, 0), "empty");

    self->picture = gtk_picture_new ();
    gtk_picture_set_content_fit (GTK_PICTURE (self->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink (GTK_PICTURE (self->picture), TRUE);
    gtk_widget_set_margin_start (self->picture, 18);
    gtk_widget_set_margin_end (self->picture, 18);
    gtk_widget_set_margin_top (self->picture, 18);
    gtk_stack_add_named (GTK_STACK (self->stack), self->picture, "picture");

    {
        g_autoptr (PpsAnnotationModel) annotation_model = pps_annotation_model_new ();

        /* annotation-model can only be set at construction */
        self->document_model = g_object_new (PPS_TYPE_DOCUMENT_MODEL,
                                             "annotation-model", annotation_model, NULL);
    }
    pps_document_model_set_continuous (self->document_model, TRUE);
    pps_document_model_set_sizing_mode (self->document_model, PPS_SIZING_FIT_WIDTH);
    self->undo_context = pps_undo_context_new (self->document_model);
    self->annotations_context = pps_annotations_context_new (self->document_model, self->undo_context);
    self->search_context = pps_search_context_new (self->document_model);
    self->document_view = GTK_WIDGET (pps_view_new ());
    /* Papers 50 needs the view's contexts set, even for a read-only view */
    pps_view_set_model (PPS_VIEW (self->document_view), self->document_model);
    pps_view_set_annotations_context (PPS_VIEW (self->document_view), self->annotations_context);
    pps_view_set_search_context (PPS_VIEW (self->document_view), self->search_context);
    gtk_stack_add_named (GTK_STACK (self->stack), scrolled (self->document_view), "document");

    self->text_view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (self->text_view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (self->text_view), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin (GTK_TEXT_VIEW (self->text_view), 12);
    gtk_text_view_set_right_margin (GTK_TEXT_VIEW (self->text_view), 12);
    gtk_text_view_set_top_margin (GTK_TEXT_VIEW (self->text_view), 12);
    gtk_stack_add_named (GTK_STACK (self->stack), scrolled (self->text_view), "text");

    gtk_widget_set_margin_start (details, 18);
    gtk_widget_set_margin_end (details, 18);
    gtk_widget_set_margin_top (details, 12);
    gtk_widget_set_margin_bottom (details, 18);
    self->name = add_label (details, "heading");
    self->type = add_label (details, "dim-label");
    self->size = add_label (details, "dim-label");
    self->modified = add_label (details, "dim-label");

    gtk_box_append (GTK_BOX (box), self->stack);
    gtk_box_append (GTK_BOX (box), details);
    adw_bin_set_child (ADW_BIN (self), box);

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
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    set_window_slot (self, NULL);
    /* Drop the view before the model and contexts it uses */
    adw_bin_set_child (ADW_BIN (self), NULL);
    g_clear_object (&self->search_context);
    g_clear_object (&self->annotations_context);
    g_clear_object (&self->undo_context);
    g_clear_object (&self->document_model);

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
