# 导入保真与图片识谱（0.6.0）

## MusicXML / MIDI

“文件 → 打开”直接将 `.musicxml/.xml/.mxl/.mid/.midi` 转为简谱显示。原生导入始终保留完整 MuseScore 工程，不使用文字投影替换原生乐器定义。

支持反复、反复次数、多个跳房子编号、开放/闭合房子、跨行续画、D.C./D.S./Fine/Coda/Segno、速度、力度、连线、倚音、连音组、常用演奏法、装饰音、延长、换气、踏板及八度线。常用音乐符号使用随软件内嵌的 Bravura 字体。报告面板列出导入器诊断及内核保留但尚未显示的对象。MIDI 不含的制谱信息不会凭空恢复；未知 MusicXML 扩展仍受固定内核导入器的支持范围限制。

## 图片识谱

1. “文件 → AI 接口设置…”配置完整请求地址、模型和 key；可单独测试连接。
2. “文件 → AI 图片识谱…”添加 PNG/JPEG/WebP，拖动或用上移/下移调整页序，预览并移除不需要的图片。
3. 开始后打开独立新窗口。文字稿区依次显示识别、对照原图复核和按需修复的流式结果。原窗口不受影响。
4. 检查通过后整体提交；一次撤销回到新窗口初始状态。取消、超时、接口错误或输出截断保留草稿；可手工修改后继续正常编辑。

默认识别后检查两轮：第一轮固定看图复核，仍有语法、时值、歌词、记号落点或疑点时才调用第二轮修复；最多三次 AI 调用。设置为 1 只复核，0 只进行本地检查。不会自动切换模型或服务商，也不会自动重试拼接中断流。

谱图会发送到配置的服务；仅在点击开始时上传。单张图片最多 6000 万像素，全部图片 PNG/base64 编码合计最多 32 MiB；超过限制时提示裁剪或减少图片，不暗中降低清晰度。请求空闲超时 60 秒，总超时 300 秒。识别期间不能修改目标窗口；可取消后修改已接收草稿，关闭窗口也会中止请求。

默认地址 `https://www.sophnet.com/api/open-apis/v1/chat/completions`，模型 `DeepSeek-Flash`。key 保存在 Qt 用户配置目录的 `ai.json`，Linux 为 `~/.config/JianpuLab/JianpuLab/ai.json`，仅用户可读写；也支持 `JIANPU_AI_API_KEY` 环境变量覆盖。工程、源码和普通发行包均不携带 key。Windows 使用对应的用户配置目录。

DeepSeek 请求显式关闭默认思考模式，防止隐藏推理耗尽输出额度而没有返回文字稿。其他模型不附加此模型专用参数。协议参考：[Sophnet](https://sophnet.com/docs/component/API.html)、[DeepSeek 思考控制](https://api-docs.deepseek.com/guides/thinking_mode/)。

系统提示词位于 `src/ai/recognition-system-v1.txt`，通过 Qt 资源打包。它包含实际解析规则，要求只输出文字稿，不编造音符；无法确认处使用 `# 疑点：…` 注释。存在疑点时保留草稿，不标记任务成功。

**本地格式检查不能证明视觉识别准确。** 清晰谱图也可能漏抄八度点、和弦音或把跨行记号的范围看错。完成提示会要求对照原图核对；人工核对尤其应检查跳房子、反复次数、连线范围和歌词。

## 番茄式 1.1 扩展

旧 `V: 1.0` 曲谱继续使用原格式。和弦或 M 记号使用 `V: 1.1`，工程的输入格式记录为 `fanqie-1.1`；旧程序会明确拒绝该格式，避免静默丢失。ZIP 工程容器仍为格式 2。

和弦 `{1 3' 5#}/`：括号内列出独立音高，时值后缀写在右括号后并共享。一个和弦只占一个歌词词位。其他 Q/C、歌词段次和注释规则不变。

`M:` 后为单行 JSON，对象包含 `kind`、`voice`、`start`。voice=0 表示曲谱级，其他数字是 Q 声部编号。时间为从曲首起、全音符为单位的非负精确分数字符串，例如 `0/1`、`1/14`、`2/1`，不使用四舍五入的 tick。M 行可以放在所有 Q/C 后，自动换行不会改变它的音乐位置；插删与谱面编辑按原音符身份调整坐标。

| kind | 其他参数 |
| --- | --- |
| volta | `end`、`endings:[1,3]`、`closed:false`；起止为小节边界 |
| repeat | `count:3`；start 必须对应右反复线的结束位置 |
| jump | `type:DC/DS/DC_FINE/DS_FINE/DC_CODA/DS_CODA`，可选 target |
| marker | `type:segno/coda/fine/tocoda`，可选 label |
| tempo | `bpm:96`，可选 text；BPM 为四分音符每分钟 |
| fermata / breath | start 对应音符 |
| articulation | `type:staccato/tenuto/accent/strong-accent/staccatissimo/trill-mark/turn/inverted-turn/mordent/inverted-mordent` |
| pedal | `end` |
| ottava | `end`、`octaves:1/2/-1/-2`；Q 写实际音高，不重复移调，起止落在音符边界 |
| key | `fifths:-7…7`、`minor:false`；位于小节起始 |
| time | `numerator:3`、`denominator:4`；位于小节起始 |

原生导入保留内部跳转目标、跳房子类型、反复次数和其他扩展对象。文字格式只允许注册的记号类型和参数；不能完整表达的原生对象继续保护。示例见 `examples/ai-recognition.jptxt`。

## 开发验证

`tst_notation` 覆盖新旧语法、和弦、记号保真、MusicXML/MXL/工程往返、精确连音时间、变调变拍、八度线、片段和撤销。`tst_ai` 通过本地模拟 HTTP 服务验证流、错误和窗口行为；常规 CTest 不访问收费服务。

真实服务验证使用独立命令，显式调用才会上传图片并消耗额度：

```bash
QT_QPA_PLATFORM=offscreen build/linux-release/jianpu_ai_probe image1.png image2.png \
  --checks 2 --output artifacts/recognized.jptxt
```

成功退出表示完整响应与本地校验通过；退出码 2 表示保留的识别稿仍存在问题。工具不输出 key 或推理内容。Windows 新增的两个测试和探针已加入 Qt 依赖部署清单；Windows/MSVC、真实中文输入法及无开发工具的便携环境仍需实机验证。
