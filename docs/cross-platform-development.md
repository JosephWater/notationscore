# Linux 开发、GitHub 同步、Windows 测试

日常流程：Linux 修改和测试 → 提交并推送 GitHub → Windows 拉取同一提交 → 本机构建、自动测试和交互测试。两端分别保留自己的 `build/` 和 `artifacts/`，不通过 Git 同步二进制。

## 1. Linux 首次连接 GitHub

目标仓库为 `https://github.com/JosephWater/notationscore.git`，连接时已确认为空仓库。当前 Linux 工作区已初始化 `main` 分支并配置 `origin`。以下提交身份要替换成自己的；可以使用 GitHub CLI 的 `gh auth login --hostname github.com --git-protocol https --web` 完成浏览器登录和 Git 凭据配置，不使用账户密码作为 HTTPS Git 密码。

```bash
cd /home/liyuese/dev/jianpu-lab
# 当前工作区已经初始化为 main；全新源码目录才需要执行：
# git init -b main
git config user.name "你的提交名称"
git config user.email "你的 GitHub 邮箱或 GitHub 提供的 noreply 邮箱"
# 当前工作区已配置 origin；全新源码目录才需要执行：
# git remote add origin https://github.com/JosephWater/notationscore.git
git add .
git diff --cached --stat
git commit -m "Initial JianpuLab source"
git push -u origin main
```

`.gitignore` 已排除内核参考源码、构建目录、分发包、日志和本机 CMake 配置。MuseScore 约 690 MB，不提交到本仓库；两端通过脚本获取同一个固定版本。跨平台文本换行由 `.gitattributes` 统一为 LF。

参考：[GitHub 导入已有项目](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github)。如果目标仓库已有提交，先检查其内容和历史，再决定如何合并。

## 2. Windows 首次准备

使用 Windows 10/11 x64，选择一个短路径，例如 `C:\dev\jianpu-lab`。

1. 安装 Git for Windows。
2. 安装 Visual Studio 2022 或 Build Tools，选择“使用 C++ 的桌面开发”，包含 MSVC v143 x64/x86 工具、Windows SDK、CMake 和 Ninja。MSVC 必须达到 19.40（工具集 14.40）或更高。
3. 安装 Python 3.12，并使 `python` 可在命令行调用。
4. 安装 Qt 6.10.2 的 `msvc2022_64`，包含 Qt 5 Compatibility、Multimedia、Shader Tools。可以使用 Qt 安装器，也可以在 PowerShell 中执行与项目 CI 一致的命令：

```powershell
python -m pip install aqtinstall==3.3.0 cmake==3.31.6 ninja==1.11.1.3
python -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -O C:/Qt -m qt5compat qtmultimedia qtshadertools
```

打开 **Developer PowerShell for VS 2022（x64 环境）**，确认 `cl`、`cmake`、`ninja`、`git`、`python` 能执行，然后首次拉取并构建：

```powershell
New-Item -ItemType Directory -Force C:\dev | Out-Null
Set-Location C:\dev
git clone https://github.com/JosephWater/notationscore.git jianpu-lab
Set-Location jianpu-lab
$env:QT_ROOT = 'C:/Qt/6.10.2/msvc2022_64'
./scripts/build-windows.ps1 -SkipPackage
./artifacts/JianpuLab-Release-win64/bin/jianpu_lab.exe
```

`QT_ROOT` 在每个新终端里设置一次；也可在调用脚本时传 `-QtRoot 'C:/Qt/6.10.2/msvc2022_64'`。如果提示禁止运行脚本，可以仅对当前 PowerShell 窗口执行 `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass` 后重试。

构建入口会自动获取固定 MuseScore 内核及其子模块、配置 CMake、编译、执行 CTest、部署 Qt/MSVC 运行库，再验证部署后的程序能启动。首次需要下载较多依赖；后续复用现有依赖和编译结果。此路径不需要额外启动 Git Bash。

## 3. 每次 Linux 修改后

```bash
cd /home/liyuese/dev/jianpu-lab
source scripts/env.sh
cmake --build --preset linux-debug --parallel 6
ctest --preset linux-debug
```

确认测试通过后再提交并推送：

```bash
git status
git add .
git diff --cached --stat
git commit -m "描述本次修改"
git push
```

首次配置或者更改 CMake 配置时，先执行 `cmake --preset linux-debug`。新机器需要先按 README 安装 Linux 工具链并执行 `bash scripts/fetch-reference.sh`。

## 4. Windows 每次更新和测试

关闭上一次启动的程序，在 x64 Developer PowerShell 中执行：

```powershell
Set-Location C:\dev\jianpu-lab
$env:QT_ROOT = 'C:/Qt/6.10.2/msvc2022_64'
./scripts/update-windows.ps1
./artifacts/JianpuLab-Release-win64/bin/jianpu_lab.exe
```

`update-windows.ps1` 先检查工作区，再用 `git pull --ff-only` 更新当前分支的上游，显示本次测试的提交号，然后调用构建测试入口。建议 Windows 保持在克隆得到的 `main` 分支；脚本不会自动切换分支。有本地修改或分叉历史时会停止，保留现场供你处理。`--ff-only` 的行为见 [Git 文档](https://git-scm.com/docs/git-pull#Documentation/git-pull.txt---ff-only)。

日常更新默认跳过压缩源码和分发包，仍执行编译、测试、运行库部署与冒烟验证。需要 ZIP 和对应源码包时执行：

```powershell
./scripts/update-windows.ps1 -Package
```

产物是 `artifacts/JianpuLab-Release-win64.zip` 和 `artifacts/JianpuLab-source.tar.gz`。只重新构建当前代码、不拉取时，使用 `./scripts/build-windows.ps1 -SkipPackage`；调试构建可给任一入口添加 `-Configuration Debug`。

交互测试至少覆盖：打开示例、音符编辑与撤销、中文歌词输入、保存并重开 `.jianpu`、MusicXML/MIDI 导入导出。反馈问题时附上 `git rev-parse --short HEAD`、复现步骤和错误输出；CTest 日志在 `build/windows-release/Testing/Temporary/LastTest.log`。Windows 上如需修改源码，将修改提交推回 GitHub，再在 Linux 拉取后继续开发。

## 5. GitHub 自动构建

仓库已有 `.github/workflows/windows.yml`：push、PR 或手动触发时，在 Windows runner 上安装工具链并构建、测试、打包。首次推送后到仓库的 **Actions** 页面查看结果；成功后可下载 `JianpuLab-Windows-x64` 构建产物，失败时可查看步骤日志和测试日志附件。

本机 Windows 构建方便增量编译和交互排查；Actions 可提供额外的 Windows 构建结果。本次流程准备在 Linux 上完成，尚不能证明真实 Windows 构建、输入法和界面交互通过，需在你的 Windows 电脑或 Actions 中验证。
