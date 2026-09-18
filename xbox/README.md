# KRKR Xbox UWP

本目录把 [Kirikiri SDL2](https://github.com/krkrsdl2/krkrsdl2) 作为引擎上游，目标是生成可在 Xbox Developer Mode 安装的 MSIX。仓库根目录只保存移植层；构建脚本会自动浅克隆引擎及其子模块，避免把上游源码复制进本项目。

## 在 Windows 上构建

需要 Visual Studio 2022 的 Desktop C++、Windows 10/11 SDK、Meson、Ninja 和 Xbox Developer Mode。请在 **x64 Native Tools Command Prompt for VS 2022** 中运行 PowerShell：

```powershell
$env:KRKR_GAME = 'D:\Games\YourGame'
.\xbox\build-vs.ps1
```

`build-vs.ps1` 使用 Meson 的 Visual Studio 后端并调用 MSBuild；也可以用 `build.ps1` 的默认 Ninja 后端：

```powershell
.\xbox\build.ps1
```

上游已经将 Meson 标记为弃用，推荐使用 CMake。安装 vcpkg，并确保其中存在 `x64-uwp` triplet，然后运行：

```powershell
$env:VCPKG_ROOT = 'C:\vcpkg'
$env:KRKR_GAME = 'D:\Games\YourGame'
.\xbox\build-cmake.ps1
```

该路线使用 Visual Studio 2022 的 CMake 生成器、Windows Store 工具链和 `x64-uwp` SDL2 依赖，适合 Xbox 主机的 x64 开发者模式包。

## 没有 Windows

仓库提供了 GitHub Actions 云端构建：[`.github/workflows/build-xbox-msix.yml`](../.github/workflows/build-xbox-msix.yml)。将项目推送到 GitHub 后，在 **Actions → Build Xbox MSIX → Run workflow** 中填写一个由你控制的 ZIP 地址；ZIP 内必须有 `startup.tjs` 或 XP3 文件。工作流会在 `windows-2022` runner 上构建、生成临时开发证书并签名，然后把 `KRKR-Xbox.msix` 和 `.cer` 上传为 Artifact。

下载 Artifact 后，先在 Xbox Dev Mode 的证书管理/Device Portal 中安装 `krkr-xbox-dev.cer`，再安装 MSIX。每次工作流运行都会生成新证书，旧包和旧证书不能混用。

只应上传你有权使用的游戏资源。不要把受 DRM 或加密保护的商业资源提交到仓库或公开 URL。

`KRKR_GAME` 必须包含 `startup.tjs` 或至少一个 XP3 包以及游戏所需的其他资源。脚本会生成最小 PNG 图标，生成物是 `out\KRKR-Xbox.msix`。

首次拉取本项目后直接运行脚本即可；也可以预先执行：

```powershell
git submodule update --init --recursive
```

在 Xbox 的 Dev Home 中启用 Developer Mode，打开 Remote Access，上传并安装 MSIX。第一次安装自签名包时需要在主机上信任对应证书；开发包不要用于分发商业游戏。

## 当前边界

这是基于 SDL2 的引擎移植层，不包含任何游戏资源，也不会绕过 XP3 加密、DRM 或签名校验。上游目前主要验证 Linux、macOS、Android 和 Web 构建；Xbox/UWP 的最终兼容性取决于 Windows SDK 对 SDL2 后端、字体、音频和插件的支持。`build.ps1` 在 Windows 上会先验证这些条件，失败时保留 Meson 的具体错误。

MSIX 必须使用开发证书签名后才能安装。`makeappx` 只负责打包，不会替你生成或安装证书；可使用 Visual Studio 的开发者证书或 Xbox Dev Home 的测试证书完成签名。