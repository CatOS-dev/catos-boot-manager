# CatOS Boot Manager

A Qt Widgets boot-loader manager using the CatOS Hello navigation and palette.
It is an independent desktop application, launched from CatOS Hello or directly.

## Architecture

- `src/model`: immutable discovery types and a replaceable staged transaction.
- `src/services`: read-only firmware / ESP / disk discovery, restricted loader
  settings, and repair plan validation plus mount lifecycle.
- `src/pages`: overview, menu, loader settings, firmware entries, maintenance.
- `src/widgets`: controls styled to match CatOS Hello.
- `src/dialogs`: review all staged changes and run one `pkexec` authorization.
- `src/privileged`: separate, validated non-shell transaction executor.

Nothing is written when a setting or firmware action is selected. Changes are
queued in the footer, reviewed together, and applied only after confirmation.
The GUI delivers the transaction to the privileged executor over stdin, not as
command-line arguments. Administrator authorization happens once via `pkexec`.
The privileged executor re-reads the relevant configuration and firmware state
before writing. Configuration is managed through the native generators:
`grub-mkconfig`, `limine-mkinitcpio`, and systemd-boot loader configuration.
Kernel entry generation is deliberately not reimplemented in the GUI.

Firmware writes cannot be made fully atomic by a GUI: an EFI NVRAM change may
remain if a subsequent operation fails. The review dialog explicitly warns of
this and shows operation output. EFI files are not deleted when deleting an NVRAM
entry. All commands are invoked through an argument vector, not shell strings.

## Supported workflows

- Detect the boot firmware mode, current UEFI selection and installed boot
  loader files on each mounted ESP. Show their configuration locations.
- Edit common GRUB, Limine and systemd-boot settings; use native generators.
- View each loader's menu. With the accompanying new `limine-tool`, navigate a
  structured Limine menu; add kernel/initramfs, UKI and EFI entries, remove a
  selected entry, select the default and change menu timeout.
- Manage UEFI BootOrder, BootNext, entry creation/removal, enable/disable and
  entry labels via `efibootmgr`.
- Rebuild existing GRUB and Limine configuration, and update systemd-boot.
- Install or repair GRUB in BIOS/UEFI mode, and install/rebuild Limine or
  systemd-boot in UEFI mode, with a *user-chosen* destination and optional
  firmware registration. The default is never to overwrite EFI fallback.
- Choose the current system, a separately mounted Linux system, or choose a
  Linux root partition in a Live environment. For the latter, explicitly choose
  the Btrfs subvolume (when applicable), a FAT ESP and its intended mount point.
  Root and ESP are mounted only inside the privileged transaction, commands
  execute via `arch-chroot`, and mounts are cleaned up in reverse order.

For multi-ESP systems the application retains the ESP-specific target. Boot
loader *installation* is always a separate explicit action; modifying menu
timeout never performs `grub-install` or changes UEFI registration implicitly.

The new Limine menu and effective-settings APIs are provided by the adjacent
`limine-tool` source tree (`--tree-json`, `--config-json`, `--set-global`). When
an older binary is installed, its menu is shown read-only; edits requiring the
new APIs are disabled. Reading must never trigger the old wrapper's sudo/FIDO
prompt: the read-only probe uses the underlying binary for legacy wrappers.

This is a development build. Compiling and opening the GUI do not prove a real
machine will boot after an installation or repair. Verify the chosen disk,
filesystem and ESP before applying; firmware and multi-step loader operations
cannot be made fully atomic. The application does **not** auto-install packages
or change boot state on startup.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The build produces `catos-boot-manager` and `catos-boot-apply`. The latter is
installed in `/usr/lib/catos/` and is invoked through `pkexec` only on apply.

Dependencies: Qt 6 Widgets, Qt LinguistTools, CMake and a C++17 compiler.
Runtime tools vary by installed loader: `efibootmgr`, `grub-mkconfig`,
`limine-tool` and `bootctl`.
