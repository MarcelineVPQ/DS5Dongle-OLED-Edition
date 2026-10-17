# Pico2W DualSense 5 Bridge — OLED Edition

[English](README.md) · **中文** · [Deutsch](README.DE.md) · [Español](README.ES.md) · [Français](README.FR.md) · [日本語](README.JA.md) · [Português (Brasil)](README.PT-BR.md)

> 把 Raspberry Pi Pico2W 变成 DualSense (DS5) 控制器的无线适配器 —— 还可选配板载状态显示屏。

> **OLED Edition** 是 **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)**（上游）的一个分支，增加了可选的 Pico-OLED-1.3 128×64 显示屏插件，提供 11 个屏幕（状态、4 槽多控制器配对、带收藏与特效预设的光条调色器、扳机测试、陀螺仪倾斜、触摸板、诊断、CPU/时钟、蓝牙信号强度、音频 VU 表，以及一个持久化设置菜单），外加 DS5 按键组合软重启。核心桥接固件以上游为权威来源；本分支跟踪上游，并在其之上叠加插件功能。

> **🧪 想先用上最新功能？试试开发通道。** 在应用中打开 **应用设置 → 更新 → 开发版**（会先询问确认；随时可用 **回到稳定版** 返回）。开发版每次改动都通过与稳定版相同的自动测试；稳定版还会在真实硬件和 Windows 上完成全部发布检查，并经过一段日常使用。当前开发版 **0.9.5-dev.18** 相比稳定版 0.9.4 新增：
>
> - **DualSense Edge：** 重新分配 Fn键和背面键，PlayStation Accessories 中的 Edge 自定义配置文件，以及专属的控制器绘图和 OLED 视图
> - **操作杆：** 灵敏度曲线和操作杆测试
> - **控制器麦克风：** 麦克风音量、回声消除，静音键指示灯跟随静音状态；电脑的静音和麦克风音量会同步到控制器
> - **睡眠与唤醒：** 电脑睡眠时控制器自动关闭，PS键可唤醒电脑
> - **游戏（Linux）：** 为每个游戏选择任意 Proton 版本、HDR 设置，仅支持 XInput 的游戏也能找到控制器
> - **在应用中操作 OLED：** 实时画面旁的 KEY0/KEY1 和控制器按键，以及 **分享 OLED**（PNG、剪贴板或 GIF）
> - **控制器固件检查** 和控制器健康信息
> - **耳机（Linux）：** 耳机插入控制器时切换为立体声，拔出后回到扬声器
> - **在设备上配对：** OLED 提示 *Press PS to connect*；按住 KEY0 2 秒开始配对并显示倒计时；空闲时不再一直扫描
> - **所有提示均支持 7 种语言**

---

## 🛠️ 快速上手：下载应用

**[→ 下载 DS5 Dongle OLED Edition Configurator](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest)**（Linux AppImage 和无需管理员权限的 Windows 安装程序，两者都经过测试；macOS 版本由社区测试：[已有用户报告在 macOS 27.0.1 上可用](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/17)）

只需要这个应用。每个版本都自带对应的固件，应用会把固件装到你的 Pico 上、保持更新，并配置所有功能：

1. 组装好你的开发板（见下方[硬件](#硬件)），然后打开应用。
2. **按住 BOOTSEL 按钮**把 Pico 插到电脑上（全新的 Pico 不需要按）。应用会找到它，一键安装固件，无需手动拷贝文件。
3. 配对控制器：按住 **PS键 + 创建键** 直到光条闪烁。连接成功后应用会显示。

![应用：实时显示你的控制器，使用它本身的配色](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/live-controller.png)

你的控制器按它本身的配色绘制，并随你的操作实时变化；点击任意部位即可设置。**[分步指南](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/GUIDE.md)**（英文）从组装开发板讲到每一项设置：

- **控制器：** 光条模式和收藏颜色、自适应扳机效果、**左右分别设置振动强度的触觉反馈**（触觉反馈和传统振动都适用，带 3 秒测试）、音频自动触感、扬声器和内置麦克风（带测试）、按键配置（所有按键一个列表，也包括 DualSense Edge 的 Fn键和背面键，以及游戏运行时可切换的**已保存按键布局**，不写入闪存：固件 v0.9.4）、四个配对槽位，以及**控制器信息**（控制器自己的固件，并与索尼最新版本对比、电池电压和累计计数）。
- **校准：** 操作杆漂移、操作杆或扳机推不到底、陀螺仪缓慢漂移，都在 Pico 里修正（按配对槽位），对所有游戏生效；不会写入控制器。另有每个操作杆的**灵敏度曲线**和操作杆测试（圆度和漂移）。
- **开发板：** OLED 屏幕实时镜像、诊断仪表板（温度、时钟、蓝牙和 USB 流量、音频管线，以及固件各部分的耗时）、电源、显示和 USB 设置，以及**一键固件更新**（无需按 BOOTSEL，也无需拷贝文件）。
- **游戏：** 你的 Steam、Heroic 和 Lutris 游戏库（Windows：Steam、GOG Galaxy 和 Epic），列表和封面网格两种视图，每个游戏有自己的控制器配置（游戏改动的每一项控制器设置，包括按键布局和操作杆曲线，可在控制器视图中实时编辑），每个游戏的 DualSense 支持情况；Linux 上还有设置修复和 DualSense Proton 版本。
- **个性化：** 按控制器配色的主题、玻璃、外观滑块、色觉模式、减少动态效果、通过控制器扬声器播报的旁白、7 种语言、托盘图标。

应用通过 **v0.8 或更新**固件中的一个小型配置用 USB 接口工作（校准和左右分别的振动强度需要 **v0.9**；应用会提示更新）。游戏看不到这个接口，所以在游戏眼中 Pico 仍与真正的 DualSense 完全一致。应用源码：**[MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web)**。

> **浏览器配置工具已停用**：桌面应用能做它能做的一切，而且更多。它原来的地址 [marcelinevpq.github.io/DS5Dongle-OLED-Config-Web](https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/) 现在是应用的下载页。不用应用刷写 Pico：插入时按住 BOOTSEL，把版本里的 `.uf2` 拷贝到出现的驱动器上。

## 🔄 更新与固件安装，交给应用

**应用会自己保持最新。**

- **签名并校验。** 每个更新都来自同一个用我们的 Ed25519 密钥签名的更新源。没有有效签名的更新源会被忽略；每个下载在安装前都会校验大小和 SHA-256；应用绝不会自行安装更旧的版本。
- **两个通道：** 面向所有人的 **稳定版**，以及提供最新功能的 **开发版**。**回到稳定版** 会把应用和固件都恢复到最新的稳定版。
- **由你决定：** 打开 **自动安装更新**（应用只问一次）后，更新会在后台下载并校验，下次打开应用时启动新版本。关闭 **自动检查更新** 后，只有在你按下检查按钮时应用才会联网。
- **各系统：** 在 Linux 上，AppImage 把新版本放在自己旁边，菜单项随之指向新版本。在 Windows 上，按用户安装的安装程序无需管理员权限。在 macOS 上，应用会显示更新并附上发布页面的链接。

**一键安装固件。**

- **随附：** 每个应用版本都带有与它一起构建和测试的固件，因此新板子离线也能完成设置。更新的固件来自同一个签名更新源。
- **首次设置：** 按住 BOOTSEL 插入 Pico（全新的 Pico 不需要按键）。应用会找到它并安装固件。
- **更新：** 无需按键，也无需复制到磁盘。应用自己让板子重启进入 BOOTSEL，通过 USB 写入固件并重新连接。
- **写入前校验：** 文件的 SHA-256 必须与更新源一致，且必须是这颗芯片的固件。未通过的文件不会写入任何内容。
- **写入后确认：** 应用会等待新固件启动。如果板子反而回到 BOOTSEL，应用会说明情况并提供 **重新安装**。写入固件期间，应用不会重启、退出或自我更新。
- **你的设置保留：** 设置、配对、校准和按键分配位于闪存中单独的区域。每次固件构建都会检查它不触及该区域，应用的测试也会确认更新只写入固件文件自身的扇区。

## 🔬 如何构建与测试

每次改动都要通过全部自动检查。每次发布还会再加上更多检查。完整列表及每项检查对应的文件：[docs/QUALITY.md](docs/QUALITY.md)（英文）。

**每次改动**

- **应用：** 1,600 多个测试（单元、集成和安全测试），在 Linux、Windows 和 macOS 上运行，另有类型检查、ESLint 和 Prettier。
- **固件：** 185 个在 PC 上运行的固件逻辑测试，启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；以及五种固件构建（Pico 2 W、Pico W、调试版和两个变体）。如果时间关键的代码离开了 RAM，或闪存设置发生变化，构建就会失败。
- **应用的规则检查：** Electron 安全设置、禁止不安全的 HTML、只有一套主题系统且没有散落的颜色、没有未给出结论的重复代码或重复文件。
- **书面结论：** 每个会等待或位于热路径上的循环、每条错误路径、每个缓存，以及代码占用的每个资源（定时器、监听器、设备）都带有写明结论的注释。新增的若没有结论，检查就会失败。
- **安全扫描：** Semgrep、Gitleaks 和 npm audit，并且每周运行。

**每个开发版**

- **界面检查器：** 一份使用模拟板子的独立应用副本，以两种窗口尺寸、7 种语言打开每个页面。出现文字被截断、对比度低于 WCAG AA 或控件没有名称时即失败。
- **资源预算** 在 Linux 和 Windows 11 虚拟机中测量。
- 在 Windows 11 虚拟机中测试已安装的应用（**Windows**），并在 GitHub Actions 的真实 Mac 上进行 **macOS** 冒烟测试（像用户一样安装、启动并操作）。

**稳定版发布之前**

- **完整界面检查：** 每个屏幕和面板、7 种语言、两种尺寸、每种主题和样式、两种控制器型号的每种配色。
- **长时间预算测量：** 包括 30 分钟的内存增长持续测试，在 Linux 和 Windows 上进行。
- **硬件检查：** 在真实的板子和 DualSense 上检查实时画面、触觉反馈、灯条、按键分配、槽位、扳机、音频计数器、重启和 BOOTSEL、首次设置、更新程序以及游戏。
- **日常使用：** 开发者本人在日常使用该版本后签字确认。

**效率**

- **不达标就不发布的预算：** 静止页面每秒最多 10 帧；显示且空闲时低于一个 CPU 核心的 2%；隐藏在托盘时低于 0.5%；内存最多 275 MB；30 分钟内增长少于 5 MB。超出预算就修复，不提高上限。最新数据：[BUDGETS.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md)。
- **GPU 渲染：** 效果由显卡绘制；窗口隐藏时不绘制任何内容。
- **固件：** 音频路径在 RAM 中运行，应用会显示固件各部分所用的时间。

**安全与信任**

- **签名更新源**（Ed25519）和经过 SHA-256 校验的下载，见上文。
- **可验证的下载：** 每个发布文件都有构建证明（attestation）。`gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition` 可以证明它是由本仓库的发布工作流从对应标签构建的。
- **加固的应用：** 沙盒化的窗口、严格的 Content Security Policy、来自窗口的每条消息都会检查发送者并校验内容、USB 访问仅限板子和 Pico 引导程序。依赖使用固定版本，并且只选发布至少 2 周的版本。审计报告：[SECURITY_AUDIT.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md)。
- **对游戏来说就是真正的 DualSense：** 板子的 USB 描述符与索尼的逐字节相同（289 字节），配置接口是独立的，游戏永远看不到它。

**无障碍与语言**

- **7 种语言，完整：** 任何语言缺少字符串，测试就会失败；控制器功能在每种语言中都使用索尼的官方名称。
- **清晰且有名称：** 界面检查器在每种语言下检查对比度和控件名称；文字在每种尺寸下都必须放得下。
- **随你所需：** 色觉模式、减少动态效果，以及每个弹出菜单都支持键盘操作。

---

## 概述

本项目让 Raspberry Pi Pico2W 成为 DualSense 控制器的蓝牙桥接器，实现无线连接并增强触觉支持。

## 功能特点

**核心桥接（来自上游）：**

- 通过 Pico2W 完整连接 DualSense
- HD 触觉（高级振动反馈）
- 无线蓝牙桥接
- 通过麦克风音量调节触觉增益
- 可配置的 LED 与断连行为

**OLED Edition 新增：**

- **在 Linux/Proton 的 PC 游戏中通过 Pico 使用原生 DualSense 自适应扳机。** 原生支持 DualSense 的游戏会经由 Pico 无线驱动控制器的自适应扳机，**与有线直连的 DualSense 1:1 一致**。需要带有 Wine `winebus.sys` #9034 修复的 Proton（Wine 11 / 当前的 Proton-GE），并关闭 Steam Input —— **不需要启动选项**（Wine 11 默认启用 hidraw 原生路径）。**Steam 和 Heroic**（Epic/GOG）都可用。固件让 Pico 与真正的 DS5 逐字节无法区分，游戏因此接受它（完整说明见 [CHANGELOG.md](./CHANGELOG.md)）。代价：此固件上浏览器的 Config/Remap 标签页被停用（见上方说明）。
- 可选的 Pico-OLED-1.3 状态显示屏，含 **11 个屏幕**（状态、配对槽、光条、扳机测试、陀螺仪倾斜、触摸板、诊断、CPU/时钟、RSSI、VU 表、设置）
- **在 Pico 里校准（v0.9）** —— 操作杆中心偏移、盲区和推满半径、扳机行程和陀螺仪零点，按配对槽位保存，作用于游戏收到的数据（从不写入控制器）。通过应用的引导式校准设置。
- **左右分别的振动强度（v0.9）** —— 左右两侧分别设置，触觉反馈和传统振动都适用。
- **灵敏度曲线（v0.9.5）** —— 每个操作杆一条曲线（线性、精确、稳定、快速、数字，或四个自定义点），在 Pico 里于校准之后应用，因此对所有游戏生效；游戏的配置可以在运行时切换为自己的曲线。
- **DualSense Edge 支持** —— 通过控制器连接时发送的固件报告区分 Edge 和 DualSense；它的 Fn键和背面键可以像其他按键一样配置，OLED 状态屏也会显示它们。它的自定义配置文件可在索尼的 PlayStation Accessories 应用中显示和保存（上游 awalol 8a2f576；控制器连接后应用可能需要几秒钟）。在 DS5 模式下，应用不再提示一个会失败的固件更新（上游 awalol ae83907）。
- **控制器信息（v0.9.5）** —— 应用可以通过 Pico 读取控制器自己的固件版本、电池电压和累计计数（只读；控制器序列号在应用读取之前即被清除）。
- **分阶段计时（v0.9.5）** —— Pico 为自己的每部分工作计时（蓝牙、USB、音频、输入、OLED、core1 的扬声器和麦克风帧），供应用的耗时卡片使用。
- **OLED 屏幕镜像（v0.9）** —— 应用逐像素显示开发板的屏幕，装不装 OLED 插件都可以。
- **按键配置** —— 把 16 个数字控件（动作键、方向键、L1/R1/L2/R2、操作杆按下、创建键和选项键）以及 DualSense Edge 的 Fn键和背面键中的任意一个重新分配为其他按键。保存在 Pico 上，并在主机看到报告之前应用，因此在**任何游戏和操作系统中都无需任何主机端软件**即可生效；默认为恒等映射（不重映射）。可在[配置应用](#️-快速上手下载应用)的**按键配置**页面中可视化编辑，或通过 `scripts/remap_test.py` 无界面编辑。保存在专属闪存扇区，重启后保留。
- **电脑睡眠与唤醒（上游 awalol）** —— 电脑睡眠 3 秒后控制器关闭（默认开启），还可选择让 PS键唤醒电脑（默认关闭：没有连接控制器时，Pico 会显示为一个只用于唤醒的小键盘 "DS5 Dongle Wake Keyboard"，电脑必须允许它唤醒）。连接控制器后，Pico 与以前完全一样就是 DualSense。两项都在应用和 OLED 设置菜单中。唤醒需要电脑在睡眠时保持 USB 供电。
- **扳机效果强度、扬声器增强、锁定音量、麦克风与扬声器选择、状态引脚（上游的设置，协议 1.13）** —— 分 10 级减弱自适应扳机（默认为游戏自己的强度）、控制器功放的前级增益、阻止游戏更改控制器音量、麦克风选择自动 / 内置 / 耳机、电脑音频输出到自动 / 扬声器 / 耳机 / 关，以及一个显示控制器连接状态的 GPIO。静音键指示灯跟随麦克风静音，电脑的静音开关和麦克风音量会同步到控制器。全新配置的轮询率为 500 Hz。
- **4 槽持久化多控制器配对** —— 可绑定至多四个 DualSense，在 OLED 上切换，开机时自动重连槽 0
- **光条调色器**，含 4 个用户收藏槽 + 呼吸 / 彩虹 / 渐变特效预设
- **持久化设置菜单**，涵盖 8 个固件配置字段（触觉增益、扬声器音量、轮询率等），并提供按住确认的“重置”与“清除全部槽”操作
- **OLED 空闲省电阶梯** —— 手动亮度循环（长按 KEY1）、空闲 2 分钟自动深度变暗并显示一个呼吸小点、空闲 15 分钟完全熄屏。按键、控制器配对或输入时立即唤醒。是真正的防烧屏，而不只是调对比度。
- **软重启**，无需拔 USB：按住 DS5 的 `PS + Mute`（无屏幕也可用），或在 OLED 插件上**同时按住 KEY0 + KEY1 1 秒**（取代了旧的 KEY0 双击手势，快速翻页时它很容易误触发）
- **干净的控制器扬声器与麦克风** —— core1 的音频路径（Opus、重采样器、内存操作）在 RAM 中运行，音频以上游的打包 `0x39` 报告发送，麦克风在 core1 解码并按主机的 USB 时钟输出。这消除了长期存在的扬声器爆音和开麦卡顿（见 [CHANGELOG.md](./CHANGELOG.md)）。**归功于 [awalol](https://github.com/awalol/DS5Dongle)**：这是上游的音频工作，移植到了这里。
- **核心桥接的审计修复** —— 修复音频路径中关键的栈溢出（解决长期存在的“音频卡顿”）、安全加固、看门狗、HID/L2CAP 边界处的长度校验（见 [CHANGELOG.md](./CHANGELOG.md)）

## 🐧 Linux：原生自适应扳机（Proton）

在 Linux 上让游戏**通过 Pico 驱动控制器的自适应扳机**，需要带有 Wine `winebus.sys` **#9034** 修复的 Proton。官方 **GE-Proton 11** 版本现在已包含它。这里提供的版本 **`GE-Proton11-7-DualSense`**（见[它的发布页](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense)）是官方 `GE-Proton11-7` 加上一个额外的 `winebus.sys` 修复：DualSense 把序列号报告为冒号分隔的蓝牙 MAC，这让它的设备路径在 Windows 中无法打开，于是使用索尼 libScePad 的游戏无法访问扳机。此版本替换了这些字符，路径就能打开了。下面的扳机结果就是在这个版本上验证的。

> 原因：#9034 的 bug 会在同一 VID/PID 存在 hidraw 设备时屏蔽 SDL 手柄设备，于是手柄*除了游戏里*到处都能看到。修复让原生（支持扳机的）路径和正常工作的手柄可以共存。

**一次性设置**

1. **安装这个 Proton 版本。** 从[发布页](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense)下载 `GE-Proton11-7-DualSense.tar.gz` 并解压到：
   - **Steam：** `~/.steam/root/compatibilitytools.d/`
   - **Heroic：** `~/.config/heroic/tools/proton/`

   然后**完全重启 Steam**，让它识别新的兼容性工具。
2. **关闭 Steam Input**，让游戏直接与控制器通信 —— Steam → 游戏 **属性 → 控制器 → 禁用 Steam 输入**（或全局：设置 → 控制器 → 关闭 PlayStation 控制器支持）。

**每个游戏**

3. **强制使用该 Proton：** Steam → 右键点击游戏 → **属性 → 兼容性 → 强制使用特定 Steam Play 兼容性工具 → `GE-Proton11-7-DualSense`**。（Heroic：把游戏的 *Wine 版本* 设为它。）

**不需要启动选项** —— Wine 11 默认启用 hidraw 原生路径，所以 Pico 会自动交给游戏。直接启动即可 —— 原生自适应扳机通过 Pico 工作，与有线 DualSense 1:1 一致（已在 Cyberpunk 2077、Uncharted、Spider-Man Remastered、The Last of Us Part I、Avatar、Indiana Jones、Star Wars: Galactic Racer 上无需任何参数验证 —— 而且 **Steam 和 Heroic 都可以**）。游戏必须**原生支持 DualSense**（仅支持 XInput 的游戏只有震动，没有自适应扳机 —— 这是引擎的限制，不是 Pico 的问题）。在新的 Proton 下首次启动时会做区域/前缀设置，可能显示“未响应”一分钟 —— 那是在设置，不是崩溃；让它完成。

**让控制器的音频保持默认配置。** GE-Proton 11 自己处理 DualSense 的触觉，并要求声卡使用标准的 **HiFi** 配置。强制 DualSense 使用 **pro-audio** 的 WirePlumber/PulseAudio 规则（旧版 Proton 的常见技巧）会让 Avatar 发送零力度的扳机并丢失 HD 触觉（已验证；其他索尼移植的游戏很可能也一样）。删除这样的规则，或用 `pactl list cards` 确认 DualSense 显示 `Active Profile: HiFi (...)`。

### Heroic（Epic / GOG）—— 同样的方法，另加两个要求

原生扳机在 Heroic 上同样可用，但非 Steam 启动器对两件主机端的事很敏感，它们会悄悄抢走控制器：

1. **完全退出 Steam** —— 后台运行的 Steam 会从非 Steam 游戏那里抢走 DualSense（或者关闭 设置 → 控制器 → PlayStation 控制器支持）。
2. **不要全局设置 `PROTON_PREFER_SDL`** —— 这个环境变量会强制走 SDL/Xbox 路径并*屏蔽*原生扳机。只在单个游戏上使用它，作为有意选择的通用手柄后备方案。

**启动选项参考**

| 变量 | 值 | 用途 |
|---|---|---|
| `PROTON_ENABLE_HIDRAW` | `0x054c/0x0ce6` | *旧式 / 可选 —— 在 Wine 11 上**不需要***，Wine 11 默认启用 hidraw 原生路径。设置了也无害。只对 hidraw 需要手动开启的旧版 Proton 有意义（但那些版本也没有 #9034 修复，所以扳机在那里本来就不工作）。 |
| `PROTON_PREFER_SDL` | `1` | *替代方案，仅输入。* 强制走 SDL 手柄路径 —— 输入和震动正常，但作为通用/Xbox 手柄（**没有扳机**）。只在游戏缺少原生 DualSense 支持、且 `ENABLE_HIDRAW` 无效时使用。对需要扳机的游戏**不要**和 `ENABLE_HIDRAW` 一起用。需要一个支持它的 Proton：已发布的 **GE-Proton11-7-DualSense** 会忽略它，所以在下一个 DualSense 版本发布之前，请用标准 GE-Proton 运行仅支持 XInput 的游戏。 |
| `PROTON_LOG` | `+hid` | *仅用于诊断*（不用于正常游戏）。写出一份 HID 枚举跟踪 —— Steam 把它放在 `~/steam-<appid>.log`；Heroic 把 `PROTON_LOG_DIR` 设为你的主目录，所以它在 `~/steam-0.log`。可用于确认游戏打开了原生 HID 路径。 |

> **Heroic 与 Steam：** 原生扳机在**两者**上都可用 —— 而且在 Wine 11 上都不需要启动选项。Steam：设置运行器 + 关闭 Steam Input。Heroic：设置运行器、**完全退出 Steam**（后台的 Steam 会从非 Steam 游戏那里抢走手柄），并保持 `PROTON_PREFER_SDL` 关闭。无论哪种，游戏都必须原生支持 DualSense —— 仅支持 XInput 的游戏（例如 Ghostrunner、Control）在任何系统上都只有震动，没有自适应扳机。这些游戏需要 `PROTON_PREFER_SDL=1` 和支持它的 Proton 版本：已发布的 **GE-Proton11-7-DualSense** 不支持，所以在下一个 DualSense 版本发布之前，请为它们使用标准 GE-Proton。

## 硬件

### 必需

| 物品 | 说明 | 大致价格 |
|---|---|---|
| **Raspberry Pi Pico 2 W** | 搭载 RP2350 MCU，板载 CYW43 蓝牙/WiFi。[官方产品页](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) | 约 $7 USD |
| **索尼 DualSense 控制器** | 任何 PS5 DualSense（VID `054C:0CE6`）或 DualSense Edge（`054C:0DF2`）。 | — |
| **USB-C 线缆** | 把 Pico 2 W 连接到主机电脑。 | — |

### 可选（强烈推荐）

| 物品 | 说明 | 大致价格 |
|---|---|---|
| **Waveshare Pico-OLED-1.3** | 128×64 SH1107 OLED 插件板（SKU HIPI1798）。直接插到 Pico 2 W 排针上。固件检测到时自动驱动，没有时安静地什么都不做。[产品页](https://www.waveshare.com/pico-oled-1.3.htm) · [Wiki](https://www.waveshare.com/wiki/Pico-OLED-1.3) | 约 $6 USD |
| **小散热片**（用于 RP2350） | 不需要：固件让 RP2350 以其额定的 150 MHz 和默认电压运行（见[性能 / 时钟](#性能--时钟)）。已经装了也无妨。 | $1–3 USD |

### 哪里购买

Pico 2 W 和 Waveshare Pico-OLED-1.3 在全球都很容易买到：

- **Adafruit**、**Pimoroni**、**The Pi Hut**、**DigiKey**、**Mouser** —— 主要电子元件分销商（美 / 欧）
- **Waveshare 自营商店** 购买 OLED 插件
- 各地区 **Amazon** 店面 —— 搜索 `Raspberry Pi Pico 2 W` 和 `Waveshare Pico-OLED-1.3`（或 SKU `HIPI1798`）
- **AliExpress（速卖通）** —— 原装 Waveshare 与 Pico 货源以及仿品；注意查看卖家评分

## 快速开始

### 刷写固件

[应用](#️-快速上手下载应用)会替你完成。如需手动刷写（或使用 Pico W 版本，它是 CI 构建产物）：

1. 按住 Pico2W 上的 BOOTSEL 按钮
2. 通过 USB 把 Pico2W 连接到电脑
3. 设备会挂载为 USB 存储设备
4. 把 .uf2 固件文件（见 [Releases](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases)）拖放到该设备上

### 配对控制器

1. 让 DualSense 控制器进入蓝牙配对模式
2. 等待 Pico2W 检测并连接
3. 连接后，设备会出现在主机系统中

## 配置

有四种方式配置固件：

**配置应用（推荐）：** 适用于 Linux 和 Windows 的 [DS5 Dongle OLED Edition Configurator](#️-快速上手下载应用)（也包含 macOS 版本，已有用户报告在 macOS 27.0.1 上可用；游戏页面在 Linux 上列出 Steam、Heroic 和 Lutris 游戏，在 Windows 上列出 Steam、GOG Galaxy 和 Epic 游戏，其中的 Proton 设置仅限 Linux）。它会自己找到 Pico，修改即时生效，一键保存到 Pico 的闪存。需要 v0.8 及以上固件。

**设备端（装有 OLED 插件）：** 使用屏幕上的 **Settings** 菜单（第 11 个屏幕）。方向键 ▲▼ 移动选择，▶◀ 调整数值，△ 保存到闪存。

**终端 CLI（任何系统、任何浏览器）：** 安装 hidapi，然后使用 `scripts/set_ds5.py`：

```bash
pip install hidapi
scripts/set_ds5.py                            # 显示当前配置
scripts/set_ds5.py --auto-haptics fallback    # 修改某字段并持久化到闪存
scripts/set_ds5.py --speaker-volume -10 --haptics-gain 1.5
scripts/set_ds5.py --slot 2                   # 切换当前的多槽配对
scripts/set_ds5.py --version                  # 固件版本
scripts/set_ds5.py --rssi                     # 实时蓝牙信号，相对理想范围的 dB（0 = 理想）
scripts/set_ds5.py --help                     # 完整参数列表
```

该脚本通过 USB HID 特性报告 `0xF6`/`0xF7`/`0xF8`/`0xF9` 与固件通信。自 v0.6.12 起这些报告不再在 HID 描述符中声明（游戏要求描述符与真正的 DualSense 逐字节一致），因此该脚本只在 Linux 上可用；其他系统请使用配置应用。移植自 [loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)，并为本分支的 `current_slot` 字段做了扩展。

**DualSense 控制器按键（旧式后备方案，无 OLED、无 CLI）：**

### 麦克风音量

控制触觉增益倍率。范围：`[1.0 – 2.0]`。

### 扬声器静音

关闭 LED 连接指示。控制器重新连接后生效。

### 麦克风静音

关闭静默断连行为。

## 注意事项

只有在控制器连接之后，Pico 设备才会对系统可见

某些行为需要重新连接一次才会生效

### 低电量 LED 指示

当所连 DualSense 报告电量等于或低于 10%（且未在充电）时，Pico 板载 LED 会从常亮切换为 1 Hz 闪烁，让你一眼看到警告。一旦控制器插上充电或报告电量回升，LED 恢复常亮。即使设置了 `disable_pico_led`，闪烁也会触发 —— 该警告被视为关键，会覆盖关闭 LED 的偏好；电量恢复或控制器开始充电后，LED 回到禁用（熄灭）状态。

如需在编译时关闭此功能，用 `-DENABLE_BATT_LED=OFF` 配置。默认为 ON。

## 蓝牙 DualSense 麦克风

**DualSense 的内置麦克风可以通过 Pico 的蓝牙配对使用**（自 v0.6.8 起）。控制器把麦克风以 Opus 音频流传输；Pico 解码后作为标准的 DualSense USB 录音设备提供给主机，因此任何应用（Discord、OBS、游戏内语音）都能像普通麦克风一样使用它。

这一度被认为不可能 —— 本分支早期版本曾把它记录为索尼固件的硬性限制（Linux 的 `hid-playstation` 内核驱动至今仍不支持）。结果发现它只取决于 Pico 外发音频报告中的一个使能位。**完全归功于 [awalol](https://github.com/awalol/DS5Dongle)（上游）发现了它。** 更正后的调查记录见 [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)。

**使用方法：**

- **默认开启。** 配对控制器后一两秒内麦克风便开始传输 —— 不需要游戏音频（Pico 会自己保持音频流）。
- **在主机上调高录音音量** —— 它默认很低。在 Linux 上，用 `arecord -l` 找到声卡，然后例如 `amixer -c <DualSense 声卡> sset 'Headset' 90%`。用 `scripts/mic_diag.sh capture` 验证录音。
- **关闭它以节省控制器电量** —— OLED **Settings → BT Mic**，或[配置应用](#️-快速上手下载应用)中的 **内置麦克风** 开关。常开的麦克风会让 DS5 的音频子系统保持唤醒，明显加快耗电，所以不用语音时请关闭。

**注意点：**

- 麦克风音频为**单声道**（解码为单声道，在立体声录音端点上复制）。
- 使用中途关闭会立即停止发往主机的数据，但控制器会继续传输直到下次重新连接（目前没有已知的“停止”命令）；在关闭状态下全新连接则永远不会启用。
- OLED **Diagnostics** 屏幕的 `Mic in:` 计数器在麦克风传输时约为 ~100/s —— 是确认它在工作的快捷方式。
- **丢包隐藏：** 丢失的麦克风帧（蓝牙链路弱、距离远、干扰）会用 Opus PLC 隐藏，让语音保持连续而不咔嗒/中断，代价是少量抖动缓冲延迟（~30 ms）。Diag 屏幕的 `Mic PLC:` 计数器只在隐藏帧时增长 —— 实际上就是一个实时的链路质量指示。

## 已知问题

- 在 Linux + Steam 上，HD 触觉未必在每个游戏中都触发；这是游戏方面的问题（部分游戏只在 Windows 专用 API 下发送 HD 触觉音频）。在 Spider-Man Remastered 中测试可用；在 Ghost of Tsushima 中没有送达 —— 同一固件、同一控制器。
- **USB 3.0 端口可能干扰配对** —— 控制器可能卡在常亮的琥珀/黄色光条上始终连不上，而同一块开发板在 USB 2.0 端口上工作正常。这是射频干扰，不是固件 bug；见下文 [USB 3.0 端口与蓝牙干扰](#usb-30-端口与蓝牙干扰)。（自 v0.6.8 起，固件会自动重试卡住的连接，而不是一直挂着，能恢复许多 —— 但不是全部 —— 边缘情况。）
- **Windows：应用打不开 Pico** —— 发生在以前用 USB 插过真正 DualSense 的电脑上（Pico 与它使用相同的 USB ID，而 Windows 记住了这些 ID “不需要驱动”）。从应用 0.9.2 起，设置界面提供 **修复 USB 驱动**：一次管理员确认，然后重新插拔。它运行的完整 PowerShell 逐行列在[应用的 README](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web#repair-usb-driver-exactly-what-it-runs) 中。

## USB 3.0 端口与蓝牙干扰

如果 Pico 在某个端口能用、在另一个不能用，**请先试试 USB 2.0 端口。** USB 3.0 端口（尤其是 USB 3.0 延长线）会发出以 2.4 GHz 附近为中心的宽带射频噪声 —— 正是 Pico 的蓝牙无线电与控制器通信所用的频段。这是业界有充分记录的问题（Intel，*"USB 3.0 Radio Frequency Interference Impact on 2.4 GHz Wireless Devices"*），并非本固件特有。噪声会降低 Pico 蓝牙接收的灵敏度，于是控制器可以开始连接（琥珀色光条），但链路太嘈杂无法完成 —— 卡在黄色上。

缓解措施，大致按效果排序：

1. **把 Pico 插到 USB 2.0 端口**（往往是最简单的修复 —— 许多主板/机箱两种都有）。
2. **用一根短的 USB 2.0 延长线**，让 Pico 离 USB 3.0 端口 / 金属机箱几厘米远，改善与控制器之间的视线。尤其要避免 USB 3.0 延长线。
3. **使用插在 USB 3.0 端口上的有源 USB 2.0 集线器** —— 集线器会把链路降速并增加距离。
4. **在靠近 Pico 的线缆上夹一个磁环。**
5. **配对时让控制器更靠近 Pico / 与它之间没有遮挡。**

固件会自己持续重试卡住的连接，所以光条变成琥珀色后让它继续插着等约 10–20 秒，也许无需重新插拔就能恢复。

## 性能 / 时钟

**这一项你什么都不用做。** Pico 2 W 使用与上游 awalol/DS5Dongle v0.7.0 相同的设置：

- **时钟：150 MHz**，RP2350 的额定时钟（也就是它启动时的时钟）
- **核心电压：1.10 V**，默认值。固件在启动时设置它，因为 RP2350 在断电之前会保留旧固件设置的电压。
- **蓝牙芯片总线（CYW43 PIO SPI）：分频 2**
- **闪存：75 MHz**（分频 2），由固件在启动时自行设置（`PICO_EMBED_XIP_SETUP`）。保持 bootrom 的设置曾导致部分开发板上 core1 崩溃（[issue #15](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/15)）。

旧版本超频到 320 MHz、1.20 V，这一设置继承自上游。上游在音频路径移入 RAM 后改回 150 MHz；本分支在音频路径和 core0 的蓝牙/USB 热路径都运行于 RAM 之后也这样做了。1.20 V 高于 RP2350 的 1.16 V 工作上限，150 MHz 下也不需要散热片。

在一个版本内，自行编译时仍可把旧的超频作为后备：`-DDS5_CLOCK_KHZ=320000`（1.20 V，蓝牙总线分频 5，闪存分频 4）。没有内嵌闪存设置或闪存高于 100 MHz 的 Pico 2 W 配置会被构建拒绝。Pico W 构建不变（200 MHz）。

## 编译说明

从源码编译本项目：

1. 安装 Pico SDK **2.3.0**。编译使用 `pico_sdk_import.cmake`。CI 和正式版本使用 ARM 官方 GNU 工具链 **15.2.Rel1**（`arm-none-eabi`）构建；发行版自带的 `gcc-arm-none-eabi` 通常也能用，但 15.2.Rel1 能得到与正式版本相同的二进制。确切的版本（SDK、TinyUSB 提交、工具链下载地址）见 `tools/toolchain.env`。如果工具链不在 `PATH` 中，请把 `PICO_TOOLCHAIN_PATH` 设为解压后的工具链目录。
2. **在 Pico SDK 内把 TinyUSB 固定到 `tools/toolchain.env` 中的提交**（`$PICO_SDK_PATH/lib/tinyusb`）：即 TinyUSB 0.21 加上之后的修复，与上游一致。Pico SDK 2.3.0 捆绑的仍是 0.18.0，它没有本项目 `tusb_config.h` 使用的 `TUD_AUDIO_EP_SIZE` 四参数形式；而且除 0.21 以外的 TinyUSB 都会被构建拒绝：
   ```bash
   cd "$PICO_SDK_PATH/lib/tinyusb"
   git fetch --depth 1 origin 2d56dc533e45e4e91b15e93fdab5e22e964f328d
   git checkout --detach FETCH_HEAD
   ```
3. 用 CMake + Ninja（或 Make）配置并编译：
   ```bash
   cd /path/to/DS5Dongle
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
   cmake --build build --target ds5-bridge
   ```
4. UF2 会生成在 `build/ds5-bridge-oled.uf2`。照常用 BOOTSEL 刷写。

值得了解的编译选项：

- `-DENABLE_BATT_LED=ON`（默认）—— DS5 电量低时闪烁 Pico LED。
- `-DENABLE_SERIAL=ON` —— 把 printf 输出到 USB CDC 用于调试（默认 OFF；正式构建不占用 UART）。
- `-DPICO_W_BUILD=ON` —— 为初代 Pico W 编译（去掉音频、降低时钟）。默认目标是 Pico 2 W。

## 诊断与调试工具

排查桥接问题有两种方式 —— 设备端通过 OLED Diagnostics 屏幕，主机端通过 `scripts/mic_diag.sh`（Linux）。主机端更快：无需切换屏幕、无需刷写，可在控制器正在使用时运行。

```
# 一次性快照 —— Pico 在 USB 上吗？ALSA 枚举到它了吗？
# 录音流在运行吗？当前有控制器配对吗？
scripts/mic_diag.sh status

# 对麦克风 IN 端点做 3 秒 arecord —— 报告峰值 / RMS / 非零
# 计数，以便区分“流是静音的”和“流在产生音频”。
scripts/mic_diag.sh capture 3

# 同 `status`，但循环运行，只在状态变化时打印。可用于
# 捕捉配对完成或音频流打开 / 关闭的确切时刻。
scripts/mic_diag.sh watch

# 实时读取固件的 0xFD 厂商特性报告（经由 /dev/hidraw）：
# 蓝牙输入计数与速率、最近见到的非 0x31 ID、字节前缀，以及
# 扳机流计数器（收到的主机 0x02 / 置了 AllowTriggerFFB 的 /
# 转发到蓝牙的）。bt-trace 会给出结论 —— “主机驱动没有发送
# 扳机 Allow 位”还是“已转发但控制器没有动作” —— 这原本
# 需要 USB 协议分析仪才能判断。
scripts/mic_diag.sh bt-trace
```

最初是为排查被搁置的 DS5 蓝牙麦克风调查而写的（见 [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)）。`0xFD` 特性报告和 `bt-trace` 解码器现在还带有为 [issue #3](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/3)（部分游戏缺少自适应扳机阻力）新增的扳机流计数器。

## OLED 显示插件（可选）

如果你把 [Waveshare Pico-OLED-1.3](#硬件) 插到 Pico2W 的排针上，固件会自动把它作为实时状态显示屏驱动。无需任何配置 —— 没有 OLED 时固件安静地什么都不做。

<img src="./assets/oled/oled-status-live.gif" alt="OLED 上实时运行的状态屏幕" width="512">

真实板子上实时运行的状态屏幕，通过 OLED 镜像逐像素捕获。下面每个屏幕的图片也都是从板子上实际捕获的。

### 开机画面（上电后 1.5 秒）

空白屏幕上居中显示固件版本 1.5 秒，然后跳到状态屏幕。

### 11 个屏幕，用插件上的 KEY0 循环

循环顺序：**Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → CPU/Clock → BT Signal → VU Meters → Settings →** 回到开头。**短按 KEY0 前进；短按 KEY1 后退** —— 在*每个*屏幕上都如此。各屏幕内的操作（循环扳机预设、循环光条模式、移动 Settings 光标、切换槽）都放在 **DualSense 控制器按键**上，绝不放在 KEY0/KEY1 上，因此 OLED 插件上这两个物理按键始终表示同一含义。

每个屏幕还会在左上边缘（KEY0 旁）画 **`>`**，在左下边缘（KEY1 旁）画 **`<`**，让屏幕上的标记与按键在位置上对应。

#### 1. 状态

连接状态、已配对 DualSense 的蓝牙地址、带条形的电量百分比（`+` 充电中 / `*` 已充满 / `!` 错误）、实时操作杆位置（按下 **L3 / R3** 时，对应的操作杆框会反色闪烁 —— 白框黑点）、方向键、动作键（△ ◯ ✕ □）、L1/R1，以及 L2/R2 模拟扳机填充条。连接指示和电量使用小像素图标。充电时，电量旁会显示到 100% 的预计时间 `~Nm`。

<img src="./assets/oled/oled_sc01.png" alt="OLED 上的状态屏幕" width="512">

#### 2. 配对槽

持久化的 4 槽多控制器配对。浏览已保存的控制器、切换当前槽，或清除单个槽。`>` 是光标，`*` 标记当前使用的槽。

<img src="./assets/oled/oled_sc02.png" alt="OLED 上的配对槽屏幕" width="512">

- **方向键 ▲▼** —— 在槽 0–3 之间移动光标
- **△** —— 切换到光标所在的槽（断开当前控制器，重新连接到该槽保存的控制器）
- **□ 按住 1.5 秒** —— 清除光标所在的槽（删除 bd_addr + BTstack 链路密钥）
- 当前槽会被保存；Pico 下次开机时重新连接到它

#### 3. 光条调色器

在各个轴上倾斜控制器来调出 R / G / B；固件以 10 Hz 把得到的颜色发送到 DualSense 真正的光条上，所以光条本身就是预览（OLED 是单色的）。

<img src="./assets/oled/oled_sc03.png" alt="OLED 上的光条调色器" width="512">

- 在控制器上按 **△ ◯ ✕ □** 把当前颜色**保存**到收藏槽 0 / 1 / 2 / 3
- 在控制器上按 **R1** 循环模式标签：`[LIVE]` → `[FAV0]` → `[FAV1]` → `[FAV2]` → `[FAV3]` → 特效（Breathing / Rainbow / Fade）→ `[HOST]` → 回到 `[LIVE]`
- `[HOST]`（默认）把光条交给电脑 / 游戏；Pico 不设置颜色。其他任何模式都保持 Pico 的颜色，不管游戏发送什么
- 默认收藏：红、绿、蓝、白

#### 4. 扳机测试

在控制器上按 **△** 循环七种自适应扳机效果，同时作用于 L2 和 R2。扣动扳机来感受效果。

<img src="./assets/oled/oled_sc04.png" alt="OLED 上的扳机测试屏幕" width="512">

循环顺序：**Off → Feedback → Weapon → Vibration → Bow → Gallop → Machine Gun → Off …**（关 → 反馈 → 武器 → 振动 → 弓 → 疾驰 → 机枪）。效果参数依据 [dualsensectl](https://github.com/nowrep/dualsensectl) 的逆向工程按位打包，全部为最大强度。

#### 5. 陀螺仪倾斜

实时 X/Y/Z 加速度计数值，配 40×40 十字准线框。圆点实时跟随控制器的倾斜，并在控制器**平放时居中** —— 它使用控制器自带的逐台出厂 IMU 校准（从特性报告 `0x05` 解析），因此每个控制器的静止位置和增益都准确。向左/右、向前/后倾斜时，圆点会朝相应方向移动。

<img src="./assets/oled/oled_sc05.png" alt="OLED 上的陀螺仪倾斜屏幕" width="512">

#### 6. 触摸板

触摸板表面的实时画面。当前手指位置显示圆点；手指触碰 / 离开时计数随之更新。

<img src="./assets/oled/oled_sc06.png" alt="OLED 上的触摸板屏幕" width="512">

#### 7. 诊断

可滚动的实时计数器列表 —— 运行时间、蓝牙状态、主机 → 蓝牙的扳机数据流（`host02` / `trig` / `tx`）、蓝牙 0x31 输入速率、USB 音频帧/秒、蓝牙 0x32 包/秒，底部还有已搁置的麦克风调查计数器。控制器方向键 ▲/▼ 滚动；右边缘的小 `^` / `v` 表示“上方/下方还有更多”。只读，所以没有光标。不用 UART 线就能确认桥接在传输数据。

同样的计数器也通过 HID 特性报告 `0xFD` 提供给主机端工具 —— 见下文 `scripts/mic_diag.sh bt-trace`。

<img src="./assets/oled/oled_sc07.png" alt="OLED 上的诊断屏幕" width="512">

#### 8. CPU / 时钟

实时 RP2350 指标：设定的（`Set`）和实际运行的（`Real`）系统时钟（以晶振为参考测量）、从稳压器读回的核心电压，以及片上温度（256 次采样平均 + 慢速 EMA，让数值反映真实芯片温度而不是 ADC 噪声）。

<img src="./assets/oled/oled_sc08.png" alt="OLED 上的 CPU / 时钟诊断屏幕" width="512">

同样的遥测数据也通过 HID 特性报告 `0xFC` 提供给工具。

#### 9. 蓝牙信号

当前链路的实时蓝牙信号强度，单位为相对理想范围的 dB（0 = 理想，显示 *in range*；越负越弱），并带条形（−30 dB 为空，0 dB 为满）和与应用相同的文字等级（Excellent / Good / Fair / Weak，即极佳 / 良好 / 一般 / 较弱）。在第一次读数之前或读数失败时，显示 `Signal: --` 和 `Link: unknown`。

<img src="./assets/oled/oled_sc09.png" alt="OLED 上的蓝牙信号屏幕" width="512">

#### 10. VU 表

扬声器和触觉音频路径的实时峰值表。不把控制器插到主机上也能确认音频路由。

<img src="./assets/oled/oled_sc10.png" alt="OLED 上的 VU 表屏幕" width="512">

#### 11. 设置

持久化配置编辑器。方向键 ▲▼ 移动选择，▶◀ 调整数值，△ 保存到闪存。包含固件配置字段（触觉增益、扬声器音量、空闲超时、轮询率）、音频自动触感控件，以及两个按住确认的操作：

`Hap Left` 和 `Hap Right` 分别调整左右握把强度，按下 △ 应用并保存修改。

<img src="./assets/oled/oled_sc11.png" alt="OLED 上的设置屏幕" width="512">

- **AutoHap Off / Fallback / Mix / Replace** —— 选择音频自动触感模式。默认 `Off`：触觉只来自游戏。`Fallback` 只在游戏不发送原生触觉数据时（例如 Linux 上的 Ghost of Tsushima）才产生派生的震动 —— 注意它也会跟着音乐和桌面音频震动；确实发送原生触觉的游戏（Spider-Man Remastered）原样通过。`Mix` 在原生之上叠加派生，`Replace` 完全忽略原生。
- **AH Gain N%** —— 派生信号增益，0–200 %，每步 10 %。默认 100 %。
- **AH LP 80/160/250/400 Hz** —— 包络跟随之前作用于扬声器音频的低通截止频率。越低越偏重低音，越高越有临场感。默认 160 Hz。
- **TrigEff game / -1 … -10** —— 扳机效果强度：分 10 级减弱游戏的自适应扳机阻力。默认 `game`（0）：游戏自己的强度，报告原样通过。
- **SpkBoost game / 1 … 7** —— 扬声器增强：控制器功放的前级增益。默认 `game`（0）：交给游戏决定。与 `Spk Vol` 是两个设置。
- **VolLock on / off** —— 锁定音量：游戏不能更改控制器的扬声器、耳机或麦克风音量、静音或静音键指示灯；电脑自己的音量和静音仍然生效。默认 off。
- **SPK Out Auto / SPK / Hset / off** —— 音频输出：电脑的声音从哪里播放（Auto = 控制器上插了耳机时用耳机）。默认 Auto。
- **Mic In Auto / Int / Hset** —— 使用的麦克风：自动、内置麦克风或耳机麦克风。默认 Auto。`BT Mic` 仍是关闭麦克风的开关。
- **Sleep CtrlOff on / off** —— 电脑睡眠 3 秒后关闭控制器。默认 on。
- **PS Wakes PC on / off** —— 没有连接控制器时，Pico 显示为只用于唤醒的键盘（"DS5 Dongle Wake Keyboard"），这样 PS键可以唤醒电脑。默认 off。
- **Reset to defaults** —— 按住 △ 2 秒，恢复所有配置字段
- **Wipe all slots** —— 按住 △ 2 秒，删除全部 4 个已配对控制器 + 全部 BTstack 链路密钥

### 按键参考

OLED 插件上的两个物理按键**只用于导航**：

| 按键 | 动作 |
|---|---|
| **KEY0** 短按 | 下一屏（前进） |
| **KEY1** 短按 | 上一屏（后退） |
| **KEY1** 按住超过 1.5 秒 | 按住时立即切换一档亮度，松手不后退 |
| **KEY0 + KEY1** 同时按住 ≥ 1 秒 | `watchdog_reboot` —— 不拔 USB 的软重启 |

各屏幕内的状态变更（循环扳机预设、循环光条模式、在 Settings 中移动、切换槽、把颜色保存到收藏槽）全部在 **DualSense 控制器按键**上进行 —— 绝不在 KEY0 / KEY1 上 —— 因此这两个物理按键在每个屏幕上始终表示同一含义。哪个控制器按键做什么，见上文各屏幕的小节。

### 引脚定义（标准 Waveshare Pico HAT 布局）

| 功能 | GPIO |
|---|---|
| MOSI | 11 |
| SCK  | 10 |
| CS   | 9  |
| DC   | 8  |
| RST  | 12 |
| KEY0 | 15 |
| KEY1 | 17 |

### 软重启恢复

两种不拔 USB 就能重启 Pico 的方式 —— 配对卡住或想要一个干净状态时很方便：

- **OLED 上同时按住 KEY0 + KEY1 ≥ 1 秒** → `watchdog_reboot`。取代了早期版本的“KEY0 双击”手势，因为快速向前翻页总会误触双击计时。
- **按住 DualSense 的 `PS + Mute` 2 秒** → `watchdog_reboot`（装不装 OLED 都可用 —— 无屏幕时的后备方式）。

## 致谢

本分支的部分功能与设计思路借鉴自上游的其他分支，在此致谢：

- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)** —— OLED 状态栏中的像素图标（视觉方案）、Settings 屏幕 "Reset to defaults" 项使用的“按住以恢复出厂设置”交互（按住 △ 2 秒确认），以及新的 Slots 屏幕上的多槽持久化蓝牙配对系统（4 个已绑定控制器、方向键导航、△ 切换槽、按住 □ 清除槽，以及 Settings 菜单中的 "Wipe all slots"）。
- **[loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)** —— 独立地重新发现了上游 `3a31bd7` 破坏扬声器 / HD 触觉输出的回归（提交 `c7a8d3c`）；我们 `src/audio.cpp` 中的修复恢复了同样的 SetStateData 子报告。也是音频自动触感 DSP（单极点低通 + 包络跟随器，见 Settings → Auto Haptics）以及“不要把 USB 端 UAC1 音量同步到持久化配置”修复的来源。
- **[SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge)** —— 一个同类的 Pico DualSense 桥接项目。`src/remap.{h,cpp}` 中的按键重映射应用逻辑和数字控件集合移植自它。它的音频架构（设备端 Opus 编码 vs 可选的主机端编码配套应用）也是我们诊断扬声器节奏（“颤音”）问题时的参考。感谢它打下的基础。
- **[awalol/ds5dongle-config-web](https://github.com/awalol/ds5dongle-config-web)** —— 我们分支版网页配置应用 [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web) 的基础。该分支把上游 13 字节的 Config_body 布局改为我们的 19 字节布局，并为我们的新增功能（多槽配对、Auto Haptics）添加了界面。
- **[PS5 Button Icons and Controls](https://zacksly.itch.io/ps5-button-icons-and-controls)**（作者 **Zacksly**）—— 配置工具（桌面应用与网页工具）“按键配置”页面所用的 DualSense 控制器轮廓与按键图标，采用 [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) 授权（已重新着色为 `currentColor` 并裁剪以适配主题）。

## 路线图
- 请查看 [DS5Dongle plan](https://github.com/users/awalol/projects/5)

## 社区
- 加入 Discord 服务器：[Discord Server](https://discord.gg/hM4ntchGCa)
- 如果遇到 bug，请改为提交 issue。

## 参考

- [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense) —— 项目灵感
- [egormanga/SAxense](https://github.com/egormanga/SAxense) —— 蓝牙触觉 POC
- [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense) —— DualSense 数据报告结构文档
- [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX) —— 扬声器报告数据包

---

*本译文依据英文版 [README.md](README.md)，以英文版为准；译文可能更新滞后。*
