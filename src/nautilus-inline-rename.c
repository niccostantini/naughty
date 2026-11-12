/*
 * Copyright (C) 2026 Niccolò Costantini
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * NautilusInlineRename:
 *
 * Renames a file in place, as in Finder: the name label of the item's cell is
 * hidden and an editable text view takes its place, with the name without the
 * extension selected. Enter confirms, Escape cancels, moving the focus away
 * confirms. Problems with the new name show up in a small popover below it.
 *
 * The text view wraps long names, so it also works with the multi-line names
 * of the grid view. Keys the text view does not use stop at the editor, so the
 * view's shortcuts and type-ahead don't act on the selection while typing.
 */

#include <glib/gi18n.h>

#include "nautilus-inline-rename.h"

#include "nautilus-directory.h"
#include "nautilus-file.h"
#include "nautilus-file-private.h"
#include "nautilus-filename-utilities.h"
#include "nautilus-filename-validator.h"

#define NAME_TARGET_CSS_CLASS "click-to-rename-target"

struct _NautilusInlineRename
{
    GObject parent_instance;

    /* All NULL/0 when not editing. */
    NautilusFile *file;
    NautilusFilenameValidator *validator;
    GtkWidget *item_ui;
    GtkWidget *label;
    GtkWidget *box;
    GtkWidget *text_view;
    GtkWidget *error_popover;
    GtkWidget *error_label;

    NautilusRenameCallback callback;
    gpointer callback_data;

    int text_height;
    guint fix_height_tick_id;

    gulong item_handler_id;
    gulong file_handler_id;
    guint commit_idle_id;
};

G_DEFINE_FINAL_TYPE (NautilusInlineRename, nautilus_inline_rename, G_TYPE_OBJECT)

static GtkWidget *
find_name_label (GtkWidget *widget)
{
    if (gtk_widget_has_css_class (widget, NAME_TARGET_CSS_CLASS))
    {
        return widget;
    }

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_name_label (child);

        if (found != NULL)
        {
            return found;
        }
    }

    return NULL;
}

static char *
get_text (NautilusInlineRename *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds (buffer, &start, &end);

    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
unparent_and_unref (GtkWidget **widget)
{
    if (gtk_widget_get_parent (*widget) != NULL)
    {
        gtk_widget_unparent (*widget);
    }
    g_clear_object (widget);
}

static void
finish (NautilusInlineRename *self)
{
    g_autoptr (GtkWidget) box = g_steal_pointer (&self->box);
    g_autoptr (GtkWidget) text_view = g_steal_pointer (&self->text_view);
    gboolean had_focus;

    if (self->file == NULL)
    {
        return;
    }

    g_clear_handle_id (&self->commit_idle_id, g_source_remove);
    g_clear_signal_handler (&self->item_handler_id, self->item_ui);
    g_clear_signal_handler (&self->file_handler_id, self->file);
    g_clear_object (&self->file);
    /* Dropping the validator also cancels its pending directory callbacks. */
    g_clear_object (&self->validator);

    if (self->fix_height_tick_id != 0)
    {
        gtk_widget_remove_tick_callback (text_view, self->fix_height_tick_id);
        self->fix_height_tick_id = 0;
    }

    had_focus = gtk_widget_has_focus (text_view);
    self->error_label = NULL;

    /* Give the focus back to the item, as the rename popover does. */
    if (had_focus)
    {
        for (GtkWidget *w = self->item_ui; w != NULL; w = gtk_widget_get_parent (w))
        {
            if (gtk_widget_get_focusable (w))
            {
                gtk_widget_grab_focus (w);
                break;
            }
        }
    }

    unparent_and_unref (&self->error_popover);
    unparent_and_unref (&box);
    gtk_widget_set_visible (self->label, TRUE);

    g_clear_object (&self->label);
    g_clear_object (&self->item_ui);
}

void
nautilus_inline_rename_cancel (NautilusInlineRename *self)
{
    finish (self);
}

gboolean
nautilus_inline_rename_is_active (NautilusInlineRename *self)
{
    return self->file != NULL;
}

static void
rename_to (NautilusInlineRename *self,
           const char           *new_name)
{
    g_autoptr (NautilusFile) file = g_object_ref (self->file);
    NautilusRenameCallback callback = self->callback;
    gpointer callback_data = self->callback_data;
    gboolean unchanged = g_strcmp0 (new_name, nautilus_file_get_display_name (file)) == 0;

    finish (self);

    if (!unchanged)
    {
        callback (file, new_name, callback_data);
    }
}

static void
on_name_accepted (NautilusInlineRename *self)
{
    g_autofree char *name = NULL;

    if (self->file == NULL)
    {
        return;
    }

    name = nautilus_filename_validator_get_new_name (self->validator);
    rename_to (self, name);
}

/* Confirm a valid name when the editor is left by other means than Enter,
 * else restore the old name. */
static gboolean
commit_or_cancel (gpointer user_data)
{
    NautilusInlineRename *self = user_data;
    g_autofree char *name = NULL;
    gboolean passed = FALSE;

    self->commit_idle_id = 0;

    if (self->file == NULL)
    {
        return G_SOURCE_REMOVE;
    }

    name = nautilus_filename_validator_get_new_name (self->validator);
    g_object_get (self->validator, "passed", &passed, NULL);

    if (passed)
    {
        rename_to (self, name);
    }
    else
    {
        finish (self);
    }

    return G_SOURCE_REMOVE;
}

static void
queue_commit (NautilusInlineRename *self)
{
    /* Not right away: this runs from focus and model changes, which should
     * not see the editor disappear while they are being emitted. */
    if (self->file != NULL && self->commit_idle_id == 0)
    {
        self->commit_idle_id = g_idle_add (commit_or_cancel, self);
    }
}

static void
on_focus_leave (GtkEventControllerFocus *controller,
                NautilusInlineRename    *self)
{
    GtkRoot *root;

    if (self->text_view == NULL)
    {
        return;
    }

    /* Switching to another window keeps editing, as in Finder. */
    root = gtk_widget_get_root (self->text_view);
    if (GTK_IS_WINDOW (root) && !gtk_window_is_active (GTK_WINDOW (root)))
    {
        return;
    }

    queue_commit (self);
}

static void
on_item_changed (NautilusInlineRename *self)
{
    /* The cell now shows another item (e.g. the folder changed). */
    queue_commit (self);
}

static void
on_file_changed (NautilusFile         *file,
                 NautilusInlineRename *self)
{
    if (nautilus_file_is_gone (file))
    {
        finish (self);
    }
}

static void
select_name_part (NautilusInlineRename *self,
                  gboolean              cycle)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view));
    g_autofree char *text = get_text (self);
    GtkTextIter start, end, sel_start, sel_end;
    gboolean all_selected;

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_get_selection_bounds (buffer, &sel_start, &sel_end);
    all_selected = gtk_text_iter_equal (&sel_start, &start) && gtk_text_iter_equal (&sel_end, &end);

    /* Like the popover's F2: whole name, then the name without extension. */
    if (nautilus_file_is_regular_file (self->file) && (!cycle || all_selected))
    {
        gtk_text_buffer_get_iter_at_offset (buffer, &end,
                                            nautilus_filename_get_extension_char_offset (text));
    }

    gtk_text_buffer_select_range (buffer, &start, &end);
}

static gboolean
on_editor_key_pressed (GtkEventControllerKey *controller,
                       guint                  keyval,
                       guint                  keycode,
                       GdkModifierType        state,
                       NautilusInlineRename  *self)
{
    switch (keyval)
    {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
        {
            nautilus_filename_validator_try_accept (self->validator);
            return GDK_EVENT_STOP;
        }

        case GDK_KEY_Escape:
        {
            finish (self);
            return GDK_EVENT_STOP;
        }

        case GDK_KEY_F2:
        {
            select_name_part (self, TRUE);
            return GDK_EVENT_STOP;
        }

        default:
        {
            return GDK_EVENT_PROPAGATE;
        }
    }
}

static gboolean
on_box_key_pressed (GtkEventControllerKey *controller,
                    guint                  keyval,
                    guint                  keycode,
                    GdkModifierType        state,
                    gpointer               user_data)
{
    /* Keys the text view ignored must not reach the view's shortcuts. */
    return GDK_EVENT_STOP;
}

static void
on_insert_text (GtkTextBuffer *buffer,
                GtkTextIter   *location,
                const char    *text,
                int            len,
                gpointer       user_data)
{
    g_autofree char *clean = NULL;

    if (strpbrk (text, "\r\n") == NULL)
    {
        return;
    }

    /* A pasted line break would end up in the file name. */
    clean = g_strndup (text, len < 0 ? strlen (text) : (gsize) len);
    g_strdelimit (clean, "\r\n", ' ');
    g_signal_stop_emission_by_name (buffer, "insert-text");
    gtk_text_buffer_insert (buffer, location, clean, -1);
}

/* GtkTextView finds its height only after laying out its lines, and the
 * list and grid views don't notice, leaving it 0 px tall (also when the name
 * wraps to a new line while typing). After each change, ask for a new size
 * on every frame until the height stops changing. */
static gboolean
fix_height_tick (GtkWidget     *text_view,
                 GdkFrameClock *frame_clock,
                 gpointer       user_data)
{
    NautilusInlineRename *self = user_data;
    int width = gtk_widget_get_width (text_view);
    int height;

    if (width == 0)
    {
        /* Not allocated yet. */
        return G_SOURCE_CONTINUE;
    }

    gtk_widget_measure (text_view, GTK_ORIENTATION_VERTICAL, width,
                        &height, NULL, NULL, NULL);
    if (height == self->text_height)
    {
        self->fix_height_tick_id = 0;
        return G_SOURCE_REMOVE;
    }

    self->text_height = height;
    gtk_widget_queue_resize (text_view);

    return G_SOURCE_CONTINUE;
}

static void
queue_fix_height (NautilusInlineRename *self)
{
    if (self->fix_height_tick_id == 0)
    {
        self->fix_height_tick_id = gtk_widget_add_tick_callback (self->text_view, fix_height_tick,
                                                                 self, NULL);
    }
}

static void
on_buffer_changed (NautilusInlineRename *self)
{
    g_autofree char *text = get_text (self);

    g_object_set (self->validator, "new-name", text, NULL);
    nautilus_filename_validator_validate (self->validator);
    queue_fix_height (self);
}

static void
on_feedback_changed (NautilusInlineRename *self)
{
    g_autofree char *feedback = NULL;

    if (self->text_view == NULL)
    {
        return;
    }

    g_object_get (self->validator, "feedback-text", &feedback, NULL);

    if (feedback != NULL)
    {
        gtk_label_set_text (GTK_LABEL (self->error_label), feedback);
        gtk_widget_add_css_class (self->text_view, "error");
        gtk_popover_popup (GTK_POPOVER (self->error_popover));
        gtk_popover_present (GTK_POPOVER (self->error_popover));
    }
    else
    {
        gtk_widget_remove_css_class (self->text_view, "error");
        gtk_popover_popdown (GTK_POPOVER (self->error_popover));
    }
}


gboolean
nautilus_inline_rename_start (NautilusInlineRename   *self,
                              GtkWidget              *item_ui,
                              NautilusFile           *file,
                              NautilusRenameCallback  callback,
                              gpointer                callback_data)
{
    g_autoptr (NautilusDirectory) directory = NULL;
    GtkWidget *label;
    GtkTextBuffer *buffer;
    GtkEventController *controller;
    const char *edit_name;

    g_return_val_if_fail (NAUTILUS_IS_INLINE_RENAME (self), FALSE);
    g_return_val_if_fail (NAUTILUS_IS_FILE (file), FALSE);

    finish (self);

    label = item_ui != NULL ? find_name_label (item_ui) : NULL;
    if (label == NULL ||
        !gtk_widget_get_mapped (label) ||
        !GTK_IS_BOX (gtk_widget_get_parent (label)))
    {
        /* Not on screen: the caller falls back to the popover. */
        return FALSE;
    }

    self->file = g_object_ref (file);
    self->item_ui = g_object_ref (item_ui);
    self->label = g_object_ref (label);
    self->callback = callback;
    self->callback_data = callback_data;

    if (nautilus_file_is_self_owned (file))
    {
        directory = nautilus_directory_get_for_file (file);
    }
    else
    {
        directory = nautilus_directory_ref (nautilus_file_get_directory (file));
    }

    /* A new validator for each rename, so an old answer can't confirm it. */
    self->validator = g_object_new (NAUTILUS_TYPE_FILENAME_VALIDATOR, NULL);
    nautilus_filename_validator_set_containing_directory (self->validator, directory);
    nautilus_filename_validator_set_target_is_folder (self->validator,
                                                      nautilus_file_is_directory (file));
    nautilus_filename_validator_set_original_name (self->validator,
                                                   nautilus_file_get_display_name (file));
    g_signal_connect_swapped (self->validator, "name-accepted",
                              G_CALLBACK (on_name_accepted), self);
    g_signal_connect_swapped (self->validator, "notify::feedback-text",
                              G_CALLBACK (on_feedback_changed), self);

    /* Own references: the cell may get destroyed while editing. */
    self->text_view = g_object_ref_sink (gtk_text_view_new ());
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_justification (GTK_TEXT_VIEW (self->text_view),
                                     gtk_label_get_justify (GTK_LABEL (label)));
    gtk_text_view_set_accepts_tab (GTK_TEXT_VIEW (self->text_view), FALSE);
    gtk_widget_set_hexpand (self->text_view, TRUE);
    gtk_widget_add_css_class (self->text_view, "inline-rename-editor");
    gtk_accessible_update_property (GTK_ACCESSIBLE (self->text_view),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _("New Filename"),
                                    -1);

    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->text_view));
    edit_name = nautilus_file_get_edit_name (file);
    gtk_text_buffer_set_text (buffer, edit_name, -1);
    g_object_set (self->validator, "new-name", edit_name, NULL);
    g_signal_connect (buffer, "insert-text", G_CALLBACK (on_insert_text), NULL);
    g_signal_connect_swapped (buffer, "changed", G_CALLBACK (on_buffer_changed), self);

    controller = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "key-pressed", G_CALLBACK (on_editor_key_pressed), self);
    gtk_widget_add_controller (self->text_view, controller);

    controller = gtk_event_controller_focus_new ();
    g_signal_connect (controller, "leave", G_CALLBACK (on_focus_leave), self);
    gtk_widget_add_controller (self->text_view, controller);

    self->box = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
    gtk_widget_set_hexpand (self->box, TRUE);
    gtk_widget_add_css_class (self->box, "inline-rename");
    gtk_box_append (GTK_BOX (self->box), self->text_view);

    controller = gtk_event_controller_key_new ();
    g_signal_connect (controller, "key-pressed", G_CALLBACK (on_box_key_pressed), NULL);
    gtk_widget_add_controller (self->box, controller);

    self->error_label = gtk_label_new (NULL);
    gtk_label_set_wrap (GTK_LABEL (self->error_label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (self->error_label), 30);
    self->error_popover = g_object_ref_sink (gtk_popover_new ());
    gtk_popover_set_child (GTK_POPOVER (self->error_popover), self->error_label);
    gtk_popover_set_autohide (GTK_POPOVER (self->error_popover), FALSE);
    gtk_popover_set_position (GTK_POPOVER (self->error_popover), GTK_POS_BOTTOM);
    gtk_widget_set_can_focus (self->error_popover, FALSE);
    gtk_widget_set_parent (self->error_popover, self->box);

    gtk_widget_insert_after (self->box, gtk_widget_get_parent (label), label);
    gtk_widget_set_visible (label, FALSE);

    self->item_handler_id = g_signal_connect_swapped (item_ui, "notify::item",
                                                      G_CALLBACK (on_item_changed), self);
    self->file_handler_id = g_signal_connect (file, "changed",
                                              G_CALLBACK (on_file_changed), self);

    gtk_widget_grab_focus (self->text_view);
    select_name_part (self, FALSE);
    self->text_height = -1;
    queue_fix_height (self);

    return TRUE;
}

static void
nautilus_inline_rename_dispose (GObject *object)
{
    finish (NAUTILUS_INLINE_RENAME (object));

    G_OBJECT_CLASS (nautilus_inline_rename_parent_class)->dispose (object);
}

static void
nautilus_inline_rename_class_init (NautilusInlineRenameClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_inline_rename_dispose;
}

static void
nautilus_inline_rename_init (NautilusInlineRename *self)
{
}

NautilusInlineRename *
nautilus_inline_rename_new (void)
{
    return g_object_new (NAUTILUS_TYPE_INLINE_RENAME, NULL);
}
