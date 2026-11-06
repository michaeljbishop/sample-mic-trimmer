# Sample Mic Trimmer

**Works with Spitfire Audio libraries.**

A macOS tool that makes a **reduced copy** of a large Spitfire Audio library. You choose which
microphone positions to keep (for example Close and Tree only), and the tool copies just those
sample files to a local disk and patches the library databases to match. The Spitfire plugin then
loads faster and the library takes much less disk space.

It works from a full copy of the library on a big (slow, remote) disk and never changes that source.
Libraries are described by small **profiles** (BBC Symphony Orchestra and Eric Whitacre Choir are
included; anyone can add more, see [Adding a profile](#adding-a-profile)).

> **Unofficial.** This project is independent. It is not made by, affiliated with, sponsored by or
> endorsed by Spitfire Audio Holdings Ltd (the company named in the footer of spitfireaudio.com), the
> BBC, the BBC Symphony Orchestra or Eric Whitacre. You need your own licensed copy of every library
> you use.
>
> "Spitfire Audio" and the product names the tool works with, including "BBC Symphony Orchestra"
> and "Eric Whitacre Choir", and the names "BBC" and "Eric Whitacre" inside them, are the names or
> trademarks of their respective owners (for example the BBC owns "BBC"). They are used here only
> to say which libraries the tool is compatible with. No Spitfire samples, patches, databases,
> artwork or fonts are included in this repository, and the tool never uploads or redistributes any
> of them. It reads and copies only the library files that you own, on your own disks.

## Compatibility

| Library | Status |
|---|---|
| BBC Symphony Orchestra (Core and Professional) | Supported. Built-in profile; mics are chosen per section (Brass, Percussion, Strings, Woodwinds). |
| Eric Whitacre Choir, including Evo Grid | Supported. Built-in profile; nine mic signals offered, chosen per voice. |
| Originals Cinematic Frozen Strings, Originals Intimate Grand Piano, Originals Abe Laboriel Jr Drums | Cannot be trimmed: their sample files are named by articulation or instrument and hold every microphone inside each `.spitfire` container. |
| Other Spitfire libraries | Not checked. A library works if its sample file names contain the microphone code. See [Adding a profile](#adding-a-profile). |

Only the libraries marked "Supported" have been tried on real installs. The Spitfire app and plugins
change over time, so please open an issue if a new version stops working.

## What you need

- macOS 13 or newer. Windows 10/11 is **experimental**: it builds and passes the automated tests on a Windows build server, but nobody has used it with a real Spitfire install yet (see [Windows](#windows-experimental)).
- The Spitfire Audio app and the library's plugin, installed on the computer where you will play.
- A full copy of the library on a remote disk (a NAS, a network share or a big external drive).
- A local disk for the reduced copy.
- To build the tool: Qt 6 (developed with 6.11), CMake and Ninja.

## Build

```bash
export PATH=$HOME/Qt/Tools/Ninja:$PATH
cmake -S . -B build/release -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/macos -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
open "build/release/src/app/Sample Mic Trimmer.app"
```

The command line tool is `build/release/src/cli/mictrim`. Run the tests with
`ctest --test-dir build/release` (they only use generated fake libraries, never your real one).

### Windows (experimental)

Windows support was written without a Windows machine at hand. What has been checked: `.github/workflows/build.yml`
builds the tool on GitHub's Windows runner (Qt 6.10.3, MSVC) and all test suites pass there. What has not: nobody has run
the app on a real Windows computer, with the Spitfire app, a Spitfire plugin or a network share, and the places where
Spitfire keeps its files on Windows are guesses. Expect problems, and please open an issue with the output if you hit
one. Please include the location of your `Spitfire.properties` file.

```bat
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.11.0\msvc2022_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run it from a Visual Studio developer prompt. The built-in profiles are read from a `Profiles` folder next to the
program. The tool needs Qt's `QtSql` module and the `sqldrivers/qsqlite.dll` plugin next to it (run `windeployqt` on the
program; the installed deploy script does this). Where the Spitfire app keeps `Spitfire.properties` on Windows is
unverified: the tool looks in `Music\Spitfire Audio\Settings` and then `Documents\Spitfire Audio\Settings`. Set the
environment variable `SMT_SPITFIRE_SETTINGS_DIR` to the right folder if it is somewhere else. Put the reduced copy on a
local drive; the alias checks for network shares are best effort.

## The workflow

Spitfire signs each library's patch and preset files for the computer and install that created
them. A plain copy of those files made from an unrepaired library does not load: the plugin reports
"failed to load patch config" and **Error #1** or **#3**. Spitfire's *Repair* signs them for your
computer. So the order matters:

1. **Install the full library on a remote disk.** It is fine if this is slow. This stays your
   master copy. Nothing in this tool ever writes to it.
2. **On the computer where you want to use the library, run the Spitfire app and run Repair on
   every section of the library.** Point Repair at the *full* library on the remote disk. Repair
   each section (for BBCSO that means Core, Brass, Strings, Woodwinds and Percussion, whichever you
   own): a section you skip keeps its old patches, and the plugin only shows the interface of
   the sections you repaired.
3. **Start this app.** Choose your remote library as the **Source Installation** and the place for
   your reduced mics as the **Reduced Mic Installation**.
   - Pick the folder you want the reduced library in. If it is not already named exactly like the
     source (for example `Spitfire Audio - BBC Symphony Orchestra`), the app creates a folder with
     that name inside it when you press Apply. The Spitfire app only accepts libraries with the
     original folder name.
   - Tick the microphone positions you want in the table. A click on a section heading, or a mic
     name, switches a whole column or row.
4. **Use the app as normal.** Press **Apply**. The button shows what will happen
   ("Apply (+12 GB / −3 GB)"), the status line beside it says why Apply is disabled when it is,
   and a plan dialog lists the steps. When it finishes, press **Change current installation
   location…** (under the profile picture) to point the Spitfire app and plugin at the reduced
   copy, then reload the plugin in your DAW. Later you can switch mics on and off and Apply again:
   mics you add are copied, mics you remove are deleted from the reduced copy.

Every Apply copies the library's `Patches`, `Presets` and `Config` files again, even when nothing
else changed (about 230 MB for BBCSO). Repair rewrites those files without changing their size, so
the tool cannot tell a repaired file from an old one. That is what keeps them valid.

### Updating a library

If you update or reinstall a library through the Spitfire app, do it **from the computer you play
on, with the Spitfire app pointed at the remote library**. Then run Repair on the sections again and
Apply in this tool. An update done from another computer signs the patches for that computer, and
your plugin will show Error #1 or #3 again.

### Things you may see

- **Error #4 in the plugin** after removing mics: the plugin still lists the removed mics but has
  no samples for them. Turn on the plugin's "disable missing samples" setting.
- **Error #1 or #3**: the patches or presets are not signed for this computer. Run Repair on the
  full library from this computer, then Apply again.
- **Only the Core interface (or only some sections) shows**: only those sections were repaired.
  Repair the others.
- **Apply is grey**: read the red or amber message beside it (not enough space, no mic selected,
  source and destination overlap, and so on).

## Safety

- The source library is never modified: not its files, its folders or its lock. The source and the
  destination may not overlap, also through aliases and a second mount of the same share.
- A reduced `.db` never lists a file that does not exist in the destination.
- Apply can be interrupted and resumed by running it again.
- "Change current installation location…" edits only Spitfire's own
  `~/Music/Spitfire Audio/Settings/Spitfire.properties`, only the entries of the selected profile.
  It keeps the first backup as `Spitfire.properties.smt-backup` and writes the file atomically.
- Scans and plans run in the background, so the window stays responsive on a slow network disk.
  Scan results are cached in the app's data folder (not in your library).

## Command line

```bash
mictrim -s <source> analyze                      # mic positions and sizes (read-only)
mictrim -s <source> -p <dest> plan               # what Apply would do (changes nothing)
mictrim -s <source> -p <dest> apply -y           # do it
mictrim -s <source> -p <dest> use trimmed        # point Spitfire at the reduced copy ("use source" = back)
```

`--profile <id|path.smtprofile>` picks the profile (`bbcso` and `ewc` are short names);
without it the profile is guessed from the source folder name. `mictrim --help` lists everything.

## Adding a profile

A profile tells the tool how one library is laid out: which sample files are which microphone
position, which mics to offer, and which entries of Spitfire's settings file belong to it. It is a
folder of small JSON files, so you can add a library without writing C++.

A library qualifies only if its sample files have the mic position in their file name (as BBC
Symphony Orchestra and Eric Whitacre Choir do). Libraries whose files are named by articulation or
instrument, such as Originals Cinematic Frozen Strings and Intimate Grand Piano, keep every mic
inside each `.spitfire` container and cannot be trimmed.

**[docs/ADDING-A-PROFILE.md](docs/ADDING-A-PROFILE.md)** is the complete guide, written so that a
person or their Claude can follow it from an installed library to a pull request: how to
investigate the library, every profile file and field, a worked example, how to check it, how to
register it and what a pull request must contain.

For how the code is organised, read `CLAUDE.md`; the verified facts about Spitfire's settings,
plugin errors and library formats are in `docs/SPITFIRE-SYSTEM.md`, and the manual checks on a real
library are in `docs/ACCEPTANCE.md`.

## Background and thanks

Removing microphone positions by hand is something Spitfire itself describes, and this tool
automates it:

- [How to remove microphones from BBC Symphony Orchestra Professional](https://support.spitfireaudio.com/en/articles/11816020-how-to-remove-microphones-from-bbc-symphony-orchestra-professional)
  (Spitfire Audio support).
- [How to remove microphones from Eric Whitacre Choir](https://support.spitfireaudio.com/en/articles/11815877)
  (Spitfire Audio support).
- [Fix for slow BBC SO Pro load times after removing mic positions](https://vi-control.net/community/threads/fix-for-slow-bbc-so-pro-load-times-after-removing-mic-positions.140297/)
  (VI-Control forum). Deleting the sample files is not enough: the plugin still looks for them in the
  library's databases, and loading stays slow. Removing the rows of the deleted files from each
  database's `Containers` table fixes that, and this tool does it for you. Many thanks to
  **@pawel**, who found and shared the fix.

## Credits and license

Co-written by Michael Bishop and Claude (Anthropic's AI model, used through Claude Code).

Released under the [MIT License](LICENSE). The license covers this tool's code and documentation
only; it gives no rights in Spitfire Audio's libraries, plugins or software.

This tool changes files on your disks. Keep your master library backed up (the tool never writes to
it, but a backup costs nothing), and use it at your own risk.
