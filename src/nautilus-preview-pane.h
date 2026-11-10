/*
 * Copyright (C) 2026 Niccolò Costantini
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_PREVIEW_PANE (nautilus_preview_pane_get_type ())

G_DECLARE_FINAL_TYPE (NautilusPreviewPane, nautilus_preview_pane, NAUTILUS, PREVIEW_PANE, AdwBin)

G_END_DECLS
