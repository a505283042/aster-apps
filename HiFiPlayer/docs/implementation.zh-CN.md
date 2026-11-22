# HiFiPlayer 0.4.28 技术实现：独立 AOT APP、动态歌词、字形、封面和内存

> 本文描述 `HiFiPlayer` 在 `fix/hifi-unlimited-autoplay` 分支上的 **0.4.28** 实现，目标是 FAKE POD NANO PRO S31、460×460、RISC-V AOT、Aster ABI 4.5。与 [固件资源/TF 卡协同文档](https://github.com/a505283042/aster-firmware/blob/fix/hifi-unlimited-autoplay/docs/fakepod-s31-media-resource-implementation.zh-CN.md) 配套阅读。**不要把 APP 内自行实现的机制说成固件全部已支持，也不要把计划中的递归目录、音频 I/O 优先级说成现状。**

## 1. APP 与系统的职责边界

~~~text
HiFiPlayer AOT APP
 ├─ 桌面之外的 460×460 UI / 手势 / IMU / 播放器控制
 ├─ 曲库目录索引、LRC/FLAC 标签解析、封面查找
 ├─ 动态歌词排版、字形 Atlas、场景动画和局部重绘
 └─ Aster ABI 调用
      ├─ aster_player_control / aster_player_get_info  → 固件媒体服务
      ├─ aster_fs_* /assets /music /cache              → 固件文件与权限服务
      ├─ aster_surface_* / aster_ge_*                   → 固件图形服务
      └─ aster_time_now_ms / ASTER_EVENT_FRAME          → 固件时间和事件循环

固件媒体服务 → 解码 FLAC/MP3/WAV → I²S DMA / 蓝牙输出
~~~

**APP 不直接解码供耳机播放的音乐，也不直接驱动 CS43131。** APP 可以退出，但固件音乐会话不会因此自动停止；采用新版 `ASTER_PLAYER_CAP_AUTO_NEXT` 的固件还能在 APP 退出后自动开始下一首。文件读取与动态歌词属于 APP 的工作，依然会和系统播放共享 TF 卡。

关键入口：`src/main.c` 的 `aster_app_init()`、`aster_app_event()`、`aster_app_pause()`、`aster_app_resume()`、`aster_app_stop()` 和 `aster_app_destroy()`。在 `init` 创建 RGB565 surface、启动目录扫描、查询播放器、将帧率设置为 30 FPS；每个 `ASTER_EVENT_FRAME` 中按条件运行 `poll()`、`scan_tick()`、`lyrics_tick()`、`paper_tick()`、`cover_tick()`，仅在 `dirty` 为真时重绘。

## 2. 可安装、可卸载的 AOT 包

`app.toml` 定义：

| 字段 | 0.4.28 值 |
| --- | --- |
| `id` / `version` | `aster.hifi` / `0.4.28` |
| `abi` / `abi_min` | `4.5.0` / `4.5` |
| `aot` / `target` | `true` / `riscv` |
| `supported_boards` | `fakepod-s31` |
| `requires` | `display`、`touch`、`media-player` |
| `stack` | 16,384 B |
| `initial_memory` | 1,572,864 B（1.5 MiB） |
| `max_memory` | 2,097,152 B（2 MiB） |
| `heap` | 524,288 B，受 runtime/manifest 内存限制 |
| `orientation` | `portrait` |

`assets` 随安装包发布，含原创示例 `audio/demo.flac`、默认封面 `art/moonlight.bmp`、`text/gbk.bin` 和两套艺术字轮廓及许可证。独立 APP 的 `/assets` 虚拟路径指向已安装版本槽中的资源；不会把每个资源在启动时全部装入内存。

固件的双程序槽布局：

~~~text
/SD:/aster/apps/aster.hifi/
  .active0 / .active1
  .v0/  package.ast、app.aot、manifest.json、assets/...
  .v1/  package.ast、app.aot、manifest.json、assets/...
  data/
  cache/
~~~

升级写入备用槽，提交后切换激活记录；正常升级保留稳定的 `data/` 与 `cache/`。详情参见固件 `docs/package-installation.md`。

构建、检查及串口部署示例：

~~~sh
aster build HiFiPlayer --target riscv --aot
aster verify HiFiPlayer/dist/riscv/aster.hifi.ast --target riscv
aster deploy HiFiPlayer/dist/riscv/aster.hifi.ast --target riscv --port COM3 --run
~~~

上述命令需要已配置 Aster SDK；COM3 只是一种端口示例，须以实际设备串口为准。设备上也可将 `aster.hifi.ast` 放入 TF 卡后使用固件 shell 的 `aster app install /SD:/.../aster.hifi.ast`。升级前先退出正在运行的 APP。

## 3. WASM/AOT 内存：为什么 16 MiB PSRAM 不等于 APP 能随意申请

APP 按编译后的 WAMR 线性内存布局运行；运行时由固件在 PSRAM 中承载相应内存。`initial_memory`=1.5 MiB，`max_memory`=2 MiB 仅约束 APP 模块的相关内存，不包含系统的 LCD `snapshot`、媒体解码器、系统字库缓存等其他分配。

**这版 APP 采用 freestanding C 环境**，不能假定存在可用的标准 `malloc/realloc/free`。0.4.28 曾尝试标准动态分配但在 AOT 构建检查中失败，随后改为固定字节预算的**紧凑歌曲索引 Arena**；它并没有把 16 MiB PSRAM 暴露给 APP 当作无限堆。

重点区分：

| 对象 | 预算/说明 | 是否位于 APP 模块预算内 |
| --- | --- | --- |
| 曲库 `track_arena` | 384 KiB 固定静态区域 | 是 |
| 艺术字体 `atlas` | 36×80×80×1 = 230,400 B，约 225 KiB | 是 |
| 艺术字体双索引 | 2×8192×8 = 131,072 B，128 KiB | 是 |
| 封面输出像素 `pixels` | 208×208×2 = 86,528 B，约 84.5 KiB | 是 |
| JPEG workspace | 16 KiB（含 4 KiB read-ahead） | 是 |
| 460×460 RGB565 surface | 423,200 B，约 413.3 KiB | 通过模块 surface 分配，须纳入 APP 可用空间 |
| 系统 LCD `snapshot` | 423,200 B | 否，系统 PSRAM |
| 系统媒体 PCM/解码器 | 系统服务自己的预算 | 否，系统资源 |
| TF 上艺术字体原文件 | 单个数 MB | 否，仅按需读取数据 |

表中还有歌词文本、命令 arena、状态机、堆栈和其他数据，不能把列出的容量简单相加当成准确峰值。新增特效或更多缓存时首先要检查模块静态数据量、WASM memory、surface 分配和实际实机堆水位。

## 4. 音乐库：超越 64 首，但 0.4.28 不递归

`src/main.c`：

- `scan_start()` 打开虚拟路径 `/music`，清理本次索引并开始扫描。
- `scan_tick()` 每个 UI 帧最多枚举 **4 个**目录项，避免一次性扫描阻塞界面。
- 仅接受当前目录中的文件（`!is_dir`），且单个名字长度小于 248 字节；支持 MP3/FLAC/WAV 扩展名。
- 索引区总容量为 **384 KiB**：前端按顺序放 `uint32_t` 偏移量，后端倒序放 UTF-8 文件名。追加前同时检查索引和字符串是否相撞。
- `track_name(index)` 根据偏移取出文件名；单个名字空间长短不一，不再为每首固定保留 248 B。
- `tracks_clear()` 在重新扫描、退出时重置索引；它不会去释放不存在的独立 malloc 分配。
- 每页显示 5 首。`current_index()` 由当前系统路径反查目录扫描的索引。

因此，**64 首限制已解除，但仍有 384 KiB 的数据预算**；实际可容纳数量随文件名长度变化。`scan_tick()` **不进入子文件夹**，例如 `/music/周杰伦/稻香.flac` 当前不在曲库中。

播放列表顺序当前依赖 FAT 文件目录枚举。新增递归扫描需要同时扩展固件的后台下一首逻辑，不能只修改 APP，否则退出 APP 后跨目录接续会失败。

## 5. 歌词的读取、优先级与同步

主要模块：`src/lrc.c`、`src/flac_lyrics.c`、`src/main.c`。

选择顺序：**同名 `.lrc` → FLAC 内嵌带时间戳歌词 → FLAC 纯文本歌词**（未取得歌词则显示状态提示）。

- `lrc.c` 解析 UTF-8、UTF-16 LE/BE 和 GBK，使用随包 `/assets/text/gbk.bin` 进行需要的编码转换。
- 解析输出到固定容量的 `lrc_t`、文本缓冲、时间戳数组与单词高亮信息，绘制时不需要每帧重新读取 LRC。
- 根据 `aster_player_get_info()` 获取系统播放时间；`poll()` 在动态艺术歌词页每约 **50 ms**、其他页面每约 **150 ms** 更新播放器快照。
- `LYRIC_MAX_LEAD_MS=100`：针对显示计算/提交所用时间进行有限前瞻和补偿；实际歌词动画仍依据系统**绝对播放位置**定位，避免渲染变慢时累积漂移。
- 普通 LRC 在无逐字时间戳时估算行内进度；增强歌词使用对应词时间信息；纯文本没有可依赖的时间轴，因此支持上下翻阅。
- 蓝牙接收时歌词来自手机 AVRCP/相关系统提供的文本与状态，支持程度取决于手机播放器，并非设备可以从任意手机 APP 提取完整 LRC。

## 6. 全屏动态歌词的设计（不是 MJPEG 视频）

`src/paper.c`、`src/lyric_layout.c`、`src/paper_font.c`：

- 三个效果：`CUT_PAPER`（剪纸换幕）、`FOLD_PLANES`（折面换向）、`CAMERA_PUSH`（整句推镜）；每种含 6 个构图变体。
- 字符拥有 codepoint、位置、字号、角度、演唱进度等属性；逐帧根据时间计算位移、缩放、旋转、阴影、透视感和逐字/逐词着色。
- APP 不是逐帧从 TF 读取视频图片，而是重用当前句的字形蒙版，绘制到 RGB565 surface。
- 在转场/镜头未稳定时可全屏或大范围重绘；画面 `settled_frame` 后，`paper_draw()` 只定位高亮发生变化的字符/词的边界，调用 `aster_surface_flush()` **局部更新**。
- 复杂英文按单词组织，中文行按规则排版。超过艺术字体布局上限或遇到无法加载的字形时由系统字体路径兜底。
- 背景几何形状、光效和碎片由绘图计算生成。它们会消耗 CPU，但不会每帧消耗新的 TF 数据。

注意：`aster_app_set_framerate(30)` 是**请求的最大目标节奏**，不是已实测稳定 30FPS；高负载下应同时观测渲染耗时、刷新区域、播放是否断续。

## 7. 艺术字体文件为什么数 MB，但运行中仅需少量字形

与固件系统 cpfont 不同，APP 自己的字体格式是 `ALV1`：

~~~text
/assets/text/paper-serif.bin
/assets/text/paper-bold.bin
        │
        ├─ 8 B 文件头 + 8 B/字形索引
        ├─ 二分查找 codepoint
        ├─ seek 到目标字形轮廓
        ├─ 一次最多读取 4 KiB packets
        ├─ 将轮廓扫描栅格化为 80×80 Alpha
        └─ 当前一行的去重 Atlas
                  ↓
          实时缩放/旋转/混色/局部重绘
~~~

具体实现：

1. `paper_font_open()` 在第一次使用时读取该字体的索引，之后复用常驻的双索引缓存（各最多 `8192×8=65536 B`）；字形轮廓仍在 TF 上。
2. `paper_font_line()` 遍历当前行字符，**相同 codepoint 共享一个 mask**。
3. `atlas` 是固定 `36×80×80=230400 B`。若当前行不同字形的数量使蒙版总空间超预算，会将 `cell` 降为更小尺寸；不会无限增长。
4. `paper_font_read()` 每个字形先定位轮廓，再最多取 4096 B packets，栅格化后存入 Atlas；当前行的后续动画帧不再次读字体。
5. 结束本行/换句时重建所需的蒙版。艺术字形加载仍可能和播放音频争用 TF，**只是在整句动画期间大幅降低重复读取**。
6. 字体二进制资源来自 Noto Serif SC / Noto Sans SC，构建流程 `tools/build_paper_font.py` 用 fontTools 提取轮廓；不能直接把任意 TTF 放进设备期待自动可用。

系统菜单和普通中文由固件的 `ui_XX.cpfont` 实现，使用另一套按字号 PSRAM LRU。两条字体实现不要混用容量数字。

## 8. 封面查找、解码与工作分片

模块：`src/cover.c`、`src/cover_jpeg.c`、`vendor/tjpgd/tjpgd.c`。

- 优先查找与歌曲同名的 JPG/JPEG/PNG/BMP；也支持 MP3/WAV ID3 和 FLAC 内嵌封面。
- 嵌入封面有显式大小上限 `MAX_EMBEDDED = 4 MiB`，并非所有超大图都无条件载入。
- baseline JPEG 使用 TJpgDec，`READ_AHEAD=4096`，`COVER_WORK_BYTES=16384`，按 MCU 步进；`cover_jpeg_step()` 每次最多使用约 10ms 的**目标时间片**（不代表文件 I/O 绝不会超过 10ms）。
- 目标输出的 `COVER_EDGE=208`，RGB565 约 84.5 KiB；不会把一张 4000×4000 的封面解成同等大小的 RGBA 全图。
- 非 JPEG 或无法由本地 JPEG 解码器处理的图片通过系统图片服务路径尝试；必要时使用 `/cache/hifi-cover.img` 中转图像数据。
- 选中另一首歌时取消旧工作并关闭文件；没有有效封面时改绘默认图形。

**封面分片是 UI 卡顿缓解手段，不代表所有 SD 操作已被统一限时。** 一旦底层卡出现几十毫秒的读取尾延迟，某个帧仍可能超时。

## 9. TF 卡竞争与 APP 侧“有意退让”的边界

| 活动 | 本 APP 采用的策略 | 尚未解决的问题 |
| --- | --- | --- |
| 曲库扫描 | 每帧最多 4 条目录项 | 目录枚举仍是同步操作 |
| LRC / FLAC 内嵌歌词 | 有界读取，解析成内存结构 | 切歌时仍有 TF I/O |
| 系统字体 | 由固件 cpfont 缓存 + 预热处理 | 冷字形读取仍可能阻塞 |
| 艺术字体 | 每句建立 Atlas，动画帧复用 | 换新句时突发字形随机读 |
| JPEG | 4 KiB read-ahead + 约 10ms MCU 步进 | 单次 SD 读取延迟不受 10ms 约束 |
| 绘图 | `dirty` 门控、稳定后局部刷新 | 全屏复杂特效仍消耗 CPU/PSRAM 带宽 |
| 音频 | APP 只发控制命令，不直接送 I²S | 系统本地媒体读盘仍可能受同卡读影响 |

固件底层有 4 KiB 内部 DMA bounce 并管理文件句柄，但**尚无全局 SD 请求优先级调度器**。正确的后续方向是系统音频读前缓存、水位监控以及字体/封面读取让路音频，而不是在 APP 中再做一个独立的、会与系统竞争的音频读取任务。

## 10. 系统播放队列兼容性

`automatic_next()` 先检查 `ASTER_PLAYER_CAP_AUTO_NEXT`：

- **新固件有该能力**：APP 不再自行发送自然 EOF 后的 `PLAY_FILE`；固件 `hifi_autoplay` 负责。退出 APP 依然有下一首。
- **旧固件无该能力**：APP 仍在窗口打开期间按自身曲库顺序继续下一首。
- **目前只处理 `/music` 当前层级**；后台固件也只扫描此目录，尚未实现跨子目录顺序播放。
- 只有一个可播放文件、试听示例、暂停/停止和手机蓝牙输入时，不按普通本地多曲循环处理。

APP 显示 `caps=255` 代表播放器能力位包含 AUTO_NEXT（bit 128），**不能仅根据这一条日志证明 SD 竞争已消除、字体缓存无缺字或 I²S 无欠载**。

## 11. 调试与验收

### 11.1 编译包与安装

~~~sh
aster build HiFiPlayer --target riscv --aot
aster verify HiFiPlayer/dist/riscv/aster.hifi.ast --target riscv
~~~

设备 shell 例子：

~~~text
aster app stop
aster app install /SD:/aster/install/aster.hifi.ast
aster app list
aster app run aster.hifi
~~~

验证版可用 GitHub Actions `hifi-0428-riscv.yml` 检查 AOT 构建、包校验、`tests/autoplay_test.c` 和 `tests/library_scan_test.c`。自动化/编译通过不代表 96 kHz + 动态歌词同时运行的实机音频连续性已经测量。

### 11.2 推荐实机回归矩阵

| 场景 | 通过标准 |
| --- | --- |
| 根目录 >64 首 | 可见 65、100、140 等条目，无错误中止扫描 |
| 63→64（零基） | 手动及自然 EOF 均能跨旧限制 |
| 末曲→首曲 | 最后一首结束正确回绕 |
| 退出 APP | 音频继续；固件自动下一首 |
| 中、英文与混排长句 | 无缺字、无越界、长句合理降级 |
| UTF-8/GBK/UTF-16 LRC、FLAC 标签 | 正确显示、seek 后重新同步 |
| 首次冷字形 + 96 kHz FLAC | 记录最大 TF 读时、真实断音与画面延迟 |
| JPEG/PNG/BMP/内嵌大封面 | 不崩溃、不无限占内存、可取消旧解码 |
| 启动→停止→启动数十次 | 无单调增长的内存泄漏、句柄全部关闭 |

实机日志如显示 `Cannot override max memory with value greater than module max memory`，应核对具体固件的 WAMR 配置、包清单和是否实际运行成功；不要单凭后续 `AOT ready` 直接推断其对内存行为没有影响。错误应另行定位，不属于文档范围内已经解决的内容。

## 12. 重要源码入口

- `app.toml`：ABI、目标架构、内存限制、权限、包内资源
- `src/main.c`：APP 生命周期、目录扫描、媒体状态、触摸、30 FPS 事件循环
- `src/lrc.c` / `src/flac_lyrics.c`：多编码歌词、同步/纯文本歌词
- `src/paper.c` / `src/lyric_layout.c`：三类动态歌词、逐字高亮、局部更新
- `src/paper_font.c` / `tools/build_paper_font.py`：ALV1 字形轮廓、索引、Atlas
- `src/cover.c` / `src/cover_jpeg.c`：嵌入封面、TJpgDec 增量解码
- `tests/autoplay_test.c` / `tests/library_scan_test.c`：播放器兼容、超 64 首扫描测试
- [固件侧实现：资源分配、SDMMC、cpfont、音频与屏幕调度](https://github.com/a505283042/aster-firmware/blob/fix/hifi-unlimited-autoplay/docs/fakepod-s31-media-resource-implementation.zh-CN.md)

> 文档维护原则：新增视觉效果时必须说明内存上限、CPU/帧时间、TF 读取时间片与失败降级；不得用“PSRAM 够大”代替容量和最坏时延验证。
