Naruto: Clash of Ninja - launcher (macOS)
========================================

Drag Naruto Launcher.app wherever you like (Applications is fine) and open it.
The four games appear as covers; click one and choose your own USA copy of the game.
The launcher writes the game folders, extracts the disc, builds the game and starts
it. Nothing from the game is included here.

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

    No game project: the launcher writes the four into its own folder, and dtk - which
    is inside the app, with CMake, Ninja, SDL2 and nodtool - makes each game's symbol map
    from your own disc. Nothing is fetched from anywhere.

First start
    macOS will not run an app it has not seen before: right-click the app and
    choose Open, once. The bundle is signed ad hoc, not notarised.

    Choose a cover, then your disc image. The launcher writes the game folders beside
    its own data, and the extracted disc, the build and your saves stay there. The
    game itself is then at:

        ~/Library/Application Support/NarutoLauncher/project/naruto/build/naruto

Prefer a terminal?
    The launcher only runs these steps for you, and the tools it uses are inside
    the app. From the folder the launcher set up:

        NARUTO_TOOLS="/Applications/Naruto Launcher.app/Contents/Resources/tools" \
            tools/setup.sh --run "/path/to/your disc image"

    That extracts the disc there and builds with the CMake, Ninja, SDL2 and
    nodtool the app carries.

The build needs about 4 GB of free space and a few minutes; later starts go
straight to Play.
