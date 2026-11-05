# CatOS Boot Manager

CatOS 的 Qt 6 引导管理器，提供按引导方式独立组织的安装和启动菜单页面。

## 页面与操作

- **引导方式**：GRUB、Limine、rEFInd、systemd-boot、UKI、EFI Stub 各自拥有软件包安装、部署、修复、更新及固件启动项操作。安装类操作立即执行。
- **启动菜单**：配置、菜单编辑和重建集中在这里。修改先暂存，统一查看并应用。
- **Limine**：编辑嵌套分组和启动项、调整顺序、默认项、等待时间、内核参数和菜单外观，按所选 EFI 分区保存。
- **systemd-boot**：增删改 BLS 启动项，编辑内核、多份 initramfs、启动参数、默认项及 loader.conf。菜单通过 bootctl 读取。
- **rEFInd**：编辑手动启动项、启动参数、扫描、默认项和外观设置；读取包含文件并把设置写回实际来源；部署时保留已有配置。
- **UKI**：选择目标系统、内核、initramfs、启动参数和可选签名材料，通过 ukify 生成镜像；支持部署、更新、修复和重建。
- **EFI Stub**：将所选内核和 initramfs 部署到指定 EFI 分区，并管理对应固件启动参数。
- **固件**：管理 BootOrder、BootNext、启动项增删和启用状态。

UKI 和 EFI Stub 为每个镜像独立保存生成设置，位于镜像旁的 `.catos.json` 文件。重建时根据系统分区身份定位目标系统。安装支持当前系统、已挂载系统，以及临时挂载的 Linux 根分区。

## 执行结构

启动时通过 pkexec 授权一次。界面以桌面用户运行，常驻特权进程负责扫描、读取配置和执行操作。两者通过私有进程管道通信；程序关闭时结束特权会话。

执行前重新检查配置、菜单指纹和目标分区。菜单文件修改先于同一文件的全局设置修改，随后进行所需重建。应用前，配置原件、操作记录和固件状态保存在 `/var/lib/catos-boot-manager/backups/`。

Limine 菜单使用相邻 `limine-tool` 项目的原生解析器，由 `catos-limine-menu` 提供结构化读写。固件项参数更新使用 libefiboot/libefivar，保留现有设备路径、编号和启动顺序。

## 构建与验证

依赖：Qt 6 Widgets、Concurrent、Test、LinguistTools，CMake、Ninja、C++17 编译器、Rust/Cargo、pkg-config、libefiboot 和 libefivar。

默认从相邻 `../limine-tool` 目录构建 Limine 解析器，可通过 `-DLIMINE_TOOL_SOURCE=/path/to/limine-tool` 指定源码位置。

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cargo test --manifest-path build/limine-menu/Cargo.toml --target-dir build/menu-tools
```

生成 `catos-boot-manager`、`catos-boot-apply` 和 `catos-limine-menu`。桌面程序安装到 `bin`，两个辅助程序安装到 `lib/catos`。

运行工具按所选引导方式使用：pacman、efibootmgr、grub-install、grub-mkconfig、limine-tool、bootctl、ukify；挂载其他系统使用 mount、umount 和 arch-chroot。

测试使用临时目录和模拟命令验证菜单文件、部署位置、配置保留、失败处理及界面信号。UKI 测试使用原生 ukify、bootctl 和 objcopy 检查生成镜像中的内核、initramfs 和启动参数。特权会话测试使用 unshare 用户命名空间。
