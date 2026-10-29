Naruto: Clash of Ninja - launcher (macOS)
========================================

Drag Naruto Launcher.app wherever you like (Applications is fine) and open it.
It asks for your own USA copy of the game, extracts it, builds the game and
starts it. Nothing from the game is included here.

What you need
    A C++ compiler. macOS builds are made from your own disc, and Apple's compiler
    - and the SDK it links against - may not be redistributed with the app, so it
    has to come from your machine.

    The Command Line Tools provide it. They are free, need no Apple account, and
    install with one click (about 2 GB):

        xcode-select --install

    The launcher can start that installer for you when the compiler is missing,
    and waits for it before carrying on. An Xcode installation (or a beta) brings
    the same compiler and SDK and counts too.

    Everything else - the game's sources, CMake, Ninja, SDL2 and nodtool - is
    inside the app, so nothing is fetched from anywhere else.

First start
    macOS will not run an app it has not seen before: right-click the app and
    choose Open, once. The bundle is signed ad hoc, not notarised.

    The launcher keeps its copy of the sources, the extracted disc, the build and
    your saves in ~/Library/Application Support/NarutoLauncher. The game itself is
    then at:

        ~/Library/Application Support/NarutoLauncher/project/naruto/build/naruto

Prefer a terminal?
    The launcher only runs these steps for you, and everything it uses is inside
    the app:

        "/Applications/Naruto Launcher.app/Contents/Resources/project/tools/setup.sh" \
            --run "/path/to/your disc image"

    That copies the sources to ~/Naruto (the app itself is read-only), extracts
    the disc there, builds with the CMake, Ninja, SDL2 and nodtool the app
    carries, and starts the game. Afterwards it is just:

        ~/Naruto/naruto/build/naruto

The build needs about 4 GB of free space and a few minutes; later starts go
straight to Play.

