# Building the engine on any Linux
Engine code demands at least partial C++11 support (uses std::shared_ptr, std::unique_ptr, etc).
Known minimal versions of compilers that should work with AGS:

-   GCC 4.4

The following packages are required to build AGS. The versions in
parentheses are known to work, but other versions will also
probably work.

-   libsdl2 (2.0.12 or higher)
-   libsdl_sound for sdl2 (revision ebcf0fe72544 or higher)
-   libogg (1.2.2-1.3.0)
-   libtheora (1.1.1-1.2.0)
-   libvorbis (1.3.2)

There are two general ways to proceed: either use [CMake scripts](../CMAKE.md) or install and build everything yourself using Makefiles provided in the Engine's directory.

Fedora package installation
---------------------------
    yum -y install git sdl2-devel libogg-devel libtheora-devel libvorbis-devel

Debian/Ubuntu package installation
----------------------------------
    sudo apt-get install git debhelper build-essential pkg-config libsdl2-dev libogg-dev libtheora-dev libvorbis-dev

Other Linux systems use their respective package managers.

SDL_Sound library installation
----------------------------------
At the time of writing SDL_Sound 2.* has just been released, but almost no linux distro provides it.
Until that is resolved, we recommend to download particular revision archive using following url:

    https://github.com/icculus/SDL_sound/archive/32ee23e2018378225eba2c2bca8c2264bdbd369f.tar.gz

then build and install using CMake (see instructions in the SDL_Sound's docs).

Download and build
------------------
Download the source with git and change into the **ags** directory:

    git clone git://github.com/adventuregamestudio/ags.git
    cd ags

Compile the engine:

    make --directory=Engine

The **ags** executable can now be found in the **Engine** folder and
can be installed with

    sudo make --directory=Engine install

# Building a Debian/Ubuntu package of AGS
Building a package is the preferred way to install software on
Debian/Ubuntu. This is how it's done.

Download the sources with git and change into the **ags** directory:

    git clone git://github.com/adventuregamestudio/ags.git
    cd ags

Build the package and install it with gdebi:

    fakeroot debian/rules binary
    sudo gdebi ../ags_3~git-1_*.deb

# Using the engine
To start an AGS game, just run ags with the game directory or the game
file as parameter, e.g.

    ags /path/to/game/

or

    ags game.exe

To view available command line options, use

    ags --help

The configuration file **acsetup.cfg** in the game directory will be used
if present. For more information on configuration and command line arguments
see [OPTIONS.md](../OPTIONS.md).

## MIDI music support

FIXME: following section is obsolete, must remove or replace with the new instructions related to SDL2 backend.

For midi music playback, you have to download GUS patches. We recommend
"Richard Sanders's GUS patches" from this address:

http://alleg.sourceforge.net/digmid.html

A direct link is here:

http://www.eglebbk.dds.nl/program/download/digmid.dat

This 'digmid.dat' is, in fact, a **bzip2** archive, containing actual data file,
which should be about 25 MB large. Extract that file and rename it to **patches.dat**.
You can now place it:

-   in the directory pointed to by the ALLEGRO environment variable; or
-   if $ALLEGRO is not defined, in $HOME; or
-   in the same directory of the AGS executable; or
-   in the game's directory.

# Debugging
When using the Debian/Ubuntu package, the package ags-dbg_*.deb containing debugging
symbols is created alongside ags_*.deb. The build date and the name of the
last git commit at the time of building are stored in the package description,
which can be viewed with

    apt-cache show ags

This information should be included in bug reports.

# Building AGS for a game release
If you want to build AGS for inclusion in a game release, you want an
engine that runs on most 32 and 64 bit Linux systems regardless of the library
versions that are installed on that system. You can get such a built by using
the script **debian/make_ags+libraries.sh**. The script itself can be used
on Debian or Ubuntu. See the comments in the script for instructions.

# Workaround: 32 bit AGS on 64 bit Debian/Ubuntu
In the past AGS worked only on 32 bit architectures, so it was necessary to compile
a 32 bit version on 64 bit systems. This is not necessary anymore, but these
instructions are kept for reference and may be helpful for debugging etc.

The development versions of Debian and Ubuntu support parallel
installation of both 32 and 64 bit versions of all required libraries
(multiarch), so you can build a 32 bit AGS to use on your 64 bit system.
This part works only on Debian sid and wheezy and Ubuntu quantal.

Download the sources with git and change into the **ags** directory:

    git clone git://github.com/adventuregamestudio/ags.git
    cd ags

## Matching working directory and orig tarball
To build the package, it is required that there is an "orig tarball"
that has the same content as the working directory. This tarball is generated
from the git content with

    debian/rules get-orig-source

The working directory must have the same content as git, i.e. be "clean".
To ensure this, check if the working directory is clean with

    git status

If there are changes, run 

    debian/rules clean 

and/or

    git reset --hard HEAD

If there are still untracked files, delete them manually.

Run `debian/rules get-orig-source` every time the sources change. If
you want to change the sources yourself, you have to commit the
changes to git, then run `debian/rules get-orig-source`, then
build the package.


## Building the package

Enable multiarch:

    sudo dpkg --add-architecture i386
    sudo apt-get update

Install and prepare pbuilder (use the same distribution you are using,
i.e. `sid`, `wheezy` or `quantal`):

    sudo apt-get install pbuilder
    sudo pbuilder create --distribution sid --architecture i386

This creates an i386 chroot which will be used to build the i386 package
on an amd64 system. pbuilder automatically manages the build dependencies.
The pbuilder base can later be updated with

    sudo pbuilder update

Build the package with pbuilder and install it and its dependencies with gdebi:

    cd ags
    pdebuild
    sudo gdebi /var/cache/pbuilder/result/ags_3~git-1_i386.deb
