# CatOS Boot Manager

A Qt Widgets boot-loader manager using the CatOS Hello navigation and palette.
It is an independent desktop application, launched from CatOS Hello or directly.

## Architecture

- `src/model`: immutable discovery types and a replaceable staged transaction.
- `src/services`: read-only firmware / ESP discovery and restricted loader settings.
- `src/pages`: overview, menu, loader settings, firmware entries, maintenance.
- `src/widgets`: controls styled to match CatOS Hello.
- `src/dialogs`: review all staged changes and run one `pkexec` authorization.
- `src/privileged`: separate, validated non-shell transaction executor.

Nothing is written when a setting or firmware action is selected. Changes are
queued in the footer, reviewed together, and applied only after confirmation.
The privileged executor re-reads the relevant configuration and firmware state
before writing. Configuration is managed through the native generators:
`grub-mkconfig`, `limine-mkinitcpio`, and systemd-boot loader configuration.
Kernel entry generation is deliberately not reimplemented in the GUI.

Firmware writes cannot be made fully atomic by a GUI: an EFI NVRAM change may
remain if a subsequent operation fails. The review dialog explicitly warns of
this and shows operation output. EFI files are not deleted when deleting an NVRAM
entry. All commands are invoked through an argument vector, not shell strings.

The initial scope covers locally mounted installations. Booting into a Live
environment and mounting/chrooting a different system belongs to CatOS Rescue,
not this application. Use the separate recovery flow for that purpose.

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
