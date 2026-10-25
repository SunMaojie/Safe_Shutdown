# Safe_Shutdown (rewritten)

A safe shutdown tool for Nintendo Switch homebrew.

The [original project](https://github.com/dezem/Safe_Shutdown) by **kempa / dezem**
is archived, and its shutdown call no longer works on recent Atmosphere releases.
This is a rewrite that talks to Nintendo's **official** applet APIs instead, so it
keeps working across Atmosphere updates.

Works on Erista and Mariko, no cracking noise on Mariko.

---

## Why the original stopped working

The original is 11 lines:

```c
spsmInitialize();
usleep(3000000);
spsmShutdown(false);   /* Atmosphere's private spsm service */
```

`spsm:*` is an Atmosphere-internal service. Its behaviour changed across major
Atmosphere releases, so the call silently became a no-op. The NRO has no UI, so
there was nothing to see — it just did nothing.

## The catch that matters most: shutdown APIs are applet-type restricted

This is the part that costs hours if you don't know it. From `libnx`'s `applet.h`:

| API | Available for | Goes through |
|---|---|---|
| `appletStartShutdownSequence()` | **SystemApplet only**, or `LibraryApplet` / `OverlayApplet` on 15.0.0+ | `IGlobalStateController` |
| `appletRequestToShutdown()` | **`AppletType_*Application` only** (infinite sleep on success) | `IApplicationFunctions` |

The two are mutually exclusive, and which one you need depends on **how your NRO
was launched**:

- **From the Album (hbmenu)** → hbmenu is a `LibraryApplet`, so your NRO is too
  → `appletStartShutdownSequence()` works.
- **From an NSP forwarder** → your NRO is a real installed title, i.e. an
  `Application` → `IGlobalStateController` doesn't even exist for it, so
  `appletStartShutdownSequence()` **always** fails. You must use
  `appletRequestToShutdown()`.

Getting this wrong looks exactly like "it counts down, then drops back to the
home menu". This build dispatches on `appletGetAppletType()` and then falls back
through the other API and finally Atmosphere's `spsmShutdown()`, printing the
return code of each on screen if everything fails.

## Features

- **3 second countdown**, press **B** or **+** to cancel
- Works from **Homebrew Menu** and from an **NSP forwarder**
- Three-stage API fallback with on-screen error codes
- Centered UI with large 7x10 block digits

## Usage

Copy `SafeShutdown.nro` to `sdmc:/switch/` and launch it from the Homebrew Menu
(hold **R** while opening the Album). You can also install it as a forwarder
(e.g. with Sphaira) so it appears on the home menu.

Press **B** or **+** during the countdown to abort.

## Build

Requires [devkitPro](https://devkitpro.org) with `libnx` and `switch-tools`.

```sh
export DEVKITPRO=/opt/devkitpro
make
```

Output: `SafeShutdown.nro`.

### Building without devkitPro installed (Docker)

```sh
docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 \
  bash -c 'make clean && make'
```

Two Makefile details that are easy to get wrong:

1. **`export LD := $(CXX)`** — devkitPro's `base_tools` exports `CC`, `CXX`, `AS`,
   `AR`, … but **not `LD`**. Without this, `switch_rules`' `%.elf` rule falls back
   to the host `ld`, which ignores `-specs=switch.specs`, and every libnx symbol
   ends up undefined.
2. **`$(OUTPUT).nro: $(OUTPUT).nacp`** — `switch_rules`' `%.nro: %.elf` rule runs
   `elf2nro --nacp=...` directly and does not declare the dependency, so the first
   build fails with `Failed to open input nacp!` unless you add it.

### Version number

The version shown in the Homebrew Menu comes from the **NACP**, generated from
`APP_VERSION` in the Makefile — *not* from any string in the source. This project
defines it once and passes it to the compiler:

```make
APP_VERSION := 1.4.4
CFLAGS += -DAPP_VERSION="\"$(APP_VERSION)\""
```

Check what actually ended up in a build:

```sh
python tools/nacp_info.py SafeShutdown.nro
```

### Digit style

The countdown digits are 7x10 block glyphs. The fill character is selectable:

| `COUNT_STYLE` | Fill | Notes |
|---|---|---|
| `1` (default) | `#` | regular grid, reads best |
| `2` | `@` | denser, but each glyph has internal detail — turns to noise |
| `3` | `=` + `\|` | thin 7-segment, authentic but verticals are 1 char wide |
| `4` | `+` | lighter grid |

```sh
make COUNT_STYLE=2
```

Only ASCII is usable: `libnx`'s console only rasterises ASCII glyphs, so `█`
(U+2588) and CJK text render as nothing.

## Files

```
source/main.cpp        the whole tool
Makefile               devkitPro / libnx build
icon.jpg               256x256 JPEG embedded as the NACP icon
tools/nacp_info.py     read name / author / version from an NRO's NACP
```

## Credits

- **kempa / dezem** for the original [Safe_Shutdown](https://github.com/dezem/Safe_Shutdown)
- **SciresM** and the [Atmosphere-NX](https://github.com/Atmosphere-NX/Atmosphere) team
- **devkitPro** and the **libnx** contributors

## License

GPL-2.0 — see [LICENSE](LICENSE). This is a derivative of a GPL-2.0 project, so it
stays under the same license.
