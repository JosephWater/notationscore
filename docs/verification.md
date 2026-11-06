# 五线谱导入保真与 AI 图片识谱（0.6.0）

验证日期：2026-11-05。Linux、GCC 15.2.0、Qt 6.10.2；固定 MuseScore 提交保持干净。

- Linux Release、Debug 均完成构建，最终 **11/11 CTest 套件通过**。新增 `tst_notation` 与 `tst_ai`；旧套件继续通过。日志：`artifacts/build-ai-release.log`、`build-ai-debug.log`、`ctest-ai-release-final.log`、`ctest-ai-debug.log`。
- 新语义用例包含和弦、跳房子多编号与开闭端点、反复次数、跨行续画、MusicXML/MXL/工程往返、七连音的精确记号时间、变调变拍、八度线不重复移调、删除词位/改休止保留两段歌词、片段复制、撤销重做和罕见原生演奏法的安全导出。
- AI 模拟服务验证 UTF-8/SSE 分包、忽略推理内容、损坏事件、截断、401/403/429/500、取消、超时、固定看图复核、最多三次调用、图片顺序、独立窗口、草稿人工接续、一次撤销和识别中关闭窗口。常规测试不连接外部收费服务。
- 192 DPI 下完整运行 notation、ai、selection、lyrics 四套测试。固定字体截图：`artifacts/ai-notation-classic.png`、`ai-notation-modern.png`、`ai-notation-children.png`、`ai-notation-ui.png`。保留了原有三种排版预设，Bravura 音乐符号随程序内嵌。

真实接口使用用户提供的 Sophnet 地址与 key、`DeepSeek-Flash`，验证文本、图片及 SSE 流式返回。key 仅写入用户配置，不进入源码、工程或截图。

- 32 小节双页谱：两轮识别/看图复核，128 个音符、音高与时值顺序、双段歌词与参考 MusicXML 比较一致。
- 含和弦、跳房子与双段歌词的复杂谱图：两轮后 19 个音符及歌词与参考一致；但部分跳房子、踏板和跳转范围不正确，仍有记号漏抄。格式通过不代表完整识别所有图中记号。
- 默认高强度隐藏推理曾耗尽输出额度且没有正文；最终请求对 DeepSeek 显式关闭思考模式，实际图片识别与复核可以正常流式返回。

识别记录：`artifacts/ai-live-rich-v3.log`、`ai-live-rich-v3.jptxt`、`ai-live-multipage.log`、`ai-live-multipage.jptxt`、`ai-live-comparison.json`。它们属于本次样例验证，不能推断任意谱图准确率。导入原生文件的保真与 AI 看图准确率分别验证。

Windows 脚本已加入新程序和测试的依赖部署；本轮未执行 Windows/MSVC、真实 Windows 中文输入法或无开发工具便携环境验收。MIDI 不携带的制谱信息不推测补齐，固定内核不认识的 MusicXML 扩展不能保证保留。使用与文字扩展说明见 [AI 图片识谱](ai-recognition.md)。

---

# 词位编辑、范围选择与片段搬移（0.5.0）

验证日期：2026-11-02。Linux、GCC 15.2.0、Qt 6.10.2；固定 MuseScore 与两个子模块提交，参考源码保持干净。

- 谱面直接中文填词与多段歌词：中间覆盖、末尾追加、选后插入、范围替换、输入法预编辑、方向键导航、行尾空格及连续按 C。超出当前 Q 行的歌词延续到同声部后续曲行；整首超出的文字保留待分配。歌词编号单独画在行左侧，开关编号不改变音符或歌词位置。
- 普通第二段歌词、跨小节长音与自动换行不再误触发保护。测试覆盖原生和弦／乐器导入后的歌词与元数据修改、有效旧草稿恢复、待分配词的逐个分配／移除、撤销后重新导出。音乐格式导出按当前内容验证，真实未完成输入仍阻止导出。
- Word 式拖动划选、Shift 连选、Ctrl 多选；小节空白选择整节。整节与多节支持插入、覆盖、非连续源和非连续目标、删除与撤销重做。点击目标小节内音符也能整节粘贴；Ctrl+Shift+V 明确插入，按键和菜单路径一致。
- 拖动已选音符／歌词／小节搬移，Ctrl 拖动复制，落点显示插入标记。测试包含组搬移后多段歌词、和弦单音、同谱表不同声部粘贴、完整三连音组和连奏线、反复小节、七连音歌词的精确落点、损坏片段失败回滚、注释与未知字段保留、删除词位留待分配和草稿不被覆盖。
- 图形元素独立拖动吸附、旋转、等比缩放；绘制与命中使用同一变换。音符与歌词排除图形变换。力度和渐变力度在音符附近使用紧凑层，歌词单独预留空间；现代预设的小节号与音符注释分层，三个预设均检查不相交。
- 输入、歌词映射、选择规则、吸附几何和内核片段事务分别封装，依赖方向见 [架构说明](architecture.md)，具体操作见 [编辑约定](editing-logic.md)。Windows 部署脚本加入两个新测试程序，便携目录同时安装操作文档。

Linux Release 和 Debug 最终均为 **9/9 CTest 套件通过**。`tst_selection` 16 个行为场景、`tst_lyrics` 15 个行为场景；192 DPI 下两套完整场景也通过。日志：`artifacts/ctest-editing-release-v050.log`、`ctest-editing-debug-v050.log`、`selection-dpi192-v050.log`、`lyrics-dpi192-v050.log`。

示例 `examples/interactive-lyrics.jptxt`。固定字体谱面截图为 `artifacts/interactive-lyrics-v050.png`、`interactive-modern-v050.png`、`interactive-children-v050.png`，旋转缩放截图为 `graphic-transform-v050.png`，完整界面为 `interactive-lyrics-ui-v050.png`；已人工检查歌词、编号与记号位置。`.jianpu` 保存变换与排版；MusicXML/MIDI 不承载全部简谱几何调整。

尚未运行 Windows / MSVC、真实 Windows 中文输入法和未安装开发工具的便携环境验收。单声部音符片段不能混选多个声部，跨声部一起操作应选整小节；不完整连音组无法安全粘贴时保留原曲谱并提示完整选择。原生导入的复杂对象继续保留保护，不使用文字投影重建原乐器数据。

---

# 同步编辑与元素调整（0.4.0）

本轮完成文字稿、输入、布局投影和图形元素的职责拆分，依赖关系见 [架构说明](architecture.md)。

- 长曲行按谱面系统时间范围拆为 Q/C，歌词多出或连线未闭合不再阻止自动小节线和换行。96 音符、双段歌词、超出歌词和超过第一谱行的插入均覆盖；歌词按原音符绑定，空音符不挪走原词。
- 移除 F6，点击／焦点决定编辑位置。中间音符默认为修改，末尾空格为追加，工具栏与 Insert 可选择插入。方向键导航；Alt+上下修改音高；原有 Ctrl+上下声部切换保留。
- 退格跨自动小节线和多条 Q 行时一次删除一个输入音符。测试连续从 48 音符删除至空白并检查撤销；手工反复线保留且不阻挡删除。
- 复合输入覆盖三连音、力度、渐变力度及自动小节线后的后倚音。未完成倚音暂存草稿，完成后提交；中间输入倚音不会删除后续音符和歌词。
- 记号横向按实际音符锚点居中，垂直空间只在所属系统、声部内预留，不再使用全局横向推挤。标题、作者、调号、拍号、歌词、符号与跨行图形共用 VisualElement 交互。拖动一次提交，方向键可微调，支持撤销、保存与重开；组合拍号整组移动。
- 原生导入投影补齐缺失声部在弱起／静默小节中的占时隐藏休止符，整小节休止符使用准确时值。记号偏移在原生音高编辑后保留。当前语法无法表达的导入对象仍受保护，保留内核原始内容。

Linux Qt 6.10.2 的 Release 与 Debug 各 7 个 CTest 套件通过；日志为 `artifacts/ctest-sync-release-v040.log`、`artifacts/ctest-sync-debug-v040.log`。新增 `tst_projection` 的 15 个行为场景通过；高 DPI 检查为 `artifacts/projection-dpi192-v040.log`。三种预设截图为 `artifacts/synchronized-editing-v040.png`、`synchronized-modern-v040.png`、`synchronized-children-v040.png`；界面截图为 `artifacts/synchronized-editor-v040.png`，示例为 `examples/synchronized-editing.jptxt`。

未在 Windows / MSVC 或真实 Windows 输入法环境运行本轮代码；`build-windows.ps1` 已加入新套件的 Qt 运行库部署，Windows 用户应运行更新脚本完成当地构建与测试。

---

# 小节对齐、覆盖输入与歌词绑定（0.3.2）

验证日期：2026-10-31。

- 小节线围绕数字实际墨迹的中线绘制，八度点不再将小节线整体向下挤；小节内按数字、附点及增时线的实际宽度平衡左右留白。继续共享多声部时间位置，保留字号、缩放、歌词对齐与命中区域。
- 删除 N 连续输入状态和 Shift 范围选择。点击谱面后数字依次覆盖已有元素，保留原时值和歌词；支持 0 / 8 / 9 的休止、隐藏休止和节奏音符，End 转到末尾追加。时值后缀作用于刚输入的元素，输入位置随内核重排更新；和弦中选中的单音及延音连接链均已覆盖。
- 文字稿记录实际修改区间，解决重复音符导致插入位置判断错误的问题；阻断文字定位谱面时的反向选择反馈，避免把插入光标变为替换选区。文字区插入新增空词音符，原音符的多段歌词保留，覆盖到休止符的歌词进入待分配区。
- 保留重复 C1 的语法，文字区显示当前声部和歌词段次，自动重排也保留原稿是否显式编号。歌词分配跳过延音后续音，改变一声部时值不会错配其他声部的歌词绑定。
- 回归测试包含三种预设的墨迹居中断言、重复音符的两段歌词、跨自动小节线插入、保存重开、撤销重做、点击中间覆盖、长音后继续覆盖、和弦内单音、0 / 8 / 9、段次提示、延音链和多声部歌词；原有格式与编辑套件继续通过。

Linux Debug / Release 各六个 CTest 套件通过，日志 `artifacts/ctest-editing-debug.log`、`artifacts/ctest-editing-release.log`。192 DPI 的对齐与重复音插入检查为 `artifacts/lyrics-editing-dpi192.log`。示例 `examples/lyrics-editing.jptxt`，谱面 `artifacts/lyrics-editing-v032.png`，界面 `artifacts/lyrics-editing-ui-v032.png`。

Windows 仍需实机编译和中文输入法验证。未闭合的延音及超出曲行词位的歌词继续保留并报告，不能用自动排版将其丢弃。

---

# 歌词、文字稿同步与歌谱版式（0.3.1）

验证日期：2026-10-31。参考图：工作区 `/home/liyuese/dev/640.png`；只参考版式，不将原图加入发行包。

- 修正屏幕 DPI 改变时谱面字体尺寸与文字框不一致的问题；歌词使用统一的像素字体和实际字体高度，多段歌词独立留出空间，标点悬挂在字的右侧。测试覆盖 96 / 144 / 192 DPI，以及中文逐字提交、行尾光标、连续填词和手工 `@` 跳音。
- 六个 Dock 面板全部接入“视图”菜单，关闭后可以从菜单重新打开。歌词编辑快捷键会显示并激活属性面板。
- 自动小节线、谱面换行和歌词行回写到文字稿。自动生成的边界与手工边界分别记录；后续时值改变会重新计算自动边界。回写与触发它的音乐或排版修改共用一次撤销历史，并随工程保存重开。
- `^` 表示延音连接，跨小节长音拆分后不会多占歌词词位；显式连音比例保持精确时值。原生导入的连音组投影保留比例与分组，已覆盖 MusicXML 导出重开后文字位置与内核位置的对应。
- 经典出版采用居中黑体标题、居中副标题、左侧调号与分式拍号、右侧署名和页眉初始速度；数字常规字重，隐藏小节号，普通行首不多画小节线。多声部共用时间轴，常规小节等宽，末行延续前行的列宽，歌词与音符字心对齐。
- 程序与内核日志版本统一为 0.3.1。Windows 部署显式包含 `qoffscreen`，修正测试仅部署 `qwindows` 的问题。

Linux Debug / Release 的六个 CTest 套件通过；日志为 `artifacts/ctest-alignment-debug-final.log`、`artifacts/ctest-alignment-release-final.log`。高 DPI 检查为 `artifacts/alignment-dpi192.log`。三种预设与多声部截图为 `artifacts/aligned-classic.png`、`aligned-modern.png`、`aligned-children.png`、`aligned-multivoice.png`，完整界面为 `artifacts/aligned-editor.png`。示例 `examples/alignment.jptxt` 可用于观察自动小节线、曲行和双段歌词回写。

保留的边界：语法错误、未闭合结构与待分配歌词暂不重排，先保留原稿；无法完整表示的原生对象仍在内核中受保护。当前环境未运行 Windows 构建或实机中文输入法，不以 Linux 的高 DPI 检查代替 Windows 验收。

---

# 键盘打谱与排版验证（0.3）

验证日期：2026-10-30。Ubuntu 26.04、GCC 15.2、Qt 6.10.2；MuseScore 与子模块仍保持固定提交，参考 checkout 未修改。

- Linux Debug 与 Release 均完成构建，六个 CTest 套件通过。新套件 `tst_textinput` 覆盖文字语法、时值、升降号习惯、精确七连音及 MusicXML 往返、跨小节延音、前后倚音、嵌套连奏线、渐变力度、中文注释、歌词绑定、删除歌词保留、双模式键盘和中文输入法提交。
- 原有格式、编辑、选择、原子保存及 Qt 运行时回归保持通过。新建默认一小节；密集小节续行后保持固定纸张，原测试的页面扩宽预期已相应更新。
- 工程格式 2 保存 `input.json`，恢复文字稿、未完成结构、绑定与排版；继续读取格式 1。损坏的文字稿关联会在替换文档前拒绝加载。
- 新文字曲谱导出前显式构建 MIDI 映射，避免内核导出器访问尚未初始化的通道。后倚音使用无斜线的 MusicXML grace 标记及 steal-time-previous，已验证恢复为内核后倚音。
- 字体统一测量和绘制，中文字体、数字字体及许可随程序内嵌。截图在固定应用字体下生成，三种预设均检查。

产物：`build/linux-release/jianpu_lab`；测试日志 `artifacts/ctest-keyboard-debug.log`、`artifacts/ctest-keyboard-release.log`；界面 `artifacts/keyboard-editor.png`；谱面 `artifacts/keyboard-classic.png`、`artifacts/keyboard-modern.png`、`artifacts/keyboard-children.png`。

示例：`examples/paipaizuo.jptxt` 补齐原始示例的两个缺失音符；`paipaizuo-draft.jptxt` 原样保留用户示例并显示待分配歌词；`keyboard-ensemble.jptxt` 演示编号声部、多段歌词和减时线。

边界：首个适配器覆盖文档列出的番茄式字段与音符语法；嵌套连音组提示未支持。当前无法完整回写的复杂谱面操作或导入内容会启用保护，保留完整内核数据并允许继续谱面编辑，正文文字重建受到限制。过高的声部组需调整字号或纸张。

Windows 构建脚本与 CI 已包含新套件；字体通过 Qt 资源部署。当前环境仍没有 MSVC／Windows Qt SDK，尚未执行 Windows 构建和实机中文输入法验收，不能据 Linux 测试宣称 Windows 已验证。

---

# 首版验证记录

验证日期：2026-10-19。环境为 Ubuntu 26.04、GCC 15.2.0、Qt 6.10.2，使用固定的 MuseScore 与子模块提交。Windows 验证未执行。

## 已执行

| 检查 | 结果 |
| --- | --- |
| Linux Debug 完整构建 | 通过 |
| Linux Release 完整构建 | 通过；主程序约 34 MB |
| Debug CTest | 5/5 通过 |
| Release CTest | 5/5 通过 |
| 正式应用启动、显示与自动退出 | 通过；独立 app_smoke 用例 |
| GDB 命中 ScoreCanvas::paintEvent 并正常退出 | 通过 |
| 8 小节、3 声部、96 音符示例导入 | 通过 |
| 原生工程、MusicXML、MXL、MIDI 往返 | 通过；原生文件额外比较音符标识与显示设置 |
| MIDI 0/1、轨道筛选、量化设置 | 通过 |
| 音高、相对调号的升降还原、附点、歌词、和弦、声部、乐器、拍号编辑 | 通过 |
| 复制粘贴、撤销重做、保存状态、失败时保留原文档 | 通过 |
| 反复、跳转、连奏线、延音线、力度、速度、渐变力度、装饰音、倚音、三连音及房子 | 对已实现的类型通过编辑与原生保存重开测试 |
| 中文输入法提交、缩放命中、范围选择、和弦八度点碰撞、密集节奏及长曲谱 | 通过 |
| 弱起、转调、变拍、保留鼓轨的综合样例 | 通过 |
| 源码目录保护 | 指定的原始源码目录未修改，参考 checkout 的 git status 保持干净 |

测试入口为 tests/tst_document.cpp、tests/tst_editor.cpp，以及保留的工具链测试。完整 CTest 输出位于本地 artifacts/ctest-debug.log 和 artifacts/ctest-release.log，GDB 记录位于 artifacts/gdb-smoke.log。预览通过应用的 --render 和 --screenshot 生成。

## 实施中的兼容处理

内核只构建必要模块，使用真实模块初始化、配置和字体。固定版本在禁用开发工具后缺少一个生产上下文需要的声明，通过应用侧 wrapper 补齐；5.00 原生读取器的“尝试识别跨音符记号”探测改为静默探测，后续未知属性的实际错误报告仍保留。以上处理位于 cmake/engine，参考源码保持不变。

MIDI 首次转换是轨道发现阶段，会忽略轨道筛选。应用在临时曲谱中完成发现后，使用全新正式曲谱执行带用户选项的第二次转换。简谱连音组的显示范围由实际成员音符计算。

## 尚未满足的验收

- 当前环境没有 Windows、MSVC 或 Windows Qt SDK，尚未生成和运行 Windows exe/便携 ZIP。
- scripts/build-windows.ps1 和 Windows CI 已准备，但未在 Windows 上执行；M1、M4 的 Windows 验收仍未完成。
- 发布前需在未安装 Qt 和开发工具的 Windows 10/11 x64 上验证启动、中文输入、文件读写及独立运行。脚本的自动冒烟验证不能替代这一步。
- 复杂总谱的细致排版仍需人工校对。0.3 已改用固定纸张和密集小节续行；过高声部组提示调整设置。首版不包含自由拖动所有排版控制点、五线谱视图或打击乐简谱编辑。

试听、打印/PDF、安装程序、扫描识谱和音频转谱不在本期范围。
