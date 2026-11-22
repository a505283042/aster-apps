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

**已经过实机验证的 USB MSC 模式可用**：Windows 识别卡、读写、弹出后返回设备桌面、APP 安装启动四项均由使用者确认。

本版不再把大型艺术歌词 `paper-serif.bin`、`paper-bold.bin` 装入 `.ast`（原来分别约 2.52 MB 和 1.77 MB），改为共享资源 **`/sd/aster/shared/hifi/fonts/`**。采用 SDK 官方支持的 `sd` 权限访问，首次从 SD 读取，字形加载和动画方式保持原状。为旧胖包保留 `/assets/text/` 回退路径；本版瘦包不含这两个备份文件，因此 **安装 alpha.5 前必须先把字库放到 SD 指定目录**。字库路径与传统 S31/FakePod 旧库索引无关。

### 一次性迁移（Windows USB MSC）

连接电脑进入 USB 读卡器模式，安全地得到某个盘符（示例 `E:`）：

```powershell
$sd = "E:\\"
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
cd D:\\ESP32\\aster-apps
.\\HiFiPlayer\\tools\\install_shared_fonts.ps1 -Drive E:\\
```

这里的 `E:\\` 仅是示例，请根据 Windows 实际分配的 TF 卡盘符替换。脚本不会格式化、删除文件或重新烧录固件。

### 安全和兼容性

- 无需重新烧录固件，不改播放器音频/封面/动态歌词算法、旧索引格式或现有 `/data` 内容。
- 正常外置字体优先从共享目录读取。若旧安装包还保留字体资源，会在共享字体不可用时尝试 /assets 路径。瘦包没有该回退内容；缺失时会显示正常的空字形/降级歌词并在串口打印 `HiFi paper font unavailable`，不应影响系统音频。
- 读入共享字体时，校验 ALV1 魔数、80 像素单元、字形索引范围、排序以及包偏移与总文件长度；允许空白字符使用零长度轮廓数据。
- 目前仍随包保留演示 FLAC（约 0.51 MB）、占位封面（约 0.077 MB）和 GBK 表（约 0.048 MB）。预期安装包总量从约 5.44 MB 降为约 1.15 MB，最终以你实际 `aster verify` 输出为准。
- **未经过当前用户机器的 SDK 编译／新版本的 S31 实机测试**。务必先复制字体、确认 Hash 一致，再安装 alpha.5；保留上一个能够正常使用的 alpha.4 安装包作为回退。
