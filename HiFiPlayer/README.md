# Aster 音乐

Hi-Fi 播放器 0.5.0-alpha.1，按 FAKE POD NANO PRO 的 460×460 方屏设计，最低固件 ABI 4.5。MP3 播放需要带 MP3 解码的固件。在系统下拉设置连接蓝牙音箱后，支持系统输出切换的固件会将当前歌曲从有线耳机转到音箱，保留播放进度。

- **播放器主界面只有两种状态：**启动直接显示「460×460 压暗封面＋控件」，轻点非控件位置显示原色全屏封面；再轻点返回控件。没有选歌欢迎页，也没有磁带模式。
- **首次自动选中曲库第一首，不自动启动音频。**以后优先从本 APP 的 KV 持久存储恢复最后选择的歌曲；曲目已删除时改选当前曲库第一首。无封面时优先使用 `/assets/art/no_cover_artwork.jpg`，若未打包则读取 TF 卡 `/music/no_cover_artwork.jpg`，均没有才使用随包旧图 `art/moonlight.bmp`。两种画面由同一份 460×460 解码结果生成，控件切换不重新读取 TF 卡。
- 在播放页横滑：右滑进入普通歌词，左滑进入曲库；反向滑动或点右上角返回箭头可返回播放页。返回时恢复压暗封面控件并重新绘制整个封面；高亮封面模式允许在整个有效封面区域横滑。歌词和曲库是手势辅助视图，不是播放前的必经页面。
- 压暗控件按 FakePod_S31 的纵向布局排列：顶部曲目信息、中央圆形上一首／播放／下一首、进度条及时间、底部三态模式图标。已删除播放页音量条和静音按钮，音量统一使用系统顶部下拉面板调节。模式图标循环为顺序、单曲循环、随机播放（本地曲库），手机蓝牙输入时显示不可用标识。
- FAKE POD NANO PRO 上，在播放页、歌词页或曲库摇一摇，会随机进入「剪纸换幕」「折面换向」或「整句推镜」。再摇一次回到刚才的页面。连续两次进入会选到不同效果。每种效果有六组逐句构图，左滑可随时回播放页。
- 三种动效都整句显示英文，按单词换行。英文长句沿用艺术字，中英混排保留英文单词。增强 LRC 按时间戳推进高亮，普通 LRC 在句内估算；手机蓝牙发来的歌词按句换幕。艺术字库缺字时使用系统字体。
- 封面解码上限为 460×460，居中裁切铺满方屏。原色与压暗版本由同一张完成的图生成；切歌时继续显示当前封面，候选封面解码至独立 staging RGB565，成功后一次性替换，不在两首歌中间显示缺省封面、不预读下一首以免与音频争用 TF 卡。切换两种主页状态不会重新解码，触摸允许 32px 内的轻微漂移。
- 将同名的 JPG、JPEG、PNG 或 BMP 放在歌曲旁边即可指定封面。也支持读取 MP3 和 WAV 的 ID3 封面、FLAC 的内嵌封面。找不到可用封面时显示同色系的抽象图案。
- 本地歌曲旁边可以放同名的 `.lrc`。例如 `music/稻香-周杰伦.mp3` 配 `music/稻香-周杰伦.lrc`。支持 UTF-8、UTF-16 和 GBK，支持毫秒时间戳、同一行多个时间戳以及 `[offset]`。当前歌词按字宽换行，拖动进度条时预览目标句，暂停时保留当前句。GBK 映射表随包安装，显示使用系统字库。

- FLAC 也可以直接使用内嵌歌词。播放器优先读取同名 LRC，其次查找 FLAC 标签里的带时间戳歌词，再回退到纯文本歌词。带时间戳的歌词随播放、拖动进度及摇一摇动效同步；纯文本可在歌词页上下滑动翻阅，在动态歌词页上下滑动换句。
- 手机接收：开启系统蓝牙后，手机连接 `Aster-S31 Audio`。播放页自动显示手机提供的曲名及歌手或专辑，可播放、暂停及切歌。
- 手机通过蓝牙播放时，播放页和歌词页同步显示已播时间和曲目时长，未知时间显示为 `--:--`。要跳到指定时间，请在手机上拖动进度条。在 FAKE POD 上播放 SD 卡歌曲时，可直接拖动设备上的进度条。
- 手机支持 AVRCP Cover Art 时，播放页使用手机提供的专辑缩略图。在网易云音乐中开启「外接设备蓝牙时显示歌词」，右滑可查看手机当前发送的文字，摇一摇进入整句动态歌词。只发送歌名的播放器会一直显示歌名。
- 本地音乐：曲库读取 SD 卡 `music` 目录中的 MP3、WAV 和 FLAC。MP3 支持 32、44.1 及 48 kHz。曲库在此开发分支支持最多 16,384 首、递归目录扫描和分帧读取，可翻页选择，也可播放随包附带的原创试听旋律。
- 播放器打开时，本地曲目自然结束后按曲库顺序续播，末曲接回第一首。曲库只有一首或播放试听示例时，播完即停。退出到桌面后，系统播完当前曲目；手机蓝牙播放由手机管理队列。
- 蓝牙输出：用系统蓝牙的“搜索设备”连接耳机或音箱。打开本地曲目时，音乐使用当前已连接的系统输出。播放页用一行显示音源的格式、采样率和位深。
- 本地播放保留源采样率和位深（以服务协商结果为准）。蓝牙当前为 SBC，属于有损编码。
- 返回桌面保留后台音乐；切换模式会停止前一种源。当前不提供发送模式的耳机按键遥控或 AAC。
- 曲库使用递归目录与双槽持久索引，不再限制为单层目录和 64 首。选中歌曲与播放模式按 APP ID 独立持久化；本地随机仅在当前已扫描的歌曲中选下一首，且避免连续重复同一首。安装包声明支持 `fakepod-s31` 并要求 `media-player`，额外申请 `kv` 权限存储上次选择。支持板型检查的固件会在启动时核对。

构建：在配置好 SDK 的终端运行 `aster build HiFiPlayer --target riscv --aot`，串口安装使用 `aster deploy PACKAGE --target riscv --port COM14 --run`，实际端口以设备识别结果为准。

`audio/demo.flac` 是 48 kHz/24 位双声道原创试听旋律，`art/moonlight.bmp` 是原创默认封面，两者均采用 MIT 授权。

基线 JPEG 封面使用 ChaN 的 TJpgDec R0.03，播放器分帧读取和解码，内嵌 JPEG 直接从歌曲文件读取。源码及许可随工程保留。[TJpgDec 来源与许可](https://github.com/hello-myj/aster-firmware/blob/6a54e3e2294ac9a91ec4aecc3445fbf4cd405c81/modules/imgdec/vendor/tjpgd.c)

本地播放时，进度条支持整条触摸区域按下与拖动，拖动时预览目标时间，松手提交跳转；音乐自然结束后仍可拖到目标位置继续播放。手机蓝牙播放时，设备显示手机回报的时间，在手机播放器内定位。

## 动态歌词的设计参考与字体

「剪纸换幕」「折面换向」「整句推镜」的视觉设计参考了 Folia（chthollyphile/folia-major）的「商籁」和「凝彩」，尤其是随歌词切句改变画面构图、让文字参与转场的做法。Aster 针对 460×460 方屏用 C 独立实现了整句排版、逐词高亮和动效绘制。感谢 Folia 作者和贡献者。[Folia 项目](https://github.com/chthollyphile/folia-major) · [Folia 许可证](https://github.com/chthollyphile/folia-major/blob/main/LICENSE)

随 APP 分发的艺术字轮廓来自 Noto Serif SC（`paper-serif.bin`）和 Noto Sans SC（`paper-bold.bin`），均采用 SIL Open Font License 1.1。两份字体的版权声明和许可证随安装包提供。[Noto Serif SC 来源](https://github.com/google/fonts/tree/main/ofl/notoserifsc) · [衬线字体许可](text/paper-serif-OFL.txt) · [Noto Sans SC 来源](https://github.com/google/fonts/tree/main/ofl/notosanssc) · [粗体许可](text/paper-bold-OFL.txt)

重新生成字库时，构建脚本使用 fontTools 提取字体轮廓；设备端由播放器直接绘制这些轮廓。[字库生成脚本](tools/build_paper_font.py) · [fontTools 项目](https://github.com/fonttools/fonttools)


## 自定义无封面图（本次上传的 460×460 JPEG）

将用户提供的 `no_cover_artwork.jpg` 放入 TF 卡 `music/no_cover_artwork.jpg`，APP 即会在歌曲缺少专辑封面时使用它，不必更改歌曲文件名或修改固件。也可放入 `HiFiPlayer/art/no_cover_artwork.jpg` 并在本地 `app.toml` 的 `assets` 列表添加 `"art/no_cover_artwork.jpg"` 以随 APP 打包；若还未复制此文件，切勿将其加入资源列表，否则编译会提示找不到资源。旧的 `moonlight.bmp` 保留兼容。


## 0.4.31 原色封面显示修复

真机日志中 `HiFi cover view=bright ready=1` 表示触摸状态已切换且封面存在，不能据此判断屏幕已提交。原色封面状态现在完全跳过无控件的 GE 绘制，改走 SDK `aster_surface_present(surface)` 原生整屏提交；控件状态仍走 GE 命令绘制。无封面变化时即使播放时间/状态刷新，原色封面不重复整屏推送；解码成功、点击进入、返回 APP 时会重新提交。串口增加 `HiFi bright native present rc=... ready=...`，若 `cover_paint` 无法取得 Surface 则打印 `HiFi bright fallback rc=...`，用以区分 Surface 路径错误。


## S31 完整复刻第一阶段：Aster V2 文件曲库（0.5.0-alpha.1）

**保留：** 原有艺术歌词、动态歌词、摇一摇、封面解码及 Aster 系统播放器服务，本阶段不触碰固件及硬解接口。

**已接入：** 基于 S31 V2 Catalog/Manifest 生命周期规则改写的 Aster C 曲库模块 `src/music_catalog.c`。它每帧在预算内扫描 `/music` 的子目录、过滤隐藏文件与 `_FakePod`、读取 MP3/FLAC/WAV、生成路径+文件大小的持久化索引；最多存储 16,384 首，UI 继续使用五行虚拟窗口、分页，不再有 64 首限制。路径查询使用每首 4 字节的内存摘要，启动时分帧分批读取旧快照，避免每次切歌都对数千条记录进行 TF 卡随机查找。

索引位于 APP 沙盒 `/data/hifi_catalog_a.bin` 和 `/data/hifi_catalog_b.bin`，即 TF 卡的 `aster/apps/aster.hifi/data/`；两个交替槽位有版本、generation、条目 CRC32、Header CRC32 和顺序无关的 path+size 变化摘要。新索引先写空闲槽、分帧校验刚写出的负载 CRC 后才切换；断电、扫描失败或文件写入失败时仍使用旧快照。已有索引会先用于展示，后台逐步完整扫描校验新增/删除路径，无变化时不提升 generation。

**限制与下一阶段：** 这是 Aster 格式，不直接覆盖原 S31 `/System/library` 的 V2 二进制；Aster 4.5 的 `aster_fs_stat` 没有 mtime，因此同路径且大小不变的文件原地覆盖可能不会触发快照变化；扫描顺序暂沿文件系统枚举顺序，排序/歌手/专辑/年代分组、完整播放上下文与随机历史、封面预热将在后续阶段移植。Aster 固件当前最多同时打开 4 个目录句柄，路径 API 限制为 160 字节，故本阶段最多扫描 4 层目录、单条相对路径不超过 151 个 UTF-8 字节；超深目录扫描会拒绝提交不完整快照，过长路径会跳过。退出 APP 后系统播放器后台仍不提供完整的连续队列，需后续配套固件 API。没有使用 ESP-IDF 或 Zephyr 私有函数。

验证：`aster build HiFiPlayer --target riscv --aot`、`aster verify HiFiPlayer/dist/riscv/aster.hifi.ast`，再安装到 SD。观察串口 `HiFi catalog scan start cached=...` / `HiFi catalog progress=...` / `HiFi catalog committed tracks=...`。首次索引构建分帧完成，大容量音乐卡会比旧 64 首扫描更久；未通过实机验证前不要合并主分支。


## 0.5.0-alpha.2：S31 风格文件夹浏览（开发中）

- 曲库页面新增「全部歌曲 / 文件夹」两种有效浏览范围。文件夹层级按歌曲索引中的路径分组，支持进入多级目录、左上角返回上级、在任意文件夹中直接选歌。文件夹始终排在歌曲前面，右侧显示包含的歌曲总数（含子目录）。
- 文件夹列表采用 **按需、逐帧构建**：每次处理至多 12 个目录索引项或约 5 ms，不额外打开 TF 卡目录句柄；仅存歌曲 Catalog ID 与当级文件夹名称，使用固定约 124 KiB 内存索引，避免与音频 Task 抢占文件系统锁。
- 原色封面、压暗控件、歌词、摇一摇、持久索引和现有音频逻辑均不改动。原曲库「试听示例」按钮被两个范围选项替代；播放样例文件仍可通过其他原有逻辑调用。
- **尚未完成**：歌手／专辑／年代分类必须基于 MP3 ID3 和 FLAC Vorbis Comment 标签建立 Metadata Catalog，本次没有用文件夹名假冒音频标签；文件夹内连续播放的队列上下文与排序也仍待移植。S31 原版会将目录排在文件前并忽略隐藏项；此版本先保留源索引的顺序，不强行对磁盘路径进行大量随机访问排序。
- 实机核验：编译安装后右滑或进入曲库，切换「文件夹」，先看根目录，再进入有音乐的子文件夹，播放一首，点击右上角返回上级。退出重进，检查原色封面和歌词是否保持正常。


## 0.5.0-alpha.3：MP3／FLAC 音乐标签分类（初始移植）

- 新增 `music_tags.c`，参考 FakePod_S31 `media_metadata.cpp` 的字段优先级，从 MP3 ID3v2.2/v2.3/v2.4（及 ID3v1 fallback）、FLAC Vorbis Comment 中提取艺术家、专辑、专辑艺术家、发布日期、原始年份。支持 UTF-8、Latin-1、UTF-16（含 BOM），跳过 APIC / FLAC PICTURE 的大体积封面数据，不在扫描器中读取歌词正文。
- 新增 `music_groups.c`，真正按元数据建立「歌手」「专辑」「年代」列表：专辑键由专辑名 + Album Artist（缺失时回退 Artist）组成；Album Artist 也加入歌手分组的歌曲关联中；年代优先 Original Year，再回退 Release Year；无标签时归入未知项，绝不使用文件夹名冒充标签。
- 曲库增加五个浏览范围「全部／歌手／专辑／年代／文件夹」。点击歌手／专辑／年代进入组内歌曲；点击组内歌曲播放。保持原色封面、动态歌词、系统播放器逻辑不改动。
- 顶栏「重扫」必须 4 秒内连点两次才触发：只删除 `/data/hifi_catalog_[ab].bin`，重新扫描音乐目录，并重建标签分组，不触碰音频／歌词／封面／已安装资源。使用者不必再取出 TF 卡删索引。
- 分组扫描以曲库索引生成完成为前提，每次最多分析一首歌；空闲状态间隔约 26 ms，**正在播放音乐时只有打开曲库页面才扫描**，且间隔约 240 ms，减少封面／艺术歌词页的额外 TF 卡 I/O。播放中如需更快完成标签扫描，可暂停音乐后等待分类完成。
- **限制**：本版只是 UI 分类能力的初始移植，当前组记录是运行时内存结构，重新启动会重新读取标签；尚未持久化 S31 式完整 Metadata Catalog、未移植多歌手 ArtistRefs、完整 Unicode 排序和组内碟号／曲号顺序。对非常复杂的 ID3 标签（压缩、逐帧加密、非同步化等）仍可能识别不全，默认归入未知分类。最多可建立 3072 位歌手、4096 张专辑和 32 个年代分类；容量耗尽时显示错误而非静默提交错误索引。
- **当前状态**：已提交云端代码，尚未使用你的 Windows Aster SDK 编译或实机测试。请先编译，验证 v0.5.0-alpha.3，再安装试用。原有已通过实机验证的 alpha.1 可以作为回退基线。


## 0.5.0-alpha.4：统一全局队列决策 + Metadata Group 双槽缓存

### 已确定：不实现分类专属的播放队列

「全部／歌手／专辑／年代／文件夹」只是曲库浏览入口；选中一首歌后，使用唯一的全局曲目 ID，后一首的选择不能被浏览时所处的分类控制。分类详情中的 `selected_tracks[]` 只是 UI 列表到全局 Catalog ID 的映射，绝不是第二套播放队列。这样减少切歌逻辑分叉并避免 APP/固件两端队列同步。未来全局播放顺序规划为：顺序停止／全部循环／单曲循环／全局随机，随机播放历史也仅维护一份。

**尚未统一后台自动切歌**：Aster 目前 APP 有前台自然结束处理，而固件声明 `ASTER_PLAYER_CAP_AUTO_NEXT`，固件现有后台任务只在 `/music` 根目录寻找后续曲目，尚不支持嵌套文件夹及全局随机/循环模式。本版没有改变固件播放器或其音频任务。后台统一队列应在固件 API 扩展后再实现，不得宣称当前后台自动下一首已完全正确。

### 已实现：Metadata Group 持久化

- 首次完成真实 MP3/FLAC 标签扫描与分组后，将实体名称字符串池、Artist/Album/Decade 分组表、Track ID 对应关系保存到 APP 专有的 `/data/hifi_groups_a.bin` 和 `/data/hifi_groups_b.bin`。并不是 S31 原生 `/System/library` 格式。
- 快照 Header 带 schema version、曲库 Catalog generation、曲目数、分组数、字符串池长度、payload CRC32、header CRC32；数据区分帧 4KB 写入、4KB 回读校验，最后才标记有效 Header。缓存损坏或 Catalog 发生更改时自动重新读取标签。
- 重启曲库扫描完成后先尝试加载匹配代际的 Group 缓存，再决定是否重新读取 MP3/FLAC 标签。相同路径和文件大小但标签原地改变的情形仍受 Catalog signature 检测限制，可通过 APP 顶栏二次确认的「重扫」强制删除 Catalog 和 Group 双份缓存并重建。
- 当前播放音乐时不启动 Group 缓存写入/校验；如果扫描刚好完成于播放中，则写盘推迟到播放暂停或结束后，避免额外 TF 卡写流量干扰音频。
- **版本状态**：alpha.4 是独立实验版本，未运行 Aster SDK 编译或实机测试。请先编译，确保输出包含 `v0.5.0-alpha.4`，再验证设备启动日志 `HiFi groups cache committed` 与二次启动 `HiFi groups restored cached`。保留已验证的 alpha.1 稳定回退包。


## 0.5.0-alpha.5：艺术歌词字库共享，瘦身 APP 安装包
**推荐：一次 USB 会话完成全部更新。**先更新和编译 APP，再进入 USB 读卡器模式，复制字库和安装包一次性安全弹出，无需重复拔插 TF 卡。

```powershell
. "D:\ESP32\aster-sdk-4.5.0\env.ps1"
cd D:\ESP32\aster-apps
git pull --ff-only origin feat/hifi-s31-full-port
aster build HiFiPlayer --target riscv --aot
aster verify HiFiPlayer/dist/riscv/aster.hifi.ast
```

设备进入「USB 读卡器」后，假设 Windows 盘符为 `E:\`：

```powershell
cd D:\ESP32\aster-apps
.\HiFiPlayer\tools\install_shared_fonts.ps1 -Drive E:\ -Package .\HiFiPlayer\dist\riscv\aster.hifi.ast
```

脚本会检查真实 `aster` 卡目录，把字体与许可证复制到共享目录，并以 SHA256 校验。随后复制同一版 AST 安装包到 `aster/install`，验证完毕后请在 Windows 安全弹出，再从设备端安装 `/SD:/aster/install/aster.hifi.ast`。以后更新 APP 再运行同一脚本，未变化的字库会跳过。


**已经过实机验证的 USB MSC 模式可用**：Windows 识别卡、读写、弹出后返回设备桌面、APP 安装启动四项均由使用者确认。

本版不再把大型艺术歌词 `paper-serif.bin`、`paper-bold.bin` 装入 `.ast`（原来分别约 2.52 MB 和 1.77 MB），改为共享资源 **`/sd/aster/shared/hifi/fonts/`**。采用 SDK 官方支持的 `sd` 权限访问，首次从 SD 读取，字形加载和动画方式保持原状。为旧胖包保留 `/assets/text/` 回退路径；本版瘦包不含这两个备份文件，因此 **安装 alpha.5 前必须先把字库放到 SD 指定目录**。字库路径与传统 S31/FakePod 旧库索引无关。

### 一次性迁移（Windows USB MSC）

连接电脑进入 USB 读卡器模式，安全地得到某个盘符（示例 `E:`）：

```powershell
$sd = "E:\"
$source = "D:\ESP32\aster-apps\HiFiPlayer\text"
$dest = Join-Path $sd "aster\shared\hifi\fonts"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $source "paper-serif.bin") -Destination $dest -Force
Copy-Item (Join-Path $source "paper-bold.bin") -Destination $dest -Force
Copy-Item (Join-Path $source "paper-serif-OFL.txt") -Destination $dest -Force
Copy-Item (Join-Path $source "paper-bold-OFL.txt") -Destination $dest -Force
Get-Item (Join-Path $dest "paper-serif.bin"), (Join-Path $dest "paper-bold.bin") |
    Select-Object Name,Length
```

预期共享 ALV1 文件大小（来自当前仓库资源版本）：`paper-serif.bin` **2,518,748 B**；`paper-bold.bin` **1,766,506 B**。源和目标可分别用 `Get-FileHash -Algorithm SHA256` 验证相同。请把许可证 `paper-*-OFL.txt` 同目录拷贝保存，避免单独分发字库时丢失许可信息。

完成拷贝、Windows 安全弹出且设备恢复正常模式后，更新源码并编译：

```powershell
. "D:\ESP32\aster-sdk-4.5.0\env.ps1"
cd D:\ESP32\aster-apps
git pull --ff-only origin feat/hifi-s31-full-port
aster build HiFiPlayer --target riscv --aot
aster verify HiFiPlayer/dist/riscv/aster.hifi.ast
```

随后再次进入 USB 读卡器模式，复制新 `HiFiPlayer/dist/riscv/aster.hifi.ast` 到 `<盘符>:\aster\install\aster.hifi.ast`，安全弹出后通过设备 shell `aster app install /SD:/aster/install/aster.hifi.ast` 安装。

也可以只执行项目里新增的一条 PowerShell 命令（内置 **SHA256 校验**，并会一起复制字体许可证；拷贝前检查是否真的是带 `aster` 目录的 SD 盘）：

```powershell
cd D:\ESP32\aster-apps
.\HiFiPlayer\tools\install_shared_fonts.ps1 -Drive E:\
```

这里的 `E:\` 仅是示例，请根据 Windows 实际分配的 TF 卡盘符替换。脚本不会格式化、删除文件或重新烧录固件。

### 安全和兼容性

- 无需重新烧录固件，不改播放器音频/封面/动态歌词算法、旧索引格式或现有 `/data` 内容。
- 正常外置字体优先从共享目录读取。若旧安装包还保留字体资源，会在共享字体不可用时尝试 /assets 路径。瘦包没有该回退内容；缺失时会显示正常的空字形/降级歌词并在串口打印 `HiFi paper font unavailable`，不应影响系统音频。
- 读入共享字体时，校验 ALV1 魔数、80 像素单元、字形索引范围、排序以及包偏移与总文件长度；允许空白字符使用零长度轮廓数据。
- 目前仍随包保留演示 FLAC（约 0.51 MB）、占位封面（约 0.077 MB）和 GBK 表（约 0.048 MB）。预期安装包总量从约 5.44 MB 降为约 1.15 MB，最终以你实际 `aster verify` 输出为准。
- **未经过当前用户机器的 SDK 编译／新版本的 S31 实机测试**。务必先复制字体、确认 Hash 一致，再安装 alpha.5；保留上一个能够正常使用的 alpha.4 安装包作为回退。


## 0.5.0-alpha.6：系统唯一播放队列与四种播放模式（需配套固件）

- 配套固件实验分支：`a505283042/aster-firmware` → `feat/s31-hifi-global-playback`，基于**已经实机验证 USB MSC 的** `feat/fakepod-s31-usb-msc` 建立。详情参见固件分支专属文档；这是**两个仓库配套**的改动，不能只将 APP 升级后就宣称后台随机可用。
- APP 取消自然结束时与固件抢切歌：只要固件有 `ASTER_PLAYER_CAP_AUTO_NEXT`，APP 就不再执行自己的 `automatic_next`。APP 不创建歌手／专辑／年代／文件夹队列；当前播放仅使用完整 HIF2 Catalog 的全局曲目 ID。
- 固件新增可选能力 `ASTER_PLAYER_CAP_QUEUE_MODE=256`、可选命令 `ASTER_PLAYER_SET_QUEUE_MODE=14`；重用原先 Player Info `reserved[3]` 回报 mode。仍是原 ABI 4.5 的请求/回报结构，alpha.6 可以使用已安装 SDK 4.5.0 编译，能力检测决定是否发送新命令。
- 播放模式按顺序循环选择：**顺序停止(0) → 全部循环(1) → 单曲循环(2) → 全局随机(3)**。新固件的模式选择、自动下一首和按键上一首／下一首均由固件控制；模式由固件 NVS 持久化，APP 只负责回显。
- **向后兼容**：旧固件若只有 `CAP_AUTO_NEXT`，模式按钮显示「固件」，点击解释「当前固件尚不支持四种后台模式」，不伪装已切换成功。没有固件自动切歌能力的极老平台，APP 使用自己的回退队列（四种模式，KV `play_mode_v2`）。
- **保持原样**：v0.5.0-alpha.5 的 TF 共享艺术字（无需重拷）、460x460 原色/压暗封面、歌词和界面分组、双槽 Catalog/Metadata 缓存、蓝牙收发、音频解码线程、I2S 与 TF 访问方式（仅固件后台 EOF 时索引访问改变）。
- **未验证声明**：上述代码已在云端开发分支提交并经过源码级检查，尚未通过 ESP-IDF 6.1.0 / Aster SDK 4.5.0 编译和真机测试。请**先编译 APP，不烧录未构建的固件**；保留用户刚实测通过的 alpha.5 安装包与 USB MSC 固件作为回退基线。测试中如出现新卡顿，先回退固件并分析后台状态。


## 0.5.0-alpha.7：首次建库、HFD1 持久目录索引、无重复全盘扫描

**当前仅云端开发版；尚未经过 Windows Aster SDK 编译或 S31 真机测试。** 使用独立分支 `feat/hifi-s31-indexed-folders`，以 alpha.6 为基线。配套 S31 实验固件 `feat/s31-hifi-global-playback` 不需要为本 APP 变更而重新刷写。

### 行为和职责

- **已有有效 HIF2 Catalog：APP 启动直接恢复旧快照**，不再执行每次启动的递归 `/music` 全盘扫描。即使没有 HFD1，也可以直接从 HIF2 全曲目视图播放。
- **首次安装、没有有效 HIF2：自动创建基础曲库**，在完成 CRC 校验和双槽发布前不开放本地曲库选歌/自动队列。试听 Demo 和手机蓝牙接收仍独立。首次 UI 显示已发现歌曲数、文件夹数；总数未知时不伪造百分比。文件扫描完成后显示已校验记录数和真实进度条。
- **HFD1 文件夹目录索引**：从已发布的 HIF2 生成父子目录节点和子项引用，全部使用全局 Track ID（不创建新播放队列）。每级目录的歌曲引用连续存放，打开文件夹只用内存目录结构；读取当前页真实歌曲名时仍需访问最多五条 HIF2 记录，不再遍历整个全曲库。
- HFD1 存储为 `/data/hifi_folders_a.bin` 和 `/data/hifi_folders_b.bin`，每份带 `magic/version`、HIF2 generation、曲目总量、目录数、Header CRC32、payload CRC32。空闲后台分块生成/回读校验；仅在 payload 验证成功后写入有效 Header。系统异常或断电时，损坏缓存会重新从 HIF2 建立，不会删除 HIF2 音乐记录。
- APP 启动按代数恢复 HFD1，按块校验后一次进入可浏览状态。若 HIF2 重扫发布了新一代，文件夹目录缓存自动失效并重建。歌手／专辑／年代仍使用原双槽标签分组缓存。
- 「重扫」继续 4 秒内二次确认，**只在用户确认后重新检查 TF 卡**；有效 HIF2 旧代保留供播放和浏览，校验通过后才原子切换新代。扫描失败不会事先删除旧 HIF2 或标签缓存。请注意：**USB 新增、删除歌曲后需要手动点击「重扫」**，本版不依赖未经实现的自动文件变更通知。
- 音乐播放中暂停 HIF2/HFD1 的后台存储扫描和大量 CRC 读取，避免 UI 额外存储压力。首次 HFD1 生成过程中如已在播音乐，文件夹页将提示「暂停音乐后继续加载目录索引」。已完成的目录树在 RAM 中切换，不需要新增磁盘读取。
- HFD1 本版最多记录 **1024 个有歌曲的目录**（含 `/music` 根目录），最多 **16,384 首歌曲**，并严格继承 HIF2 对文件路径长度与目录深度的限制。达到上限会明确报错，而不是悄悄丢弃部分文件。
- 新目录索引约额外使用 **280–300 KiB** 的静态 WASM 内存；APP 预算提升至 `initial_memory=4 MiB`、`max_memory=5 MiB`。这仅影响 APP，不改已有音频解码器、艺术歌词字库、封面双状态、USB MSC、固件分区。可继续沿用 alpha.5 建立的 TF 共享字体。
- `tests/test_folder_index.py` 在具备本机 C 编译器时通过独立临时目录，编译真实 `music_browser.c`，验证多级文件夹、全局歌曲 ID、已提交快照重载与 HIF2 generation 变更后目录重建。此测试代码已提交，但尚未在当前对话环境执行。

### Windows 测试与升级

先保留 alpha.6 已确认可运行的 AST 文件。不要覆盖其它已稳定 APP 和固件。将测试分支检出到 **新的本地分支**：

```powershell
cd D:\ESP32\aster-apps
git fetch origin feat/hifi-s31-indexed-folders
git switch -c feat/hifi-s31-indexed-folders origin/feat/hifi-s31-indexed-folders
. "D:\ESP32\aster-sdk-4.5.0\env.ps1"

aster build HiFiPlayer --target riscv --aot
if ($LASTEXITCODE -ne 0) { throw "alpha.7 build failed" }
aster verify HiFiPlayer/dist/riscv/aster.hifi.ast
```

**务必确认** `aster verify` 显示 `ver=0.5.0-alpha.7`，而不是在失败构建后误以为缓存的旧 AST 是新包。已经建立的 `/sd/aster/shared/hifi/fonts/` 无需重复复制。

安装时沿用已验证的 USB MSC 模式：先将 APP 包拷贝到 TF 卡 `aster/install/aster.hifi.ast`，Windows 安全弹出，再使用 `aster shell --console COM3 aster app install /SD:/aster/install/aster.hifi.ast`（实际 COM 以设备端口为准）。**本轮不需要重刷固件。**

验证重点：从 APP 退出重进，不再打印 `HiFi catalog scan start cached=...`，而是 `HiFi catalog restored tracks=... (no rescan)`；首次打开文件夹只生成 HFD1 一次，之后多层快速切换；手动重扫后旧曲库保留到提交新代；初次空卡创建索引时显示歌曲／文件夹数量和 CRC 验证进度；播放状态下无多余的曲库后台扫描。

### 当前版本限制

- HIF2 的文件大小/相对路径指纹无法检测“同一路径、同样大小但内容已替换”的修改；这类情况下 HIF2 generation 可能不变化，标签和文件夹缓存沿用旧 generation。需要进一步元数据/mtime 能力解决。
- HIF2 的旧索引恢复仍基于已有 Header CRC 与文件尺寸检查，未新增“每次启动全表读取 CRC”步骤。HFD1 则会从磁盘分块计算 payload CRC；这是刻意避免每次重启都全盘读取音乐文件。
- 尚未进行 SDK 4.5.0 的最终编译/实机与 USB MSC 回归验收。在确认 alpha.7 通过前，保留 alpha.6 作为回退安装包。


## 0.5.0-alpha.8：460×460 V1 控件页（先于硬件加速）

本版只更改 HiFiPlayer APP，不修改音频服务或 FLUX 桌面：

- 保持全屏原色封面与「压暗封面＋控件」两个视觉状态；点击空白封面切换。
- 上方显示当前全局 HIF2 曲目序号与总数、音频格式、实际媒体服务报告的采样率（kHz）及位深。队列索引未准备好时不编造序号；蓝牙接收时不显示本地队列序号。
- 采用 FakePod_S31 风格自绘线条图标：顺序／列表循环／单曲循环／随机。蓝牙接收时以 X 表示手机控制。
- 播放进度条下移至 y=286，时间标记 y=306；命中区域同步移动，避免误触时间文字。
- 底部左右对称的 60×60 按钮：左侧播放模式、右侧「列表」。列表按钮固定进入全部歌曲视图，按当前全局 Catalog ID 定位到对应五行分页，而不是切换到某个浏览分类下的临时队列。
- 最新底部布局改为三段式：左侧 60×60 播放模式按钮，中间 168×60 单一音频来源显示框，右侧 60×60 歌曲列表按钮。中间只按真实播放器状态显示「有线」「蓝牙接收」「蓝牙发射」中的一个，不再显示四格常驻状态栏，也不是切换输出的按钮。操作错误/连接提示保留原来的临时浮层；音量继续只由系统下拉菜单调整。
- 触摸滑动、摇一摇艺术歌词、系统队列模式及后台播放逻辑维持原样。
- alpha.8 UI 微调：曲目位置不再补四位前导零，按真实位数显示 `153 / 372`、`128 / 4227`；上一首和下一首圆心从 x=132/328 调整到 x=116/344，与中间播放按钮的圆周间距由 14px 增大为 30px，命中区域同步移动；底部中间使用只读的动态音频来源标签，不增加音量或歌词按钮。

源码级回归测试：`python -m unittest HiFiPlayer/tests/test_controls_v1.py`。

本版 **没有**启用新增硬件 JPEG、PPA Blend、DMA2D、QSPI/PSRAM 直传加速；这些能力按固件驱动＋SDK 可选接口单独实施，不将 FakePod_S31 的私有 Panel IO 结构直接复制进 Aster。真机音频连续性与触控测试仍需验收。


## alpha.8 修复：下一曲切换时短暂闪回列表选曲

固件拥有全局 HIF2 队列时，手动 NEXT/PREV 会先 STOP 当前解码器，再在低优先级任务中查索引并开始下一首。中间出现的短暂 `STOPPING → IDLE` 是正常的媒体状态过渡；媒体 `path` 仍是刚才播放的曲目，而 APP 旧的 `display_track()` 会在 `IDLE` 回退到之前从列表中明确选择的 `selected_file`。因而每次切歌时可能短暂显示最初点选的歌名／封面／歌词，随后才显示新曲，视觉上像跳了两首。

修复后新增仅驻留 APP RAM 的 `confirmed_media_track`：只在媒体实际报告 `OPENING / PLAYING / PAUSED` 且路径有效时记录；短暂 `IDLE / ENDED` 时仅对同一个真实媒体路径维持显示。列表明确选新歌时清除该缓存，仍优先尊重用户的新选择；蓝牙与本地模式切换时清除缓存。固件 NEXT/PREV、后台队列、音量、封面解码和 HIF2 磁盘内容均未修改；没有新增 SD 或 KV 写入。源码回归检查已加入 `tests/test_controls_v1.py`。如果实机中是**声音**真实播出了另外一首，而不仅是封面／歌名闪回，应另采集 `hifi_autoplay: manual:` 与 `aster_media` 相关日志分析固件的实际播放调度。
