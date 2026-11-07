# CatOS Boot Manager

CatOS 的 Qt 6 引导管理器，提供按引导方式独立组织的安装和启动菜单页面。

## 页面与操作

- **引导方式**：GRUB、Limine、rEFInd、systemd-boot、UKI、EFI Stub 各自拥有软件包安装、部署及固件启动项操作；systemd-boot 另有程序更新。安装类操作立即执行。
- **启动菜单**：菜单编辑、默认项和重建集中在这里。修改先暂存，统一查看并应用。
- **Limine**：编辑嵌套分组和启动项、调整顺序、默认项、等待时间、内核参数和菜单外观，按所选 EFI 分区保存。
- **systemd-boot**：增删改 BLS 启动项，编辑内核、多份 initramfs、启动参数、默认项及 loader.conf。菜单通过 bootctl 读取。
- **rEFInd**：编辑手动启动项、启动参数、扫描、默认项和外观设置；读取包含文件并把设置写回实际来源；部署时保留已有配置。
- **UKI**：选择目标系统和安装目录，使用 catos-firmware-boot 为全部已安装内核生成镜像；支持部署、更新、修复和重建。
- **EFI Stub**：使用 catos-firmware-boot 按内核版本生成 initramfs，成对部署内核和 initramfs，并管理对应固件启动参数。
- **固件**：管理 BootOrder、BootNext、启动项增删和启用状态。
- **安全启动**：显示固件状态，按证书列出已注册和待确认的 MOK；可请求注册 DER 证书（设置一次性密码，只经标准输入交给 mokutil，不写入日志）或取消待确认的注册。
- **配置与更新**：按安装实例编辑等待时间、内核参数、外观等设置，修改先暂存。
- **修复与恢复**：显示所选安装的诊断结果；可修复当前系统、已挂载系统，或临时挂载未运行的 Linux 根分区和 ESP 后修复。

UKI 和 EFI Stub 统一使用目标系统的 `/etc/catos/firmware-boot.conf` 和 `/etc/kernel/cmdline`；当前系统的配置通过包提供的接口读取；已挂载的其他系统只按数据解析其配置文件，检测时不执行其中的任何程序。修改配置统一通过包接口，安装成功后保存目录和固件注册选项，供后续软件包更新使用。安装目录可以是普通目录，不检查分区类型或文件系统。安装支持当前系统、已挂载系统，以及临时挂载的 Linux 根分区。

## 执行结构

启动时通过 pkexec 授权一次。取消或授权失败时以只读模式打开：以桌面用户身份检测，可以准备修改，授权后才能应用。检测过程按阶段显示进度。界面以桌面用户运行，常驻特权进程负责扫描、读取配置和执行操作。两者通过私有进程管道通信；程序关闭时结束特权会话。

审阅对话框通过特权进程只读地渲染将要写入的文件，以统一差异格式展示每个配置文件的改动。执行前重新检查配置、菜单指纹和目标分区。菜单文件修改先于同一文件的全局设置修改，随后进行所需重建。应用前，配置原件、操作记录和固件状态保存在 `/var/lib/catos-boot-manager/backups/`。“修复与恢复”页列出这些备份，可单独恢复其中的配置文件：写回原件并删除该次修改新建的文件；恢复前同样会备份当前文件。固件项、生成的菜单和镜像不在恢复范围内。

Limine 菜单使用相邻 `limine-tool` 项目的原生解析器，由 `catos-limine-menu` 提供结构化读写。固件项参数更新使用 libefiboot/libefivar，保留现有设备路径、编号和启动顺序。新建固件项不改变原有启动顺序，只追加到 BootOrder 末尾；默认项只能通过显式操作修改。

## 构建与验证

依赖：Qt 6 Widgets、Concurrent、Test、LinguistTools，CMake、Ninja、C++17 编译器、Rust/Cargo、pkg-config、libefiboot 和 libefivar。运行时还需要 `binutils`（`objdump`）、`sbsigntools`（`sbverify`）、`mokutil`（安全启动 MOK 操作）以及按所选引导方式提供的 `pacman`、`efibootmgr`、`grub-install`、`grub-mkconfig`、`limine-install`、`bootctl` 和 `catos-firmware-boot-update`；缺少工具时对应检测或操作会显示不可用。

默认从相邻 `../limine-tool` 目录构建 Limine 解析器，可通过 `-DLIMINE_TOOL_SOURCE=/path/to/limine-tool` 指定源码位置。

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cargo test --manifest-path build/limine-menu/Cargo.toml --target-dir build/menu-tools
```

生成 `catos-boot-manager`、`catos-boot-apply` 和 `catos-limine-menu`。桌面程序安装到 `bin`，两个辅助程序安装到 `lib/catos`。

运行工具按所选引导方式使用：pacman、efibootmgr、grub-install、grub-mkconfig、limine-tool、bootctl、catos-firmware-boot-update；挂载其他系统使用 mount、umount 和 arch-chroot。

测试使用临时目录和模拟命令验证菜单文件、部署位置、配置保留、失败处理及界面信号。UKI／Stub 测试验证主程序调用包接口、传递安装目录、保存设置和传播执行失败；生成工具自身的测试由 catos-firmware-boot 包负责。特权会话测试使用 unshare 用户命名空间。
