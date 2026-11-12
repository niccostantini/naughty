/*
 * Copyright (C) 2026 Niccolò Costantini
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "nautilus-types.h"
#include "nautilus-rename-file-popover.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_INLINE_RENAME (nautilus_inline_rename_get_type ())
G_DECLARE_FINAL_TYPE (NautilusInlineRename, nautilus_inline_rename, NAUTILUS, INLINE_RENAME, GObject)

NautilusInlineRename *nautilus_inline_rename_new       (void);

gboolean              nautilus_inline_rename_start     (NautilusInlineRename   *self,
                                                        GtkWidget              *item_ui,
                                                        NautilusFile           *file,
                                                        NautilusRenameCallback  callback,
                                                        gpointer                callback_data);
gboolean              nautilus_inline_rename_is_active (NautilusInlineRename   *self);
void                  nautilus_inline_rename_cancel    (NautilusInlineRename   *self);

G_END_DECLS
