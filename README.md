# 简谱实验室

C++20 / Qt 6.10.2 多声部简谱桌面编辑器，采用 GPL-3.0-only，使用固定版本的 MuseScore 乐谱内核。界面与简谱排版由本项目实现。

## 使用

打开 `.musicxml`、`.xml`、`.mxl`、`.mid`、`.midi` 或 `.jianpu`。左侧可切换、隐藏和拖动调整声部显示顺序，数字和弦纵向叠写，声部按时间对齐。曲谱菜单可添加声部和常用乐器。工具栏修改时值、附点及升降号；双击左侧记号将其加入选中音符。连线与渐强渐弱需要选择同一声部的起止音符。选择谱面上的记号后，可在属性面板编辑力度、速度、渐变力度及房子编号，或删除重建其他记号。

`1–7` 改变音高，`0` 输入休止符，`N` 切换连续输入；方向键左右选择、上下改变音高，Ctrl+上下改变八度。`L` 编辑歌词，Shift+点击扩展范围，Ctrl+C/V 复制粘贴，Ctrl+Z/Y 撤销重做，Ctrl+滚轮缩放。

保存为 `.jianpu` 工程；通过文件菜单导出 MusicXML、MXL 或 MIDI。MIDI 导入支持 SMF 0/1、PPQ 时间制，提供轨道、量化及连音识别设置。MIDI 导出默认展开反复，不承载完整的制谱排版信息。

## Linux 构建

需要 GCC 14+、CMake 3.28+、Ninja 与 Qt 6.10.2。当前工作区的工具链已配置；全新 Ubuntu 环境可使用 `scripts/install-user-tools.sh`。

```bash
bash scripts/fetch-reference.sh
source scripts/env.sh
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 8
ctest --preset linux-debug
build/linux-debug/jianpu_lab examples/ensemble.musicxml
```

默认预设使用 `$HOME/Qt/6.10.2/gcc_64`。其他安装位置请在 `CMakeUserPresets.json` 中覆盖 `CMAKE_PREFIX_PATH`。首次配置会根据固定版本的 muse_deps 配方获取 XML、字体及压缩依赖。

## Windows 构建与分发

Linux 开发并推送 GitHub、另一台 Windows 拉取构建测试的完整步骤见 [跨平台开发流程](docs/cross-platform-development.md)。Windows 首次克隆后运行 `scripts/build-windows.ps1 -SkipPackage`；以后运行 `scripts/update-windows.ps1` 即可拉取更新、构建和测试。

安装 Git for Windows、Python 3.12、Visual Studio 2022 x64 C++ 工具链（MSVC 19.40+）、CMake、Ninja，以及 Qt 6.10.2 `msvc2022_64`，包括 Qt 5 Compatibility、Multimedia 和 Shader Tools。在 x64 Developer PowerShell 中运行：

```powershell
$env:QT_ROOT = 'C:/Qt/6.10.2/msvc2022_64'
./scripts/build-windows.ps1
```

脚本自动获取固定内核，执行构建、测试、运行库部署与冒烟验证，生成 `artifacts/JianpuLab-Release-win64.zip`。解压后运行 `bin/jianpu_lab.exe`。添加 `-SkipPackage` 可跳过 ZIP 和源码压缩，方便日常测试。`.github/workflows/windows.yml` 提供同样的 Windows CI 流程。

此工作区是在 Linux 上实现和验证的；Windows 脚本及 CI 尚未在 Windows 环境实际运行。Windows 10/11 无开发工具环境的交互验收须在发布前补齐，不能由 Linux 测试结果替代。

## 架构与工程格式

`ScoreDocument` 持有 `EngravingProject`，`MasterScore` 是唯一音乐数据源。`ScoreSnapshot` 提供只读视图，`JianpuLayout` 生成分页图元及点击区域，`EditRequest` 通过内核事务修改乐谱。撤销历史由内核管理，应用记录保存状态标识。内核访问限制在 GUI 线程；导入期间显示等待光标。

`.jianpu` 是 ZIP 容器：`manifest.json` 记录格式版本与内核提交，`score.mscz` 保存完整内核曲谱，`view.json` 保存简谱视图设置。使用 QSaveFile 原子替换工程文件；导入失败不替换当前文档。格式版本 1 要求匹配内核提交。

内核位于被忽略的 `references/MuseScore`，由脚本固定到提交 `4509c9072c583f7ab0c805ded2ab88f29e0ec0e5`。`cmake/engine` 单独构建必要模块，不构建 Studio 应用。兼容处理保留在应用侧，不修改参考源码。

## 验证与边界

`tst_document` 验证多声部格式往返、音高与节奏编辑、中文歌词、原子保存、轨道筛选、复制粘贴和丰富记号。`tst_editor` 验证缩放命中、选择、中文输入法提交、和弦碰撞和长曲谱布局。原始 `ScoreWidget` 仅保留为工具链回归样例，不参与正式应用。

```bash
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --render artifacts/score.png
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --inspect
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --save-project artifacts/example.jianpu
```

默认采用大调 1、小调 6 的简谱习惯，每个内核谱表最多四个声部。过密小节会扩展页面宽度以保持符号间距；声部过多时扩展页面高度。未实现五线谱视图、打击乐简谱编辑、试听、打印/PDF、安装程序、音频识谱和扫描识谱。内核能识别而简谱暂不支持的内容保留在工程中并报告；内核本身不支持的 MusicXML 扩展无法保证保留。

## 许可

参见 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。发行包需同时提供与二进制对应的本项目源码、固定内核源码和必要构建修改。构建脚本只生成本地产物，不自动发布。
