<p align="center">
  <a href="https://github.com/pablogherreram/usbloadergx-link" title="USB Loader GX Link">
    <img src="data/web/logo.png" alt="USB Loader GX Link">
  </a>
</p>

<p align="center">
  <strong>A customized USB Loader GX fork with a Wii-style startup experience and automatic SD card access over FTP.</strong>
</p>

<p align="center">
  <a href="https://github.com/pablogherreram/usbloadergx-link/actions" title="Build status">
    <img src="https://img.shields.io/github/actions/workflow/status/pablogherreram/usbloadergx-link/main.yml?branch=enhanced&logo=github&label=build" alt="Build status">
  </a>
  <a href="https://github.com/pablogherreram/usbloadergx-link/commits/enhanced" title="Latest commit">
    <img src="https://img.shields.io/github/last-commit/pablogherreram/usbloadergx-link/enhanced?logo=github&label=updated" alt="Latest commit">
  </a>
  <a href="https://github.com/pablogherreram/usbloadergx-link/blob/enhanced/LICENSE.txt" title="License">
    <img src="https://img.shields.io/github/license/pablogherreram/usbloadergx-link?label=license" alt="License">
  </a>
</p>

# USB Loader GX Link

USB Loader GX Link is a customized fork of [USB Loader GX](https://github.com/wiidev/usbloadergx), based on its `enhanced` branch.

The project preserves the original loader experience and compatibility while introducing a cleaner Wii-style startup screen and an integrated FTP service designed to make the Wii's SD card remotely accessible while USB Loader GX is running.

> [!IMPORTANT]
> The integrated FTP service is currently under development and is not yet available in the current build.

## Current features

- Customized white Wii-style startup screen.
- Wii Menu-inspired startup background.
- Simplified startup presentation without progress text.
- Compatibility with the existing USB Loader GX configuration, forwarder and `UNEO` return target.
- All original USB Loader GX functionality provided by the upstream `enhanced` branch.

## Planned FTP integration

USB Loader GX Link will include a lightweight FTP server that starts automatically after USB Loader GX initializes the SD card and network connection.

The FTP service is intended to provide:

- Automatic startup while USB Loader GX is running.
- Remote access to the Wii's SD card from a computer on the same local network.
- File downloads and uploads.
- Directory creation, renaming and deletion.
- Passive FTP support for compatibility with clients such as WinSCP.
- A fixed virtual root mapped exclusively to `sd:/`.
- No built-in file browser inside USB Loader GX.
- No access to USB storage, game partitions, emuNAND, the Wii NAND or ISFS.

The FTP module will be designed so that NAND and ISFS operations are not merely hidden from the client, but excluded from the FTP file-access layer entirely.

FTP should only be used on a trusted local network. Traditional FTP does not encrypt credentials or transferred files, and the service should not be exposed to the Internet through router port forwarding.

## Installation

> [!NOTE]
> Until an official USB Loader GX Link release is published, the repository must be compiled from source. The original upstream USB Loader GX releases do not contain the planned FTP integration or the customized startup screen included in this fork.

When a USB Loader GX Link build is available:

1. Extract the included `apps` folder to the root of the SD card.
2. Replace the existing USB Loader GX files when prompted.
3. Install the [latest d2x cIOS](https://github.com/wiidev/d2x-cios/releases) appropriate for the console.
4. Optionally update `wiitdb.xml` from the update menu inside USB Loader GX.
5. Optionally install the USB Loader GX [forwarder channel](https://raw.githubusercontent.com/wiidev/usbloadergx/updates/USBLoaderGX_forwarder%5BUNEO%5D.wad).
6. To return to USB Loader GX after leaving a game or supported application, open `Loader Settings` and set `Return To` to `UNEO`.

## d2x cIOS

Use the correct d2x cIOS package for the target console:

- `d2x-v11-beta3` for Wii.
- `d2x-v11-beta3-vWii` for the Wii U's virtual Wii environment.

When using the d2x cIOS installer, select the version matching the downloaded package and install the required slots with the following bases:

```text
Slot 248 base 38
Slot 249 base 56
Slot 250 base 57
Slot 251 base 58
```

## Project status

| Component | Status |
|---|---|
| Wii-style startup background | Implemented |
| Customized startup logo | Implemented |
| Hidden startup progress text | Implemented |
| Automatic network initialization for FTP | Planned |
| Background FTP server | Planned |
| SD-only virtual filesystem | Planned |
| Passive FTP transfers | Planned |
| Upload and file management operations | Planned |
| NAND and ISFS isolation | Planned |
| Public downloadable release | Not yet available |

## Compatibility goals

USB Loader GX Link aims to retain compatibility with:

- Existing USB Loader GX configuration files.
- The standard `apps/usbloader_gx` installation directory.
- The official USB Loader GX forwarder.
- The `UNEO` return target.
- Wii and vWii environments already supported by the upstream project.

Internal identifiers and paths will remain unchanged unless a modification is technically necessary. The `Link` name identifies this fork and does not imply a replacement for the official USB Loader GX project.

## Credits

USB Loader GX Link is based on [USB Loader GX](https://github.com/wiidev/usbloadergx) and would not exist without the work of its original developers and contributors.

The planned FTP implementation may incorporate or adapt portions of https://github.com/joedj/ftpii, originally developed by Joseph Jordan, in accordance with its license. Any incorporated source files will retain the required copyright and license notices and will be clearly marked when modified.

Additional acknowledgments and license notices will be added as the FTP integration is implemented.

## Disclaimer

USB Loader GX Link is an unofficial community fork and is not affiliated with or endorsed by the official USB Loader GX maintainers.

This software is provided without warranty. Always keep a backup of important SD card contents before testing development builds, especially builds that introduce network-based file writing.

## Development branch

Active development currently takes place on the `enhanced` branch.
