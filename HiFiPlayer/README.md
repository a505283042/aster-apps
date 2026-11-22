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
- 本地音乐：曲库读取 SD 卡 `music` 目录中的 MP3、WAV 和 FLAC。MP3 支持 32、44.1 及 48 kHz。曲库最多显示 64 首，可翻页选择，也可播放随包附带的原创试听旋律。
- 播放器打开时，本地曲目自然结束后按曲库顺序续播，末曲接回第一首。曲库只有一首或播放试听示例时，播完即停。退出到桌面后，系统播完当前曲目；手机蓝牙播放由手机管理队列。
- 蓝牙输出：用系统蓝牙的“搜索设备”连接耳机或音箱。打开本地曲目时，音乐使用当前已连接的系统输出。播放页用一行显示音源的格式、采样率和位深。
- 本地播放保留源采样率和位深（以服务协商结果为准）。蓝牙当前为 SBC，属于有损编码。
- 返回桌面保留后台音乐；切换模式会停止前一种源。当前不提供发送模式的耳机按键遥控或 AAC。
- 曲库为目录当前层，不递归子目录，最多 64 首。选中歌曲与播放模式按 APP ID 独立持久化；本地随机仅在当前已扫描的歌曲中选下一首，且避免连续重复同一首。安装包声明支持 `fakepod-s31` 并要求 `media-player`，额外申请 `kv` 权限存储上次选择。支持板型检查的固件会在启动时核对。

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

**已接入：** 基于 S31 V2 Catalog/Manifest 生命周期规则改写的 Aster C 曲库模块 `src/music_catalog.c`。它每帧在预算内扫描 `/music` 的子目录、过滤隐藏文件与 `_FakePod`、读取 MP3/FLAC/WAV、生成路径+文件大小的持久化索引；最多存储 16,384 首，UI 继续使用五行虚拟窗口、分页，不再有 64 首限制。

索引位于 APP 沙盒 `/data/hifi_catalog_a.bin` 和 `/data/hifi_catalog_b.bin`，即 TF 卡的 `aster/apps/aster.hifi/data/`；两个交替槽位有版本、generation、条目 CRC32、Header CRC32 和顺序无关的 path+size 变化摘要。新索引先写空闲槽、分帧校验刚写出的负载 CRC 后才切换；断电、扫描失败或文件写入失败时仍使用旧快照。已有索引会先用于展示，后台逐步完整扫描校验新增/删除路径，无变化时不提升 generation。

**限制与下一阶段：** 这是 Aster 格式，不直接覆盖原 S31 `/System/library` 的 V2 二进制；Aster 4.5 的 `aster_fs_stat` 没有 mtime，因此同路径且大小不变的文件原地覆盖可能不会触发快照变化；扫描顺序暂沿文件系统枚举顺序，排序/歌手/专辑/年代分组、完整播放上下文与随机历史、封面预热将在后续阶段移植。深层目录超过 8 层时扫描拒绝提交不完整快照。退出 APP 后系统播放器后台仍不提供完整的连续队列，需后续配套固件 API。没有使用 ESP-IDF 或 Zephyr 私有函数。

验证：`aster build HiFiPlayer --target riscv --aot`、`aster verify HiFiPlayer/dist/riscv/aster.hifi.ast`，再安装到 SD。观察串口 `HiFi catalog scan start cached=...` / `HiFi catalog progress=...` / `HiFi catalog committed tracks=...`。首次索引构建分帧完成，大容量音乐卡会比旧 64 首扫描更久；未通过实机验证前不要合并主分支。
