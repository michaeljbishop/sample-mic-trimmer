# How Spitfire's system works (as observed)

Spec sheet for everything this project has learned about Spitfire Audio's app, plugin and library
format. Nothing here comes from Spitfire documentation except where a support article is named. It
comes from reading the real installed library (read-only), the app binary's strings, the
app/plugin logs and experiments on a trimmed copy. Each fact is tagged:

- **[verified]** seen directly on disk or in a log, or confirmed by an experiment;
- **[binary]** read from strings in `/Applications/Spitfire Audio.app`, behaviour not observed;
- **[guess]** inferred, not tested.

Tested product: BBC Symphony Orchestra (BBCSO) Pro/Core/Discover, library v1.7.0, app/plugin
1.14.0, macOS. The goal is to reuse the tool for other Spitfire products that use the same system
(see "Other products").

## 1. Pieces

| Piece | Where | Role |
|---|---|---|
| Spitfire Audio app (library manager) | `/Applications/Spitfire Audio.app` | Installs, repairs/relocates libraries, writes the settings below. |
| Plugin (per product) | `/Library/Audio/Plug-Ins/{Components,VST3}/BBC Symphony Orchestra.*` | Loaded by the DAW; reads the settings and the library. |
| Settings folder | `~/Music/Spitfire Audio/Settings/` | Shared by app and plugins (section 2). |
| Library folder | anywhere, e.g. `/Volumes/External/Software Instrument Libraries/Spitfire/Spitfire Audio - BBC Symphony Orchestra` | Samples, patches, presets (section 3). |
| Logs | `~/Library/Logs/com.spitfireaudio/spitfireaudio<date>_<time>.log` (plugin, one per instance) and `Settings/lm.log` (app) | Section 7. |

## 2. Settings folder (`~/Music/Spitfire Audio/Settings/`)

- `Spitfire.properties` **[verified]**: JSON object, product name -> object with up to four keys,
  each an array of folder paths: `config`, `patches`, `presets`, `samples`. This is the only place
  the plugin finds a library: change a path here and the plugin uses that folder. The app watches
  the file ("Settings file changed -- updating library warnings" in `lm.log`), so a change is seen
  without restarting the app. The plugin reads it when it loads. Format: 4-space indented JSON,
  keys sorted.
- `<Product>.settings` **[verified]**: XML `<PROPERTIES>` of plugin defaults per product, e.g.
  `defaultPreloadSize`, `defaultBufferSize`, `defaultUnloadAt0`, `defaultIgnoreMissingSamples`
  (0/1), `defaultLazyTechniquePreload`. Not library paths.
- `lm.conf` **[verified]**: JSON, `auth`, `content_path` (default install location `~/Spitfire`),
  `plugin_path`. No per-library paths for BBCSO. The app rewrites it on Repair.
- `refresh_token`, `shared_auth_token`: login tokens. Never read or copy them.
- `lm.log` **[verified]**: app log; only says "refresh libraries", "library data received" and
  settings changes. It does **not** record why Repair/Locate rejects a folder.

## 3. Library folder layout (BBCSO) **[verified]**

```
Spitfire Audio - BBC Symphony Orchestra/        <- name must match, see section 4
  Config/v1.7.0/Discover Piano/BBCSO.plugin.zconfig   (31 KB, encrypted; the only *.plugin.zconfig)
  Documents/   *.pdf manuals, Release Notes.html
  NKS/         *.nksf (Native Instruments presets)
  Patches/v1.2.0 (empty), v1.5.0/..., v1.7.0/Discover Piano/   *.zmulti, *.patches.zconfig
  Presets/v1.2.0 (empty), v1.5.0/..., v1.7.0/Discover Piano/   *.*preset, *.presets.zconfig
  Samples/_Brass _Core _Discover _Percussion _PianoDiscover _Strings _Woodwinds
        each: one or more *.db (SQLite) + thousands of *.spitfire sample containers
```

- `NKS/PAResources/image/Spitfire Audio/<library folder name>/` holds `MST_Artwork.png`, `MST_Logo.png` (240x196), `MST_Plugin.png`, `VB_Artwork.png`, `VB_Logo.png`, `OSO_Logo.png` **[verified for BBCSO, Whitacre and the Originals]**; the folder name there is the library's (e.g. `Spitfire Audio - Originals - Intimate Grand Piano`, not the on-disk folder name). Whitacre has a second set for `... Evo Grid`.
- Patches, presets and zconfig files are encrypted; the tool never reads or changes them. The
  plugin's list of mic positions per patch comes from them, so removed mics stay listed.
- Section databases (Samples/_X/*.db): Brass `BBCSO_BRASS_V020.db`, Core `BBSCO_CORE.db`,
  Discover `BBCSO_DISCOVER.db`, Percussion `BBBCSO_PERC_v020.db`, PianoDiscover `BBCSO.db`,
  Strings `BBCSO_STRINGS_v022.db`, Woodwinds `BBCSO_WOODS_V020.db` **and** `BBCSO_WOODS_V115.db`
  (the names contain typos; a folder can hold several). Never assume a name; scan `Samples/**/*.db`.
- The same sample name can exist in several section folders (e.g. `BBCSO_MV1_IR1.spitfire`, an
  impulse response, in every one). The plugin logs "Multiple Files found called X - Loaded most
  recent"; harmless.
- Files that are not mic files exist (impulse responses), so "every .spitfire has a mic token" is
  false.
- Pro, Core and Discover are one install: the app cannot remove Discover from it, and the
  Discover patches/config sit in the same folders (Config's only file is under `Discover Piano`).
  14 loose `..._Short_Hits_Discover.zmulti` Pro/Core patches are not Discover content. The tool
  copies all of it (excluding Discover saved only about 220 MB and was never shown to be needed).

### Sample file names

`BBCSO[_C]_<INSTRUMENT>_<ARTICULATION>_RR<n|a-b>_<MIC>[_<n>].spitfire`, e.g.
`BBCSO_VLN1_LONGCS_VIB_RR1_MI01_01.spitfire`. `<MIC>` is two letters plus two digits:
AF01 AM01 AR01 BA01 CL01 CW01 LE01 MD01 MI01 (Mix 1) MI02 (Mix 2) MO01 OU01 SB01 SF01 SI01 SP01
SS01 ST01 SW01 TR01. The mic exists **only** in the file name (no database column, no metadata).
Some files carry `_AUNTIE_` after `BBCSO_` (still mic-suffixed).

### Database schema **[verified]**

```
Containers(ID INTEGER PK AUTOINCREMENT, File TEXT UNIQUE)      -- one row per .spitfire file name
Files(ID INTEGER PK, Container INTEGER -> Containers.ID)       -- hundreds of thousands of rows
```

The patches reference `Files.ID`s. The plugin resolves them with `Files` joined to `Containers`.
Deleting a `Containers` row (and leaving `Files` rows orphaned) is enough to make the plugin stop
looking for that file (the vi-control "fix for slow BBC SO Pro load times" approach). The
tool does exactly this and never touches `Files`. Orphaned `Files` rows are normal in a trimmed
copy (e.g. 408,147 of 430,562 in the Strings DB with Mix 1 only). Whether deleting them as well
would speed loading is untested. Open DBs read-only/immutable when inspecting a library.

## 4. What the app accepts as a library (Repair / Locate) **[verified by experiment]**

- The dialog says: "Please select the location of your "<name>" folder." and Repair says: "You
  have not chosen a valid path to repair this library."
- **The chosen folder's name must equal the original product folder name**, e.g.
  `Spitfire Audio - BBC Symphony Orchestra`. `TEST - Spitfire Audio - BBC Symphony Orchestra` was
  rejected as "not a valid library". The same contents moved to
  `.../Spitfire-Trimmed/Spitfire Audio - BBC Symphony Orchestra` were accepted. Nothing else
  needed to change (the `project.smt` file in the folder was irrelevant; both with and without it
  failed under the wrong name).
- Trimmed contents are accepted: the app does not check that every sample exists.
- Repair writes only `Spitfire.properties` (all four paths of the product) and `lm.conf`. It
  modifies **no** library file, so copied `.spitfire` files need no re-authorisation: they are
  byte-identical to the originals and work as-is. Repair also shows "wait for the small download".
- Additional checks seen in the binary **[binary]**: each configured path must exist, be a
  directory and "contain any expected filetypes", else a warning ("does not exist", "is not a
  directory", "does not contain any expected filetypes", "No paths found"). The file-type filters:
  config `*.plugin.zconfig`; patches `*.*multi;*.*json;*.patches.zconfig`; presets
  `*.*preset;*.presets.zconfig`; samples `*.spitfire;*.db`. A copy must keep at least one of each,
  recursively. The app also mentions a `Products.manifest` (not found on disk; unknown role).

## 5. Plugin behaviour with a trimmed library **[verified]**

- Loads and plays Mix 1 (default mic). Violins 1 loads all 20 articulations;
  "WaveFS preload: 1746 of 1746 samples".
- For every removed mic it logs "Error querying database - no results found - DBs: ..." (ids not
  resolvable). After the last patch it raises **ERROR #4**, shown as a red exclamation mark at the
  upper left of the plugin; its dialog is Spitfire's support article "Error 4: something went wrong"
  ("the instrument cannot locate the samples it needs"). Sound still works.
- All mic positions stay selectable in the plugin, but those that were removed are silent.
- `defaultIgnoreMissingSamples` in `<Product>.settings` is probably the switch to silence the
  warning; set to 1 for Eric Whitacre Choir in the settings of the machine it was checked on. **[guess: effect on Error 4
  untested]**

## 5b. Spitfire's own guidance on removing mics (Eric Whitacre Choir) **[support article]**

Source: support.spitfireaudio.com article 11815877 "How to remove microphones from Eric Whitacre
Choir". It endorses what this tool does, by hand:

- Delete the sample files whose name ends in the mic code, in the library's `Samples` folder.
  Keep a backup: restoring without one means "reset and redownload the entire library".
- Codes (14 signals; the article's chart lists nine, and the tool offers exactly those nine): CL01 Close, TR01 Tree, AM01 Ambient,
  OU01 Outriggers, MI01 Mid, GA01 Gallery, ST01 Big, ST02 Medium, ST03 Small. Spitfire's product page calls
  ST01-ST03 (Big, Medium, Small) the "Stereo mix" options without describing them, and lists the mics as
  "Close, Tree, Ambient, Outrigger, Mid, Gallery". The tool groups Close, Tree, Ambient, Outriggers as Core, Mid and Gallery as Auxiliary and Big, Medium, Small as Stereo; no mic is locked. The other five
  (CL02-CL05, ST04) are not in the chart, so the tool does not offer them and always copies them (they are unlisted tokens: reported by `analyze` as unknown codes, never removed); the article says signals not in the chart are "required
  for large sections of the library so it is not recommended to remove these".
- The plugin then shows **Error 4** (top left); to clear it, enable the "disable missing samples"
  warning setting in the plugin settings (this is `defaultIgnoreMissingSamples`, section 5; it
  hides *all* missing-sample messages, so turn it off when troubleshooting with support). The
  removed mic then appears faded out in the plugin.
- No database, zconfig or settings edit is mentioned: the official method leaves the `.db` alone.
  This tool additionally patches `Containers` rows (the vi-control load-time fix).

## 6. Other products (same system) **[verified from Spitfire.properties]**

All products appear in `Spitfire.properties` the same way; only the path arrays differ:

| Layout | Examples |
|---|---|
| One root, `Config/Patches/Presets/Samples` folders | BBCSO, `Spitfire Originals - Cinematic Frozen Strings`, `... Intimate Grand Piano`, `... Abe Laboriel Jr Drums`, AIR Studios Reverb (no `config`) |
| Several search paths per key, product-named subfolders, one root shared by several entries | `Spitfire Audio - Eric Whitacre Choir` is the root of three entries (Choir, Evo Grid, Samples) |
| Many roots, per-instrument subfolders, a path that does not exist | LABS (`LABS Choir`, `LABS Hand Bells`, `LABS Common`, a typo'd `.../Software Instument Libraries/...` path) |

### Products checked that use this system (from one machine)

| Product | Properties entry | On disk when checked | Findings |
|---|---|---|---|
| BBC Symphony Orchestra | `BBC Symphony Orchestra` | yes | Everything above. Mic position in the sample file name. |
| Cinematic Frozen Strings | `Originals - Cinematic Frozen Strings` | yes, 2.5 GB | Flat layout: `Config/v1.0.2/*.plugin.zconfig`, `Patches/v1.0.2/*.zmulti`, `Presets`, `NKS`, `Samples/*.db` + 17 `.spitfire`. Same Containers/Files schema (17 containers, 4,072 files). |
| Intimate Grand Piano | `Originals - Intimate Grand Piano` | yes, 3.7 GB | Same flat layout (`v1.0.3`), 18 containers, 3,529 files. |
| Eric Whitacre Choir | `Eric Whitacre Choir`, `... Evo Grid`, `... Samples` (one shared root) | yes (153 GB) | `Patches/<Name>/v1.0.3, v1.2.1, v1.3.24/` and `Presets/<Name>/...` (two sub-products), no `Config/`, one DB `Samples/ERICWCHOIR_FULL.db` with 3,412 containers / 94,236 files, same schema. **Samples carry a mic token**: `ERICW_ALTOS_LEG-INTAAH_RR1_AM01_01.spitfire`. Tokens found: AM01 CL01-CL05 GA01 MI01 OU01 ST01-ST04 TR01 (names, and the five unlisted ones that are always copied: section 5b; BBCSO's table has none of CL02-05, GA01, ST01-04). Two names do not fit the BBCSO classifier: `..._AM01_01_V3.spitfire` (extra `_V3` suffix) and `ERICW_VERB_IR.spitfire`. |
| Abe Laboriel Jr. Drums | `Originals - Abe Laboriel Jr Drums` | yes (2.3 GB) | `Config/`, `Patches/`, `Presets/` exist but are **empty** (no zmulti/zpreset/zconfig): an incomplete install on that volume. `Samples/ORIGINALS_ABE_DRUMS.db` + 13 `.spitfire` named by instrument (`..._KICK_01`, `..._LOOPS_02`), no mic token. |

Their other Spitfire products are Kontakt instruments, which do not use this system.

**Mic trimming does not apply to the Originals libraries (Frozen Strings, Grand Piano, Drums).** It applies to Eric Whitacre Choir through its profile (a per-product mic table and a classifier that tolerates suffixes like `_V3`; see the paragraph below). Their sample names carry no mic token
(`ORIGINALS_CINEMATIC_FROZEN_STRINGS__LONG_FLAUTANDO_CS_01.spitfire`,
`ORIGINALS_INTIMATE_GRAND__PIANO__LONG_PEDAL_UP_01.spitfire`): one file per articulation/round
robin, so the mics are not separable by file name (the mix is inside the file or in the encrypted
patch). What does carry over to every product is the settings mechanism: re-pointing a product with
`mictrim use` / "Change current installation location…", the folder-name rule for Repair, and the diagnosis recipe.

Folder naming: `Spitfire Audio - <Product>`, `Spitfire Originals - <Product>`; older downloads such
as `Spitfire Chamber Strings library` do not follow this and have no entry in `Spitfire.properties`.
Consequences for generic tooling:

- A library "root" is the folder whose name the app expects. Re-pointing a library = for each Spitfire.properties
  entry the product owns, set its keys to the root plus the sub-paths the profile's `spitfire.json` lists
  (`repointLibrary()`); where the entry pointed before does not matter. BBCSO: `config`, `patches`, `presets`, `samples`
  (`Config`, `Patches`, `Presets`, `Samples`). Whitacre (read from a real file): entries "Eric Whitacre Choir"
  (patches/presets: `Eric Whitacre Choir`, `Patches|Presets`, `Patches|Presets/Eric Whitacre Choir`; samples `Samples`),
  "Eric Whitacre Choir Evo Grid" (patches/presets: `Eric Whitacre Choir Evo Grid`, `Patches|Presets/Eric Whitacre Choir Evo Grid`;
  samples `Samples`) and "Eric Whitacre Choir Samples" (samples `Samples`), all below the one library folder. The file is re-serialised on a change (keys sorted alphabetically, 4-space indent; every other entry's content is preserved, only whitespace and key order may differ); the first `.smt-backup` is never overwritten.
- Mic tokens and DB layout were verified for BBCSO and read from the Whitacre files. Trimming another product needs its own
  profile (below) and a check of its sample names.

**Profiles and the app bundle.** Per-product knowledge lives in profiles, not code: a `.smtprofile` macOS bundle (a folder Finder shows as one file) with a manifest `Contents/Info.plist` and JSON parts in `Contents/Resources/` (how to recognise the library folder and parse a sample file name, the mic table with the mics that must stay on (BBCSO Mix 1) marked `locked`, groups such as BBCSO sections or Whitacre voices, where to find the library's own artwork, notes). The tool ships BBCSO and Eric Whitacre Choir inside its app bundle (`Contents/Resources/Profiles/`); a user adds or overrides one in `<app data dir>/Profiles`. The profile is chosen by the library folder name, stored in the destination's `project.smt`, and decides which files are mics (anything else is copied, never deleted). Nothing about a profile touches Spitfire's own files; the app only reads the library's `NKS/PAResources/image/...` pictures to display them. See the design in `docs/superpowers/specs/2026-11-02-product-profiles-whitacre-design.md`; the Whitacre trial procedure is in `docs/ACCEPTANCE.md`.

## 6b. Patches and presets are signed per install **[verified on a real machine]**

The patch, preset and config files (`Patches/**`, `Presets/**`, `Config/**`; `.zconfig`, `.zmulti`, `.zpreset`) are encrypted/signed for one install. A byte copy made from a library that was not repaired for this install fails in the plugin ("failed to load patch config", Error #1 or #3 mean unusable patches). Spitfire's Repair rewrites those files in place with the same size, so a size comparison cannot tell a repaired file from a stale one. Run Repair on the source library, then Apply: the tool copies these files again every time (`isSettingsFile()` in `plan.cpp`). Samples (`.spitfire`) and `.db` files copy fine.

**Verified end to end (BBC Symphony Orchestra and Eric Whitacre Choir, Evo Grid included).** Install the full library on a remote disk; on the computer that will play it, run Spitfire's Repair on every section of the full library (repairing only some sections leaves the others on their Core patches and the plugin shows only that interface); then use this tool to copy the chosen mics to a local reduced library and "Change current installation location…" to point the plugin at it. New and removed mics keep working on later Applies. After a Spitfire update, run the update from the playing computer with the app pointed at the remote library, Repair again, then Apply. A copy made before the Repair (or on another install) fails with Error #1 or #3.

## 6c. Windows **[UNVERIFIED facts about Spitfire; the tool itself builds and passes its tests in CI on Windows]**

Everything in this section about where Spitfire keeps its files and how it behaves is a guess to be confirmed on a real install; none of it has been observed.

- The settings folder is probably `%USERPROFILE%\Music\Spitfire Audio\Settings` or the same below Documents. The tool
  tries Music, then Documents (first one holding a `Spitfire.properties`), and `SMT_SPITFIRE_SETTINGS_DIR` overrides both.
- `Spitfire.properties` probably holds backslash paths (`C:\\Libs\\...` in JSON). The tool writes native separators on
  Windows and reads either separator, ignoring case.
- Whether the app accepts a library on a mapped drive, a UNC path or a `subst` drive is unknown.
- Whether the same "folder name must match" rule for libraries (section 4) holds on Windows is unknown.

## 7. Debugging recipe

1. Plugin log: newest `~/Library/Logs/com.spitfireaudio/spitfireaudio*.log`. Useful lines:
   "FOUND PATCH CONFIG", "found N patches", "Attempting to Load Patch", "Error querying
   database", "ERROR #n", "WaveFS preload".
2. App: `lm.log`, but it does not explain library rejections. The check is by folder name.
3. Resolve ids from "Error querying" lines to mic positions with
   `select c.File from Files f join Containers c on c.ID=f.Container where f.ID in (...)` on the
   original DBs (open with `file:...?immutable=1`).

## 8. Open questions

- Why the app wants the exact folder name (no code read; empirical only).
- Whether `Files` row deletion changes load time; whether `defaultIgnoreMissingSamples` hides Error 4.
- How the plugin treats a Whitacre library with mics removed (Error 4 behaviour, `_V3` files);
  the Drums install that was checked had no patches (an incomplete install).
- Role of `Products.manifest`; what the "small download" in Repair fetches.

**Product images (`.ImageCache`).** The Spitfire app shows product pictures that it downloads from Spitfire's CDN; the URLs come from the API fields `image_url_pack_shot_1x`/`image_url_pack_shot_2x` and `image_url_cinemascope_1x`/`image_url_cinemascope_2x`. It caches them as extension-less image files (JPEG) in `~/Music/Spitfire Audio/Settings/.ImageCache/`, named by a 64-bit hash of the image URL (e.g. `-5946911695875460174`). The library itself holds only small thumbnails (`NKS/PAResources/image/...`). The profiles use `cache:` sources that read this folder read-only at display time (the pictures are never copied into this repo or the app): BBCSO Professional `-5946911695875460174` and `-5946911696707943383` (500x500) and `-2722765241741408607` (250x250); Eric Whitacre Choir `762646866156051825` and `762646877082424537` (500x500) and `-3978629468329901306` (250x250). The names are hashes of Spitfire's image URLs and may change (or the app may never have cached them), which is why the `url:` and (BBCSO) `file:` sources follow.

**Product images on the public website.** The same pack shots are on Spitfire's own site under simple URLs (verified): BBCSO Professional `https://www.spitfireaudio.com/cdn/shop/files/smc0201_square_press.jpg?width=500`, Eric Whitacre Choir `https://www.spitfireaudio.com/cdn/shop/files/EricWhitacreChoirSquare.png?width=500` (`?width=500` is the shop's resize parameter: about 30 KB instead of 0.9 MB). Product pages (the profiles' picture credit): `https://www.spitfireaudio.com/bbc-symphony-orchestra-professional` and `https://www.spitfireaudio.com/eric-whitacre-choir`. The profiles use these as `url:` sources, downloaded once at run time on the user's machine into `<app data dir>/ImageCache` (nothing is bundled). The CDN-style `.ImageCache` names above remain a local-only bonus that comes first when the Spitfire app has cached them.
