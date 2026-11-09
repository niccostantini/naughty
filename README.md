<p align="center">
  <img src="data/icons/hicolor/scalable/apps/org.gnome.NautilusDevel.svg" alt="Naughty icon: a winking neon face with a tiara and a heart eye" width="128">
</p>

<h1 align="center">Naughty</h1>

<p align="center">
  <b>GNOME Files with the two things it always lacked: a split view and a real spacebar preview.</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
  <img src="https://img.shields.io/badge/based%20on-Nautilus%2050-4a86cf" alt="Based on Nautilus 50">
  <img src="https://img.shields.io/badge/tested%20on-Ubuntu%2026.04-e95420" alt="Tested on Ubuntu 26.04">
</p>

Naughty is an unofficial fork of [GNOME Files (Nautilus)](https://gitlab.gnome.org/GNOME/nautilus) 50.
It keeps everything you know from Nautilus. It adds the features people miss after using
Finder or Windows Explorer. It installs beside your system file manager, so you can try it
and remove it without touching the system.

## What it adds

- **Split view.** Press **F3** to open a second pane next to the current one. You can drag
  files between the two panes. The inactive pane dims, so you always see which one you work in.
  **Ctrl+W** in the second pane closes the split.
- **Spacebar preview for documents.** Select a file and press **Space**. PDFs open in a
  scrollable viewer with page buttons. Word, Excel and PowerPoint files (`.docx`, `.xlsx`,
  `.pptx`, also ODF) get a full preview too.
- **Preview everywhere.** The preview also works for files in the Trash, inside ZIP archives,
  on network shares (SMB, SFTP) and on the Ubuntu desktop.
- **Its own face.** A neon icon and its own name, so you never mix it up with the system Files.

The spacebar previewer is a patched copy of GNOME Sushi:
[naughty-sushi](https://github.com/niccostantini/naughty-sushi).

## Is it for you?

Naughty is a good fit if all of these are true:

- You use **GNOME 50**. Naughty was built and tested only on **Ubuntu 26.04**.
- You are comfortable building software from source. There are no packages yet.
- You want split view or Office previews more than a polished, supported product.

Naughty is a personal project. It does not follow GNOME's release schedule, and it has no
automatic updates. Bugs belong in [this repository's issues](https://github.com/niccostantini/naughty/issues),
**never** in GNOME's bug tracker.

## Install

> All of this installs into `~/.local`. It needs `sudo` only for the build dependencies.

**1. Install the build dependencies** (Ubuntu 26.04):

```bash
sudo apt install meson ninja-build git blueprint-compiler libgtk-4-dev libadwaita-1-dev libgdk-pixbuf-2.0-dev libglycin-2-dev libglycin-gtk4-2-dev libgnome-autoar-0-dev libgnome-desktop-4-dev libgstreamer-plugins-base1.0-dev libicu-dev libportal-dev libportal-gtk4-dev libtinysparql-dev libcloudproviders-dev libwayland-dev libgirepository-2.0-dev libpapers-dev libgtksourceview-5-dev libwebkitgtk-6.0-dev gir1.2-papers-4.0 gir1.2-gtksource-5 gir1.2-webkit-6.0
```

**2. Build Naughty:**

```bash
git clone https://github.com/niccostantini/naughty.git
cd naughty
meson setup build -Dprofile=Devel -Dextensions=false -Dselinux=disabled -Dtests=none --prefix="$HOME/.local/opt/naughty"
meson install -C build
```

**3. Build the previewer** (from the same `naughty` folder):

```bash
git clone https://github.com/niccostantini/naughty-sushi.git subprojects/sushi
meson setup subprojects/sushi/build subprojects/sushi -Dprofile=default --prefix="$HOME/.local/opt/naughty"
meson install -C subprojects/sushi/build
```

**4. Add a launcher, the icon and the previewer service:**

```bash
P="$HOME/.local/opt/naughty"
cat > "$P/bin/naughty" <<EOF
#!/bin/sh
export GSETTINGS_SCHEMA_DIR="$P/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$P/share:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$P/bin/nautilus" "\$@"
EOF
chmod +x "$P/bin/naughty"
mkdir -p ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps ~/.local/share/dbus-1/services
sed -e "s|^Exec=nautilus|Exec=$P/bin/naughty|" -e "s|^DBusActivatable=true|DBusActivatable=false|" "$P/share/applications/org.gnome.Nautilus.Devel.desktop" > ~/.local/share/applications/org.gnome.Nautilus.Devel.desktop
ln -sf "$P/share/icons/hicolor/scalable/apps/org.gnome.Nautilus.Devel.svg" ~/.local/share/icons/hicolor/scalable/apps/
ln -sf "$P/share/dbus-1/services/org.gnome.NautilusPreviewer.service" ~/.local/share/dbus-1/services/
```

Naughty now appears in your app grid. Open it, select a PDF and press **Space**.

**5. Optional: make Naughty open your folders.**

```bash
xdg-mime default org.gnome.Nautilus.Devel.desktop inode/directory
```

### Office previews

Office files need a converter. Naughty uses whichever of these it finds:

- **LibreOffice**, if it is installed.
- **OnlyOffice Desktop Editors** from the Snap Store (`sudo snap install onlyoffice-desktopeditors`).
  Open OnlyOffice once after you install it, so it builds its font list.

PDF previews work without either one.

## Uninstall

```bash
xdg-mime default org.gnome.Nautilus.desktop inode/directory
rm -rf ~/.local/opt/naughty
rm -f ~/.local/share/applications/org.gnome.Nautilus.Devel.desktop ~/.local/share/icons/hicolor/scalable/apps/org.gnome.Nautilus.Devel.svg ~/.local/share/dbus-1/services/org.gnome.NautilusPreviewer.service
```

Your system Files app was never changed, so it works as before.

## Known limits

- **Shared settings.** Naughty and the system Files read the same preferences
  (`org.gnome.nautilus`). A change in one appears in the other.
- **No extensions.** Nautilus extensions (for example `nautilus-python` scripts or cloud-sync
  emblems) do not load. They are off in the build because Ubuntu 26.04 lacks `gexiv2` 0.16.
- **Shared previewer.** After step 4, the system Files app also uses Naughty's previewer.
  The uninstall steps undo this.
- **Previews of remote files make a copy.** Files from ZIP archives and network shares are
  copied to `~/.cache/sushi` before the preview. That folder is not emptied automatically.
- **Internal name.** The app still uses the ID `org.gnome.Nautilus.Devel`. A Naughty-specific
  ID is planned.

## Roadmap

- Pause and queue file copies
- Miller column view, like Finder's column view
- File tags
- A package, so you do not have to build from source

## License and credits

Naughty is free software under the **GNU General Public License, version 3 or later**. See
[`LICENSE`](LICENSE). Almost all of the code is the work of the
[Nautilus contributors](https://gitlab.gnome.org/GNOME/nautilus/-/graphs/main). Their copyright
notices stay in the source files. Every change made for Naughty is a separate commit on top of
the upstream `gnome-50` branch, so you can see exactly what differs.

The previewer, [naughty-sushi](https://github.com/niccostantini/naughty-sushi), keeps GNOME
Sushi's license, GPL-2.0-or-later. Naughty does not include LibreOffice or OnlyOffice. It runs
the copy you installed, as a separate program.

Naughty is **not** made, endorsed or supported by the GNOME Foundation. "GNOME", "Nautilus"
and "Files" refer to the original project. The Naughty icon is original artwork made for this
fork.

---

The upstream Nautilus README is in the
[GNOME repository](https://gitlab.gnome.org/GNOME/nautilus/-/blob/gnome-50/README.md).
