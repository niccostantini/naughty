# Naughty

Naughty is a personal fork of [GNOME Files (Nautilus)](https://gitlab.gnome.org/GNOME/nautilus),
based on the `gnome-50` stable branch. It adds a split view (F3) and pairs with
[naughty-sushi](https://github.com/niccostantini/naughty-sushi), a fork of GNOME Sushi,
for spacebar previews of PDF and Office files.

Naughty is not affiliated with or endorsed by the GNOME Foundation. "GNOME" and
"Nautilus" are names of their respective owners. The code keeps its upstream
license, GPL-3.0-or-later (see `LICENSE`), and the original copyright notices.
All changes are in the commits on top of upstream.

The original upstream README follows.

---

# nautilus
[![Pipeline status](https://gitlab.gnome.org/GNOME/nautilus/badges/main/pipeline.svg)](https://gitlab.gnome.org/GNOME/nautilus/commits/main)
[![coverage report](https://gitlab.gnome.org/GNOME/nautilus/badges/main/coverage.svg)](https://gitlab.gnome.org/GNOME/nautilus/commits/main) 

This is the project of the [Files](https://apps.gnome.org/Nautilus/) app, a file browser for
GNOME, internally known by its historical name `nautilus`.

## Supported version
Only the latest version of Files as provided upstream is supported. Try out the [Flatpak nightly](https://welcome.gnome.org/en/app/Nautilus/#installing-a-nightly-build) installation before filling issues to ensure the installation is reproducible and doesn't have downstream changes on it. In case you cannot reproduce in the nightly installation, don't hesitate to file an issue in your distribution. This is to ensure the issue is well triaged and reaches the proper people.

## Runtime dependencies
- [Bubblewrap](https://github.com/containers/bubblewrap) installed. Used for security reasons.
- [LocalSearch](https://gitlab.gnome.org/GNOME/localsearch) properly set up and with all features enabled. Used for fast search and metadata extraction, starred files and batch renaming.
- [xdg-user-dirs-gtk](https://gitlab.gnome.org/GNOME/xdg-user-dirs-gtk) installed.  Used to create the default bookmarks and update localization.

## Discourse

For more informal discussion we use [GNOME Discourse](https://discourse.gnome.org/tags/nautilus) in the Applications category with the `nautilus` tag. Feel free to open a topic there.

## Extensions

Documentation for the libnautilus-extension API is available [here](https://gnome.pages.gitlab.gnome.org/nautilus/).  Also, if you are interested in developing a Nautilus extension in Python you should refer to the [nautilus-python](https://gnome.pages.gitlab.gnome.org/nautilus-python/) documentation.

## How to report issues

Report issues to the GNOME [issue tracking system](https://gitlab.gnome.org/GNOME/nautilus/issues).

## Feature requests

Files is a core compoment of the GNOME desktop experience. As such, any changes in behavior or appearance only happen in accordance with the [GNOME design team][design-team].

For major changes, it is best to start a discussion on [discourse] and reach out on the [#gnome-design matrix room][design-room], and only involve the issue tracker once agreement has been reached.

In particular mockups must be approved by the design team to be considered for implementation.

For enhancements that are limited in scope and well-defined, it is acceptable to directly open an issue using the shortcoming template.

[design-team]: https://gitlab.gnome.org/Teams/Design
[discourse]: https://discourse.gnome.org/tag/nautilus
[design-room]: https://matrix.to/#/#gnome-design:gnome.org
