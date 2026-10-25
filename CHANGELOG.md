# Changelog

All notable changes to this project. Versions are `MAJOR.MINOR.PATCH`.

The upstream project (1.2 and earlier) is archived and is not part of this
history — see [dezem/Safe_Shutdown](https://github.com/dezem/Safe_Shutdown).

## [1.4.4] - 2026-10-25

First public release of the rewrite.

### Fixed

- **Shutdown works again on current Atmosphere.** The upstream build called
  `spsmShutdown(false)`, which talks to Atmosphere's private `spsm` service.
  That behaviour changed across major Atmosphere releases and the call silently
  became a no-op. This build uses Nintendo's official applet APIs instead.
- **Shutdown from an NSP forwarder works.** The two official calls are
  applet-type restricted and mutually exclusive: `appletStartShutdownSequence()`
  needs a SystemApplet (or a LibraryApplet/OverlayApplet on 15.0.0+), while
  `appletRequestToShutdown()` needs an `Application`. An NRO launched from the
  Album is a LibraryApplet; one launched from a forwarder is an Application.
  Using the wrong call fails and drops back to the home menu. The tool now
  dispatches on `appletGetAppletType()`.

### Added

- 3 second countdown, press **B** or **+** to cancel.
- Three-stage fallback: primary API → the other official API → `spsmShutdown()`,
  with a diagnostic screen that prints every return code if all of them fail.
- Centered UI computed from the console dimensions, with large 7x10 block digits.
- `COUNT_STYLE` build option to change the digit fill character
  (`#` / `@` / `=`+`|` / `+`).
- `tools/nacp_info.py` to read name / author / version from an NRO's embedded NACP.
- `mode:` indicator in the UI showing which applet type you were launched as.

### Changed

- Dropped the NSP build path (`Safe_Shutdown.json`, `Safe_Shutdown.xml`,
  `build.sh` with hacBrewPack). Only the NRO is maintained.
- Version number is single-sourced: `APP_VERSION` in the Makefile is passed to
  the compiler with `-DAPP_VERSION`, so the NACP and the on-screen text cannot
  drift apart.
- `APP_AUTHOR` is now `smj`.
