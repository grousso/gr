GNOME Recipes
=============

<p align="center">
  <img src="https://github.com/matthiasclasen/gr/blob/master/data/icons/512x512/org.gnome.Recipes.png?raw=true" alt="Recipes icon"/>
</p>

This app is about cooking and recipes.

The main objects of interest are

- recipes
- ingredients
- cuisines
- chefs

The design can be found here: https://wiki.gnome.org/Design/Apps/Recipes

Building
--------

To build GNOME Recipes from git, use the following steps:

```
git clone https://github.com/matthiasclasen/gr.git
cd gr
./autogen.sh --prefix=<your preferred location>
make
make install
```
I list `make install` as the last step here, but Recipes works fine uninstalled as well.

Testing
-------

If you don't feel like building from source yourself, you can use Flatpak, like this:

```
flatpak install --from https://alexlarsson.github.io/test-releases/gnome-recipes.flatpakref
```

or using a Flatpak bundle:

```
flatpak install --bundle https://github.com/matthiasclasen/gr/releases/download/0.2.0/org.gnome.Recipes.x86_64.flatpak
```
After installing the Flatpak with either method, the applications will show up in the GNOME shell overview, but you can also launch it from the commandline:

```
flatpak run org.gnome.Recipes
```
Other platforms
---------------

GNOME recipes has been successfully built on OS X. The following steps should get you there:

- Install [Homebrew](http://brew.sh/)
- Install autotools:
```
brew install autoconf
brew install automake
brew install libtool
brew install gettext
brew install pkg-config
```
- Install GTK+ and other dependencies:
```
brew install gtk+3
brew install adwaita-icon-theme
brew install gspell
```
- Clone from git:
```
git clone git://git.gnome.org/recipes.git
```
- Build from git as usual, disabling some problematic dependencies:
```
cd recipes
./autogen --disable-autoar
make
```
To build a released version of recipes on OS X, just

```
brew install recipes
```
