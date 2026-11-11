#!/bin/sh
# Build an installable .deb of Naughty and its previewer (naughty-sushi).
#
# Everything goes into /opt/naughty, so no file of the system "nautilus"
# package is touched (same binary name, nautilus.mo, extension library).
# Only these files land outside /opt/naughty:
#   /usr/bin/naughty                               wrapper
#   /usr/share/applications/io.github.niccostantini.Naughty.desktop
#   /usr/share/icons/hicolor/{scalable,symbolic}/apps/io.github.niccostantini.Naughty*.svg
#   /usr/share/applications/io.github.niccostantini.Naughty.Previewer.desktop
#   /usr/share/icons/hicolor/scalable/apps/io.github.niccostantini.Naughty.Previewer.svg
#   /usr/share/dbus-1/services/io.github.niccostantini.Naughty.Previewer.service
#   /usr/share/doc/naughty/
#   /usr/share/man/man1/naughty.1.gz
#
# Usage: packaging/build-deb.sh            (run from anywhere in the repo)
# Env:   WORK=dir      build and staging folder (default: <repo>/build-deb)
#        SUSHI_SRC=dir naughty-sushi checkout (default: <repo>/subprojects/sushi,
#                      cloned from GitHub when missing)
#        DEB_REV=...   Debian revision (default: naughty2)
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$REPO/build-deb}
SUSHI_SRC=${SUSHI_SRC:-$REPO/subprojects/sushi}
SUSHI_URL=https://github.com/niccostantini/naughty-sushi.git
DEB_REV=${DEB_REV:-naughty2}
PREFIX=/opt/naughty
APP_ID=io.github.niccostantini.Naughty

UPSTREAM_VERSION=$(sed -n "s/^ *version: *'\([^']*\)'.*/\1/p" "$REPO/meson.build" | head -n 1)
VERSION=$UPSTREAM_VERSION-$DEB_REV
ARCH=$(dpkg --print-architecture)
DEST=$WORK/root
DEB=$WORK/naughty_${VERSION}_$ARCH.deb

for tool in meson ninja dpkg-deb dpkg-shlibdeps git; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done

mkdir -p "$WORK"
if [ ! -f "$SUSHI_SRC/meson.build" ]; then
    SUSHI_SRC=$WORK/sushi
    [ -d "$SUSHI_SRC" ] || git clone --depth 1 --branch fork "$SUSHI_URL" "$SUSHI_SRC"
fi

# --- Build and stage --------------------------------------------------------
build () { # build <srcdir> <builddir> <meson options...>
    src=$1 bld=$2; shift 2
    if [ -d "$bld" ]; then
        meson setup --reconfigure "$bld" "$src" "$@"
    else
        meson setup "$bld" "$src" "$@"
    fi
    # ninja does not always notice edited Blueprint files
    find "$src" -name '*.blp' -exec touch {} +
    ninja -C "$bld"
    meson install -C "$bld" --destdir "$DEST" --no-rebuild --strip
}

rm -rf "$DEST"
build "$REPO" "$WORK/naughty" --prefix=$PREFIX --buildtype=release \
    -Dextensions=false -Dselinux=disabled -Dtests=none
build "$SUSHI_SRC" "$WORK/sushi-build" --prefix=$PREFIX --buildtype=release \
    -Dprofile=default

O=$DEST$PREFIX
LIBDIR=$(cd "$O/lib" && ls -d *-linux-gnu* | head -n 1)

# Naughty links the system libnautilus-extension (libnautilus-extension4), so
# system extensions and Naughty share one copy. Drop the development files and
# our own copy of the library.
rm -rf "$O/include" "$O/share/gir-1.0" "$O/share/sushi/gir-1.0" \
    "$O/lib/$LIBDIR/pkgconfig" "$O/lib/$LIBDIR"/libnautilus-extension.so* \
    "$O/lib/$LIBDIR/girepository-1.0"
# D-Bus services under /opt are never read; the previewer one goes to /usr below.
rm -rf "$O/share/dbus-1"

# Schemas: compiled here, so no maintainer script is needed.
glib-compile-schemas "$O/share/glib-2.0/schemas"

# --- Files outside /opt -----------------------------------------------------
install -d "$DEST/usr/bin" "$DEST/usr/share/applications" \
    "$DEST/usr/share/dbus-1/services" "$DEST/usr/share/doc/naughty" \
    "$DEST/usr/share/icons/hicolor/scalable/apps" "$DEST/usr/share/icons/hicolor/symbolic/apps" \
    "$DEST/usr/share/man/man1"

cat > "$DEST/usr/bin/naughty" <<EOF
#!/bin/sh
# Run Naughty from $PREFIX, beside the system Files app.
export GSETTINGS_SCHEMA_DIR="$PREFIX/share/glib-2.0/schemas\${GSETTINGS_SCHEMA_DIR:+:\$GSETTINGS_SCHEMA_DIR}"
export XDG_DATA_DIRS="$PREFIX/share:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec $PREFIX/bin/nautilus "\$@"
EOF
chmod 755 "$DEST/usr/bin/naughty"

# Upstream's manual page, renamed for the naughty command.
sed -e 's/^\.TH Nautilus /.TH Naughty /' \
    -e 's/^nautilus \\- a file manager for GNOME$/naughty \\- file manager forked from GNOME Files/' \
    -e 's/^\.B nautilus$/.B naughty/' \
    -e 's/^is a file manager, designed for the GNOME desktop\.$/is a personal fork of GNOME Files (Nautilus); it installs in \/opt\/naughty./' \
    -e 's/^Quit Nautilus\.$/Quit Naughty./' \
    -e 's|^https://apps.gnome.org/Nautilus$|https://github.com/niccostantini/naughty|' \
    -e 's|^https://gitlab.gnome.org/GNOME/nautilus/issues$|https://github.com/niccostantini/naughty/issues|' \
    "$REPO/docs/nautilus.1" | gzip -9n > "$DEST/usr/share/man/man1/naughty.1.gz"

# The previewer's launcher and icon, so the desktop can name its window.
# Its own name (not gnome-sushi's) lets both packages be installed together.
PV=$APP_ID.Previewer
sed 's|^Exec=sushi |Exec=/opt/naughty/bin/sushi |' "$O/share/applications/$PV.desktop" \
    > "$DEST/usr/share/applications/$PV.desktop"
rm "$O/share/applications/$PV.desktop"
mv "$O/share/icons/hicolor/scalable/apps/$PV.svg" "$DEST/usr/share/icons/hicolor/scalable/apps/"

# The launcher moves to /usr so the desktop sees it; it starts the wrapper
# directly because D-Bus activation would skip the environment above.
sed -e 's|^Exec=nautilus |Exec=naughty |' -e 's|^DBusActivatable=true|DBusActivatable=false|' \
    "$O/share/applications/$APP_ID.desktop" > "$DEST/usr/share/applications/$APP_ID.desktop"
rm "$O/share/applications/$APP_ID.desktop"
grep -q '^Exec=naughty ' "$DEST/usr/share/applications/$APP_ID.desktop"
grep -q '^DBusActivatable=false' "$DEST/usr/share/applications/$APP_ID.desktop"

mv "$O/share/icons/hicolor/scalable/apps/$APP_ID.svg" "$DEST/usr/share/icons/hicolor/scalable/apps/"
mv "$O/share/icons/hicolor/symbolic/apps/$APP_ID-symbolic.svg" "$DEST/usr/share/icons/hicolor/symbolic/apps/"

cat > "$DEST/usr/share/dbus-1/services/$PV.service" <<EOF
[D-BUS Service]
Name=$PV
Exec=$PREFIX/libexec/$PV
EOF

find "$O" -type d -empty -delete
chmod 644 "$O/lib/$LIBDIR/sushi/libsushi-1.0.so"

# /opt is the point: no clash with the files of the system nautilus package.
install -d "$DEST/usr/share/lintian/overrides"
cat > "$DEST/usr/share/lintian/overrides/naughty" <<EOF
naughty: dir-or-file-in-opt [opt/naughty/*]
# Sushi's web extension keeps the RUNPATH of its private library folder.
naughty: custom-library-search-path RUNPATH $PREFIX/lib/$LIBDIR/sushi [opt/naughty/lib/$LIBDIR/sushi/web-process-extensions/libsushiwebprocessextension.so]
# Sushi's own launchers start with "#!/usr/bin/env -S gjs -m"; coreutils env supports -S.
naughty: unusual-interpreter -S [opt/naughty/bin/sushi]
naughty: unusual-interpreter -S [opt/naughty/libexec/$PV]
EOF

{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: Naughty"
    echo "Source: https://github.com/niccostantini/naughty"
    echo
    echo "Files: *"
    echo "Copyright: 1999-2026 The Nautilus authors (see the AUTHORS file and source headers)"
    echo "           2026 Niccolò Costantini"
    echo "License: GPL-3.0+"
    echo " On Debian systems the full license is in /usr/share/common-licenses/GPL-3."
    echo " Some source files are under LGPL-2.1+ or carry the GStreamer exception;"
    echo " both are compatible with GPL-3.0+, the license of the program as a whole."
    echo " The AppStream metainfo is under CC0-1.0."
    echo
    echo "Files: opt/naughty/libexec/* opt/naughty/bin/sushi opt/naughty/share/sushi/* opt/naughty/lib/*/sushi/*"
    echo "Copyright: 2011-2026 The Sushi authors (see the source headers)"
    echo "           2026 Niccolò Costantini"
    echo "Source: https://github.com/niccostantini/naughty-sushi"
    echo "License: GPL-2.0+"
    echo " On Debian systems the full license is in /usr/share/common-licenses/GPL-2."
} > "$DEST/usr/share/doc/naughty/copyright"
printf 'naughty (%s) unstable; urgency=medium\n\n  * Naughty commit %s, naughty-sushi commit %s.\n\n -- Niccolò Costantini <65295160+niccostantini@users.noreply.github.com>  %s\n' \
    "$VERSION" "$(git -C "$REPO" rev-parse --short=12 HEAD)$(git -C "$REPO" diff --quiet HEAD || echo -dirty)" \
    "$(git -C "$SUSHI_SRC" rev-parse --short=12 HEAD)$(git -C "$SUSHI_SRC" diff --quiet HEAD || echo -dirty)" "$(date -R)" \
    | gzip -9n > "$DEST/usr/share/doc/naughty/changelog.Debian.gz"

# --- Control file -----------------------------------------------------------
# dpkg-shlibdeps wants a debian/control to exist; it reads nothing from it here.
SHL=$WORK/shlibs
rm -rf "$SHL" && mkdir -p "$SHL/debian"
printf 'Source: naughty\n\nPackage: naughty\nArchitecture: any\n' > "$SHL/debian/control"
ELFS=$(find "$DEST" -type f \( -perm -u+x -o -name '*.so*' \) -exec sh -c 'head -c4 "$1" | grep -q ELF' _ {} \; -print)
# Two steps: in a pipeline, set -e would miss a dpkg-shlibdeps failure.
SHLIBS=$(cd "$SHL" && dpkg-shlibdeps -O -l"$O/lib/$LIBDIR/sushi" $ELFS)
SHLIBS=$(printf '%s\n' "$SHLIBS" | sed -n 's/^shlibs:Depends=//p')

# GObject introspection modules imported by the previewer (gjs), not visible
# to dpkg-shlibdeps.
GIR="gjs, gir1.2-glib-2.0, gir1.2-gtk-4.0, gir1.2-adw-1, gir1.2-pango-1.0, gir1.2-graphene-1.0, \
gir1.2-gly-2, gir1.2-glygtk4-2, gir1.2-gstreamer-1.0, gir1.2-gst-plugins-base-1.0, \
gir1.2-gtksource-5, gir1.2-papers-4.0, gir1.2-webkit-6.0"

install -d "$DEST/DEBIAN"
cat > "$DEST/DEBIAN/control" <<EOF
Package: naughty
Version: $VERSION
Architecture: $ARCH
Maintainer: Niccolò Costantini <65295160+niccostantini@users.noreply.github.com>
Installed-Size: $(du -sk --exclude=DEBIAN "$DEST" | cut -f1)
Depends: $SHLIBS, $GIR, gsettings-desktop-schemas, gvfs, shared-mime-info
Recommends: gvfs-backends, localsearch, librsvg2-common, xdg-desktop-portal-gnome
Suggests: libreoffice-core
Section: gnome
Priority: optional
Homepage: https://github.com/niccostantini/naughty
Description: file manager forked from GNOME Files, with split view and previews
 Naughty is a personal fork of GNOME Files (Nautilus) 50 that adds a split
 view, type-ahead jump, a copy queue with pause and a preview pane. It
 installs in /opt/naughty, beside the system Files app, and starts with the
 "naughty" command.
 .
 It includes naughty-sushi, a patched GNOME Sushi previewer with its own
 D-Bus name, so gnome-sushi can stay installed for the system Files app.
 .
 Previews of Office documents need LibreOffice or the OnlyOffice Desktop
 Editors snap. PDF previews work without either.
 .
 Naughty is not made, endorsed or supported by the GNOME Foundation.
EOF

# Fixed permissions, whatever the umask of the build.
find "$DEST" -type d -exec chmod 755 {} +
find "$DEST" -type f -exec chmod go-w {} +

dpkg-deb --build --root-owner-group -Zxz "$DEST" "$DEB"
echo "Built $DEB"
