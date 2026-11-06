# Adding a profile

A **profile** teaches the app how one Spitfire library is laid out: which sample files are which
microphone position, which mics to offer, and which entries of Spitfire's own settings file belong
to the library. It is data, not code: a folder of small JSON files. You can add a library without
writing C++.

This guide is written so a person, or an AI assistant working for one, can follow it from a real
installed library to a pull request. Do not guess any value. Every name, pattern and path in a
profile must come from the library you are looking at.

Contents: [Does the library qualify?](#1-does-the-library-qualify) ·
[Investigate](#2-investigate-the-library-read-only) · [Create the bundle](#3-create-the-bundle) ·
[File reference](#4-file-reference) · [A worked example](#5-a-worked-example) ·
[Check it](#6-check-it) · [Register and submit](#7-register-and-submit) ·
[Pitfalls](#8-pitfalls)

## 1. Does the library qualify?

The tool removes microphone positions by deleting whole sample files, so a library qualifies only if
**each sample file belongs to exactly one mic position and the file name says which one**. Check
this first:

- `Samples/` holds many `.spitfire` files whose names end in a mic code, such as
  `…_CL01_01.spitfire`, `…_TR01.spitfire`. **Qualifies.** (BBC Symphony Orchestra, Eric Whitacre
  Choir.)
- `Samples/` holds a few big `.spitfire` files named by articulation or instrument, such as
  `…__LONG_PEDAL_DOWN_01.spitfire`, with no mic code. **Does not qualify.** All the mics are inside
  each container and the app cannot split them. (Originals Cinematic Frozen Strings, Intimate Grand
  Piano.)

If the library does not qualify, stop: a profile cannot make it work.

## 2. Investigate the library (read-only)

Work on a full, installed copy. Nothing below changes a file. Keep notes of what you find; the pull
request description needs them.

**Folder layout.** List the library folder. Expect `Samples/` plus some of `Config/`, `Patches/`,
`Presets/`, `NKS/`. Note the exact folder name (for example `Spitfire Audio - Eric Whitacre Choir`):
the Spitfire app only accepts a library whose folder has its original name.

**Sample file names.** List `Samples/` (and its sub-folders, if any) and find the pattern:

```bash
cd "<library>/Samples"
ls *.spitfire | head -40
# count how often each trailing code occurs (adjust the pattern to what you see):
ls *.spitfire | sed -E 's/.*_([A-Za-z]{2}[0-9]{2})(_[0-9]+)?(_V[0-9]+)?\.spitfire$/\1/' | sort | uniq -c
```

Find three things: the **mic code** (such as `CL01`), whether a **group** exists (a voice such as
`SOPRANOS`, or a section folder such as `_Strings`), and any suffixes after the code (`_01`, `_V3`)
that the pattern has to allow. Note every distinct mic code and how many files it has.

**The databases.** Each section has a SQLite database (`.db`) with two tables,
`Containers(ID, File)` and `Files(ID, Container)`. `Containers.File` holds sample file names. Open it
read-only:

```bash
sqlite3 -readonly "<library>/Samples/<NAME>.db" ".schema" \
  "select count(*) from Containers;" "select File from Containers limit 10;"
```

The names must match files on disk. The tool rewrites a database by removing the `Containers` rows
of the deleted files.

**Spitfire's settings.** Look in `~/Music/Spitfire Audio/Settings/Spitfire.properties` (JSON, one
entry per product). Find every entry that belongs to the library: a product can have several (the
Whitacre library has three) and several can share one library folder. For each, write down the
paths of its `config`, `patches`, `presets` and `samples` keys *relative to the library folder*. A
sub-path of `""` is the library folder itself. This becomes `spitfire.json`. Make a copy of the file
before experimenting; the app keeps its own backup too.

**What each mic is.** Spitfire's support articles and product pages name the mic positions. Use
their names. Note which codes Spitfire documents and which it does not.

**Which mics may be removed.** Some codes are required by the library (for example BBCSO Mix 1
`MI01`, or Whitacre's `CL02`–`CL05` and `ST04`, which Spitfire says are needed for large parts of
the library). A mic the profile does not list is "other": it is always copied and never removed. A
mic marked `locked` is listed but always on. Prefer leaving a code unlisted when you are unsure.

## 3. Create the bundle

Copy `profiles/EricWhitacre.smtprofile` (by file name) or `profiles/BBCSO.smtprofile` (by folder)
as a template. A profile is a macOS package folder named `<Name>.smtprofile`:

```
<Name>.smtprofile/Contents/
    Info.plist
    Resources/
        library.json    mics.json    groups.json    spitfire.json
        images.json (optional)    notes.md (optional)
```

Only `Info.plist`, `library.json`, `mics.json` and `groups.json` are required. `spitfire.json` is
needed for "Change current installation location…", which is part of the normal workflow, so
include it.

## 4. File reference

All paths below are relative to `Contents/Resources/`. The app reports the first thing that is
wrong as "invalid: <why>" and refuses the profile.

### `Info.plist`

An XML property list of string keys:

| Key | Meaning |
|---|---|
| `CFBundleIdentifier` | The profile's unique id, such as `name.example.smtprofile.<product>`. Saved in each reduced library's `project.smt`; do not change it later. |
| `CFBundleName` | The name shown in the pop-up and the window title. |
| `CFBundleShortVersionString`, `CFBundleVersion` | Your profile's version. |
| `SMTProfileFormat` | `1` |
| `SMTProfileLibrary`, `SMTProfileMics`, `SMTProfileGroups` | The file names of the required parts. |
| `SMTProfileSpitfire`, `SMTProfileImages`, `SMTProfileNotes` | The file names of the optional parts. |

### `library.json`: recognise the library and its sample files

```json
{
  "folderPattern": "^Spitfire Audio - Eric Whitacre Choir",
  "samplesDir": "Samples",
  "sampleFile": "^ERICW_(?<group>[A-Z]+)_.*_(?<mic>[A-Za-z]{2}\\d{2})(?:_\\d+)?(?:_V\\d+)?\\.spitfire$"
}
```

- `folderPattern`: a regular expression, matched against the library **folder name**. The app uses it
  to suggest the profile when you choose a source folder, and it must not match another product's
  folder.
- `samplesDir`: the folder below the library that holds the samples (`Samples`).
- `sampleFile`: a regular expression matched against a sample's **file name** (not its path). It
  needs a named group `(?<mic>…)` that captures the mic code, and a named group `(?<group>…)` if
  the profile matches groups by file name.
- All expressions are case-insensitive. In JSON write each backslash twice (`\\d`).
- Only `.spitfire` files below `samplesDir` can be mics; the file name is matched, so a loose
  pattern cannot make the tool delete anything else.

### `mics.json`: the mics offered

```json
{
  "mics": [ { "token": "cl01", "name": "Close", "family": "close", "locked": false, "note": "…" } ],
  "families": [ { "key": "close", "name": "Close", "tab": "Core" } ],
  "quickSets": [ { "name": "Close and tree", "families": ["close", "tree"] } ]
}
```

- `mics`: one entry per mic code the app offers. `token` is the code in lower case. `name` is what
  the user sees (use Spitfire's name). `family` names a family. `locked: true` means always on.
  `note` is optional.
- `families`: the **rows of the table**; every mic belongs to exactly one family. Several mics can
  share a family if Spitfire treats them as one signal; one family per mic is the simplest. `tab`
  is a heading that groups related rows (Core, Auxiliary, Stereo).
- `quickSets` (optional): buttons that tick a set of families. The built-in profiles have none.
- Tokens and family keys must be unique, and every mic must name a family that exists.

### `groups.json`: the columns of the table

```json
{ "groups": [ { "key": "sopranos", "name": "Sopranos", "match": "SOPRANOS" } ] }
```

A mic position is one (mic, group) cell, so every profile needs at least one group. Choose one of
two ways to match a file to its group, and use it for all groups:

- **By file name** (default): `match` equals the text your `(?<group>…)` captured, ignoring case.
  Whitacre does this with voices (`SOPRANOS`, `ALTOS`…).
- **By folder**: add `"by": "dir"` to every group; `match` must equal the **name of the folder that
  directly holds the sample file** exactly. BBCSO does this with its section folders (`_Brass`…).

A file is a mic only if both its mic code and its group are listed. Anything else is "other".

### `spitfire.json`: the entries of `Spitfire.properties`

```json
{
  "entries": [
    {
      "name": "Eric Whitacre Choir",
      "keys": {
        "patches": ["Eric Whitacre Choir", "Patches", "Patches/Eric Whitacre Choir"],
        "presets": ["Eric Whitacre Choir", "Presets", "Presets/Eric Whitacre Choir"],
        "samples": ["Samples"]
      }
    }
  ]
}
```

- Each entry is one product entry of `Spitfire.properties`; `name` must equal the entry's key there.
- `keys` maps `config`, `patches`, `presets` and `samples` to the sub-paths the Spitfire app
  itself writes, relative to the library folder (`""` = the folder itself). No `..`, no leading `/`,
  no backslashes, no empty segments.
- Copy the lists exactly as a working install has them. Do not shorten or reorder them: the lists
  that work are the ones Spitfire's own app writes.
- Limits: at most 64 entries, 16 keys per entry and 16 sub-paths per key.
- "Change current installation location…" refuses to run if an entry is missing from the user's
  file, and it only edits the entries and keys listed here.

### `images.json` (optional): the library's picture

```json
{
  "main": {
    "sources": [ "cache:<file name>", "url:https://www.spitfireaudio.com/…?width=500" ],
    "credit": "https://www.spitfireaudio.com/<product page>"
  },
  "logo": { "sources": [ "library:NKS/PAResources/image/Spitfire Audio/*/MST_Logo.png" ] }
}
```

The first source that resolves wins. Source kinds: `cache:<plain file name>` (the Spitfire app's own
image cache), `library:<glob>` (a file inside the user's installed library), `url:` (downloaded at
run time; **only `https://www.spitfireaudio.com/…` is allowed**) and `file:<path>` (a file inside
the profile). `credit` is a product-page URL on the same host, shown as the picture's tooltip.
**Do not put Spitfire's pictures into the repository.** Leave `images.json` out if unsure; the app
works without a picture.

### `notes.md` (optional)

Plain text shown as the profile's tooltip. Put in what a user must know, for example the
"disable missing samples" setting when removed mics make the plugin show Error 4.

## 5. A worked example

The Eric Whitacre Choir sample `ERICW_ALTOS_LEG-INTAAH_RR1_CL01_01.spitfire`, in `Samples/`:

| Part of the name | Meaning | Where it goes in the profile |
|---|---|---|
| `ERICW_` | product prefix | start of `sampleFile` |
| `ALTOS` | the voice (group) | `(?<group>[A-Z]+)` and `groups.json` `match: "ALTOS"` |
| `LEG-INTAAH_RR1` | articulation, vowel, round robin | `.*` in the pattern |
| `CL01` | the mic | `(?<mic>[A-Za-z]{2}\d{2})` and `mics.json` token `cl01` |
| `_01` | variant number | `(?:_\d+)?` |
| `_V3` (some files) | revision | `(?:_V\d+)?` |

`analyze` against the library should then list nine mics in five groups, and name `CL02`–`CL05` and
`ST04` as unknown mic codes that are kept.

## 6. Check it

1. **Build and run the tests:** `ctest --test-dir build/dev`. They use generated fake libraries only.
2. **Validate the profile:** run
   `build/dev/src/cli/mictrim --profile profiles/<Name>.smtprofile -s "<library>" analyze`.
   A broken field is named in the error. A good run prints the profile name first, then every mic
   and group with file counts and sizes. Check that:
   - every mic and group you expect is listed;
   - there is no "missing from disk" line (the database names match the files);
   - codes you left out appear only under the unknown/"match no mic" lines, and are the ones you
     meant to leave out;
   - the totals match what you found by hand in step 2.
3. **Plan without changing anything:** `mictrim --profile … -s "<library>" -p "<new folder>" plan`.
   Check that switched-off mics are not copied and that the files you meant to keep always are.
4. **Try it for real once**, with the workflow in the README: on a full library, run Spitfire's
   Repair for every section, then Apply a small selection into a new local folder (named exactly
   like the library), point the plugin at it with "Change current installation location…" and
   load a patch. Record what you saw. If the plugin shows Error #4 (samples missing) for removed
   mics, say so in `notes.md`. If patches fail with Error #1 or #3, repair the source first and
   Apply again.

For one-off experiments, put the profile folder in the app's profiles folder (File ▸ Reveal
Profiles Folder). A valid user profile with a built-in's id replaces that built-in.

## 7. Register and submit

1. Put the bundle in `profiles/<Name>.smtprofile/`.
2. Add every file of it to the `qt_add_resources(corelib "profiles" …)` list in
   `src/corelib/CMakeLists.txt` (the app bundle copies the whole `profiles/` folder by itself).
3. In `tests/tst_profileregistry.cpp`, test `builtInsAreValid`, raise the expected number of built-in
   profiles. You may add a small test for your profile's parsing (see `tst_profile.cpp`).
4. Build and run `ctest`. Everything must pass with no stray output.
5. Open a pull request. In the description give:
   - the product and version you checked, and where you got the mic names;
   - your `analyze` output (counts only: no file lists);
   - what happened in the plugin, including any Error #4;
   - which mic codes you left unlisted or `locked`, and why;
   - that you did not copy any Spitfire files or pictures into the repository.

Rules for a profile in this repository:

- It contains only names, codes, patterns and relative paths that describe the library. No
  Spitfire samples, patches, databases, text from their manuals, artwork or fonts.
- Pictures come only from the user's own library, the Spitfire app's cache, or
  `https://www.spitfireaudio.com`.
- No personal paths, user names or machine details.
- Product and artist names are used only to say what the profile is for; see the notice in the README.

## 8. Pitfalls

- **Copied patches fail** with Error #1 or #3 unless the source library was repaired on the
  computer that plays it. This is how Spitfire signs the files, not a profile fault. See
  `docs/SPITFIRE-SYSTEM.md` section 6b.
- **A mic needs a group.** If `analyze` shows no mic positions at all, either `sampleFile` does not
  match (test the expression on a real name), or the group `match` does not equal the captured
  text or folder name.
- **Group matching modes cannot be mixed.** Either every group has `"by": "dir"` or none does.
- **Do not list a code just because it exists.** Unlisted codes are safe: they are copied and never
  removed. A listed code can be removed, and removing a required one breaks the library.
- **`CFBundleIdentifier` is permanent.** Reduced libraries remember it; renaming it later makes the
  app refuse their `project.smt`.
- **Escape backslashes in JSON** (`\\d`, not `\d`).
- **The Spitfire app only accepts the original folder name**, so the profile's `folderPattern`
  should match it and the reduced copy must keep it.
- **Several entries, one folder:** a product can have several `Spitfire.properties` entries that
  share one library folder (Whitacre). List all of them, or "Change current installation
  location…" will leave the others pointing at the old folder.
