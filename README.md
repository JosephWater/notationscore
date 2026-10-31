# 简谱实验室

C++20 / Qt 6.10.2 多声部简谱桌面编辑器，采用 GPL-3.0-only，使用固定版本的 MuseScore 乐谱内核。界面与简谱排版由本项目实现。

## 使用

打开 `.musicxml`、`.xml`、`.mxl`、`.mid`、`.midi`、`.jptxt`、`.txt` 或 `.jianpu`。左侧可切换、隐藏和拖动调整声部显示顺序，数字和弦纵向叠写，声部按时间对齐。曲谱菜单可添加声部和常用乐器。工具栏修改时值、附点及升降号；双击左侧记号将其加入选中音符。连线与渐强渐弱需要选择同一声部的起止音符。选择谱面上的记号后，可在属性面板编辑力度、速度、渐变力度及房子编号，或删除重建其他记号。

点击文字稿正常编辑文字，点击谱面编辑音乐。点击中间音符默认修改，数字键依次修改已有音符并保留时值与歌词；选择末尾虚线空格默认连续追加。工具栏可选“末尾追加／修改音符／选后插入”，`Insert` 切换插入与修改，`End` 到当前声部末尾追加。`0` 休止符、`8` 隐藏休止符、`9` 节奏 X。左右键按时间选择，上下键按谱面位置选择，Alt+上下改音高，Ctrl+上下切换声部。`L` 编辑歌词，Ctrl+点击或框选多个元素，Ctrl+C/V 复制粘贴，Ctrl+Z/Y 撤销重做，Ctrl+滚轮缩放。已移除旧的 N 连续输入及 Shift 范围选择。

谱面“选后插入”与文字稿插入均可新增音符：插入音符后，新音符没有歌词，原音符保留自己的多段歌词。覆盖时 `/`、`.`、`-`、八度和升降后缀作用于刚修改的音符；时值改变后由内核调整节奏，再定位下一个输入位置。改为休止符时，原歌词保留在待分配区。多段歌词仍使用重复的 `C1`，`1` 是声部编号；将光标放在歌词行，文字区上方会显示“第几声部 · 第几段歌词”。示例见 `examples/lyrics-editing.jptxt`。

保存为 `.jianpu` 工程；通过文件菜单导出 MusicXML、MXL 或 MIDI。MIDI 导入支持 SMF 0/1、PPQ 时间制，提供轨道、量化及连音识别设置。MIDI 导出默认展开反复，不承载完整的制谱排版信息。

## 键盘打谱与文字稿

直接打开 `examples/paipaizuo.jptxt` 可体验整篇文字输入与实时谱面。`paipaizuo-draft.jptxt` 保留原始示例，首段多出的两个歌词词位会显示为待分配；草稿可保存为工程，解决错误后再导出音乐格式。

- 点击或聚焦文字区／谱面决定编辑位置，已移除 F6 模式切换；`Esc` 返回谱面命令状态。文本输入期间字母全部交给输入法。
- 命令 `B/Z/D/P` 编辑题目、署名、调号、拍号；`Q` 首次开始第一声部，以后新增独立声部；`Ctrl+↑/↓` 切换声部。
- 命令 `C` 从当前声部曲首新增一段歌词。文字稿中 `Q:` 续写曲行，`C:` 对应前面的曲行，多条 `C:` 表示多段歌词；`Q1:/Q2:`、`C1:/C2:` 的编号表示声部。
- 数字 `1–7` 音符、`0` 休止、`8` 隐藏休止、`9` 节奏 X。默认四分音符，后缀 `/` 减半，`-` 加一拍，`.` 对整个时值附点，`'`／`,` 升降八度，`#/$/=` 升降还原。例如 `1-.` 三拍、`1/-.` 2.25 拍。
- `[23]` 前倚音、`[h23]` 后倚音；`&mp` 力度、`"文字"` 注释、`(...)` 连奏线、`(y...)` 连音组、`<...!`／`>...!` 渐强渐弱；支持 `(y5:4 ...)` 指定比例。
- 中文逐字对应，标点不占位，`@` 跳过词位，`你~好` 合字。回改时歌词跟随原音符，新增音符空词，删除音符的歌词保留待分配。
- `Ctrl+空格` 补全字段，文字和谱面互相定位。语法错误及未闭合结构保留显示；整段粘贴一次撤销。

右侧“排版”面板提供经典出版、现代清爽、儿童教材预设，以及纸张、页边距、字体、字号、线宽、行距、歌词间距和换行设置；`Ctrl+Shift+L` 打开面板。可保存自定义预设，工程保存自身排版设置。内嵌 Noto 中文字体和 DejaVu Sans，测量与绘制使用相同字体。

“视图”菜单可以独立开关声部、记号、文字稿、属性、排版和报告面板，关闭后仍可重新打开。谱面和文字稿使用同一份音乐内容：自动分小节会回写 `|`，谱面换行会拆分为新的 `Q/C` 曲行，歌词保持原音符绑定；调整每行小节数后文字稿随之重排。这些回写与原操作一起撤销、保存。自动小节线会在后续时值修改时重新计算，手工小节线仍用于指定不完全小节。

跨小节长音拆为延音相连的音符，文字后缀 `^` 表示连接到下一个同音高音符，例如 `1 2-.^ | 2`；延音后续音不占歌词词位。歌词多出或连线未闭合时仍补充小节线和同步换行，多出的歌词留在所属曲行末尾并报告。暂时不能解析的语法保留原稿供修正。中文歌词采用与屏幕 DPI 无关的谱面字号，标点挂在字右侧，避免挤偏歌词对应位置。

经典出版预设参考提供的歌谱图：黑体主标题居中，副标题居中，调号与分式拍号居左、词曲署名居右并对齐，起始速度显示在页眉；数字采用常规字重，歌词清晰，行距更紧凑。默认隐藏小节号，普通曲行不额外绘制起始小节线；反复起始线照常显示。相同容量的谱行共享小节列宽，末行延续前行的列网格。

标题、副标题、作者、调号、拍号、歌词、小节线、注释与表达记号都可以点击选中、拖动调整位置。选中非音符元素后，方向键每次移动 1 px，Alt+方向键 0.2 px，Shift+方向键 10 px；移动支持撤销及保存重开。组合元素（例如分式拍号、跨行连线）整组移动，音乐时间不变。双击标题、作者、调号和拍号可编辑内容。记号按所属音符定位，同行其他记号不再通过全局水平避让挤动它。退格按一个输入音符删除，自动小节线不再占用一次退格。

当前适配器实现番茄式 1.0 的 V/B/Z/D/P/Q/C 字段及上述音符语法。嵌套连奏线可用，嵌套连音组会保留输入并提示。含无法表示内容的导入曲谱受保护，继续使用原有谱面编辑，不会被文字重建覆盖。

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

模块依赖、输入端口与可移动元素扩展方式见 [架构说明](docs/architecture.md)。

`ScoreDocument` 持有 `EngravingProject`，`MasterScore` 是唯一音乐数据源。`ScoreSnapshot` 提供只读视图，`JianpuLayout` 生成分页图元及点击区域，`EditRequest` 通过内核事务修改乐谱。谱面修改使用内核事务，整篇文字更新先在临时内核文档中验证，再原子替换。应用统一管理音乐、文字和排版的撤销历史，保留最近 128 次操作及保存状态标识。内核访问限制在 GUI 线程；导入期间显示等待光标。

`.jianpu` 是 ZIP 容器：`manifest.json` 记录格式版本与内核提交，`score.mscz` 保存完整内核曲谱，`view.json` 保存排版和输入习惯，`input.json` 保存文字稿、关联、光标及未完成草稿。使用 QSaveFile 原子替换工程文件；导入失败不替换当前文档。当前工程格式为 2，兼容读取版本 1，均要求匹配内核提交。

内核位于被忽略的 `references/MuseScore`，由脚本固定到提交 `4509c9072c583f7ab0c805ded2ab88f29e0ec0e5`。`cmake/engine` 单独构建必要模块，不构建 Studio 应用。兼容处理保留在应用侧，不修改参考源码。

## 验证与边界

`tst_projection` 验证长曲行／多段歌词同步、输入焦点与三种模式、跨小节退格、复合输入、记号锚定和元素移动／工程恢复；`tst_textinput` 验证文字语法、精确连音、键盘模式、中文输入、歌词绑定和工程恢复；`tst_document` 验证多声部格式往返、音高与节奏编辑、中文歌词、原子保存、轨道筛选、复制粘贴和丰富记号。`tst_editor` 验证缩放命中、选择、中文输入法提交、和弦碰撞和长曲谱布局。原始 `ScoreWidget` 仅保留为工具链回归样例，不参与正式应用。

```bash
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --render artifacts/score.png
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --inspect
QT_QPA_PLATFORM=offscreen build/linux-debug/jianpu_lab examples/ensemble.musicxml --save-project artifacts/example.jianpu
```

默认采用大调 1、小调 6 的简谱习惯，每个内核谱表最多四个声部。纸张保持固定尺寸；密集小节可在拍组边界续行，过高的声部组提示调整字号或纸张。未实现五线谱视图、打击乐简谱编辑、试听、打印/PDF、安装程序、音频识谱和扫描识谱。内核能识别而简谱暂不支持的内容保留在工程中并报告；内核本身不支持的 MusicXML 扩展无法保证保留。

## 许可

参见 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。发行包需同时提供与二进制对应的本项目源码、固定内核源码和必要构建修改。构建脚本只生成本地产物，不自动发布。
