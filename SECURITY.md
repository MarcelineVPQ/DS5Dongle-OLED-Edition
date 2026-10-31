# Security

## Getting DS5 Dongle OLED Edition safely

- Download the firmware and the configurator app only from this repository's [releases](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases). Each release lists the SHA-256 of every file in `SHA256SUMS.txt`.
- The app's update feed (`latest.json`, `latest-dev.json`) is signed with Ed25519 (`latest.json.sig`, `latest-dev.json.sig`); the app refuses a feed whose signature doesn't check out. The feeds are signed only on the maintainer's own computer, never in GitHub Actions: the signing key is not on GitHub, so a release published there by someone else is never offered to installed apps.
- The app installs the firmware bundled with its own release, or a release from this repository.

## Supported versions

Only the latest release gets security fixes (and the latest development build on the Development channel). Please update before reporting.

## Scope

- The firmware: what it accepts over USB (HID feature and output reports, the configurator's vendor interface, UAC1 audio) and over Bluetooth (pairing, L2CAP control and interrupt channels), and what it stores in flash.
- The release files: the UF2, `SHA256SUMS.txt` and the signed update feed.
- The configurator app's code lives in [DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web) (its audit: `docs/SECURITY_AUDIT.md` there); a report about the app is welcome here too.
- Problems in the core bridge that also affect upstream [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) are fixed here and passed on upstream once a fix is out.

## Reporting a vulnerability

Please report it privately through GitHub: **[Security → Report a vulnerability](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/security/advisories/new)** on this repository, not in a public issue. Include what you found, how to reproduce it, the firmware version (the OLED Status screen or the app's board page) and your board (Pico 2 W or Pico W). You'll get an answer as soon as possible; this is a free project, so please allow some days.
