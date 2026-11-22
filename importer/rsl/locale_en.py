"""English messages for desktop importer interfaces."""

MESSAGES = {' 等 %d 个': ' and %d more',
 "未找到 Ren'Py 游戏目录（需要含 .rpy/.rpyc/.rpa 文件的 game/）": "No Ren'Py game folder found (needs a game/ with "
                                                   '.rpy/.rpyc/.rpa files)',
 "--recompile 仅适用于 Python 2 游戏（Ren'Py 7 或更旧）": "--recompile only works for Python 2 games (Ren'Py 7 or older)",
 '存在 game/python-packages：必须是纯 Python 3.12，并避免使用 ctypes、ssl、sqlite3、mmap、subprocess、multiprocessing': 'game/python-packages '
                                                                                                      'is '
                                                                                                      'present: '
                                                                                                      'it must be '
                                                                                                      'pure '
                                                                                                      'Python '
                                                                                                      '3.12, and '
                                                                                                      'avoid '
                                                                                                      'ctypes, '
                                                                                                      'ssl, '
                                                                                                      'sqlite3, '
                                                                                                      'mmap, '
                                                                                                      'subprocess '
                                                                                                      'and '
                                                                                                      'multiprocessing',
 '%d 个视频高于 %d fps（%s）：以相同尺寸重编码为 %d fps，可减少 Switch 的 CPU 负担和发热': '%d videos are above %d fps (%s): re-encoding '
                                                                'them to %d fps at the same size reduces the '
                                                                "Switch's CPU load and heat",
 '  来源：  %s %s': '  Source:  %s %s',
 '  状态：  %s': '  Status:  %s',
 '  文件：   game/ 中 %d 个，%.1f MB（rpa %d，rpy %d，rpyc %d，rpym %d）': '  Files:   %d in game/, %.1f MB (rpa %d, rpy %d, '
                                                                'rpyc %d, rpym %d)',
 '  媒体：   %s': '  Media:   %s',
 '  图片： %s': '  Art:     %s',
 '  已检查：%d 个散装 + %d 个压缩包内图像/视频；%d 个无法读取的压缩包': '  Checked: %d loose + %d images/videos in archives; %d unreadable '
                                              'archives',
 '  Switch 性能：%s': '  Switch performance: %s',
 '  建议：--%s：%s': '  Suggested: --%s: %s',
 "  Ren'Py:  %s   Python：%s": "  Ren'Py:  %s   Python: %s",
 '（运行时 ': ' (runtime ',
 '）': ')',
 '跳过了 %d 个链接或特殊文件：%s': 'Skipped %d links or special files: %s',
 '--unrpa 需要一个暂存目录（命令行与窗口都会给出）': '--unrpa needs a staging folder (the command line and the window both provide '
                                 'one)',
 "%s 中没有 Ren'Py 脚本（.rpy/.rpyc/.rpa）": "No Ren'Py scripts in %s (.rpy/.rpyc/.rpa)",
 'Python 版本线索相互冲突：%s；采用 %s': 'Python version hints conflict: %s; using %s',
 '%s 指向 %s': '%s points to %s',
 "附带全部 .rpy 源码的 Python 2 游戏：Ren'Py 8.5.3 可以用 Python 3 编译它们，因此可以导出（实验性功能）": 'Python 2 game with all its .rpy '
                                                                           "sources: Ren'Py 8.5.3 can compile "
                                                                           'them with Python 3, so it can be '
                                                                           'exported (experimental)',
 '无法在 Switch 上加载的原生代码：%s；游戏一旦导入就会失败': 'Native code that the Switch cannot load: %s; the game will fail once '
                                      'imported',
 'game/ 中有 %d 个 Python 模块（%s）：与 python-packages 同样的限制': '%d Python modules in game/ (%s): the same restrictions '
                                                        'as python-packages',
 '压缩包头非标准：%s；游戏需要自己的加载器（未测试）': 'Non-standard archive header: %s; the game needs its own loader (untested)',
 "Ren'Py 无法播放 MIDI：%s": "Ren'Py cannot play MIDI: %s",
 '%d 个文件随 game/ 位于游戏文件夹中（%s），将导出到 game/ 旁边': '%d files sit in the game folder beside game/ (%s); they are '
                                             'exported beside game/',
 '--precompile 需要暂存目录': '--precompile needs a staging folder',
 '存在 2 GiB 或更大的文件（%s）：FAT32 SD 卡无法容纳 4 GiB 或更大的文件；请使用 exFAT': 'Files of 2 GiB or more exist (%s): a FAT32 SD card '
                                                              'cannot hold files of 4 GiB or more; use exFAT',
 '%d 个文件名含非 ASCII 字符（%s），将打包进 %s.rpa：Switch 的 SD 卡无法列出 这种名称（硬件运行 20）': '%d file names contain non-ASCII '
                                                                       'characters (%s); they are packed into '
                                                                       '%s.rpa: the Switch SD card cannot list '
                                                                       'such names (hardware run 20)',
 "脚本或压缩包名含非 ASCII 字符（%s）：Ren'Py 无法从压缩包加载它们，Switch 的 SD 卡也无法列出它们；游戏可能找不到这些文件": 'Script or archive names contain '
                                                                              "non-ASCII characters (%s): Ren'Py "
                                                                              'cannot load them from an archive, '
                                                                              'and the Switch SD card cannot list '
                                                                              'them; the game may not find these '
                                                                              'files',
 '无': 'None',
 '无；加载器将显示占位图': 'None; the loader shows a placeholder',
 '正常': 'OK',
 "Ren'Py 版本读自外层 %r 文件夹的 renpy/（SDK 项目或直接给出的 game/ 文件夹）": "Ren'Py version read from renpy/ in the outer %r folder "
                                                         '(an SDK project, or a given game/ folder)',
 "由 Ren'Py %s 构建；只有 8.5.x 在硬件上运行过，8.5.3 能否加载更旧的 8.x .rpyc 尚未测试（docs/COMPATIBILITY.md）": "Built with Ren'Py %s; "
                                                                                        'only 8.5.x has run on '
                                                                                        'hardware, and whether '
                                                                                        '8.5.3 loads older 8.x '
                                                                                        '.rpyc is untested '
                                                                                        '(docs/COMPATIBILITY.md)',
 "由 Ren'Py %s 构建，比运行时（8.5.3）更新": "Built with Ren'Py %s, newer than the runtime (8.5.3)",
 "Ren'Py %s：还没有支持 Ren'Py %d 的运行时": "Ren'Py %s: no runtime supports Ren'Py %d yet",
 "Ren'Py 版本未知（没有 renpy/ 文件夹）；Python 3 的 .rpyc 文件 意味着是 Ren'Py 8.x": "Ren'Py version unknown (no renpy/ folder); "
                                                                   "Python 3 .rpyc files mean Ren'Py 8.x",
 "无法判断 Ren'Py 版本或 Python 世代（没有 renpy/、lib/ 或 .rpyc 文件）": "Cannot tell the Ren'Py version or Python generation (no "
                                                         'renpy/, lib/ or .rpyc files)',
 "--decompile：unrpyc 只能反编译 Ren'Py 6.18 及更新版本，而非 %s": "--decompile: unrpyc can only decompile Ren'Py 6.18 and "
                                                     'newer, not %s',
 '%s 使用 Python 2；还没有 Python 2 运行时（附带 .rpy 源码可尝试 --recompile，否则尝试 --decompile）': '%s uses Python 2, and there is '
                                                                                'no Python 2 runtime (with .rpy '
                                                                                'sources try --recompile, '
                                                                                'otherwise try --decompile)',
 '%s 使用 Python 2，且其脚本在 .rpa 压缩包内；还没有 Python 2 运行时——可尝试 --unrpa 解开压缩包，然后 --decompile 反编译': '%s uses Python 2 and '
                                                                                          'its scripts are inside '
                                                                                          '.rpa archives; there '
                                                                                          'is no Python 2 runtime '
                                                                                          '- try --unrpa to '
                                                                                          'extract the archives, '
                                                                                          'then --decompile',
 '这个游戏': 'this game',
 '脚本在 .rpa 压缩包内：解开它们成散文件（到导出暂存区，源目录不变），然后即可反编译并用 Python 3 编译游戏（实验性功能）': 'Scripts are inside .rpa archives: '
                                                                        'extract them as loose files (into the '
                                                                        "export's staging area, leaving the "
                                                                        'source untouched), then the game can be '
                                                                        'decompiled and compiled with Python 3 '
                                                                        '(experimental)',
 "%d 个编译后的脚本从未被反编译：它们没有 .rpy 源码 （%s）。unrpyc 或 rpycdec 可以重建它们，然后 Ren'Py 8.5.3 会用 Python 3 编译游戏（实验性功能）": '%d '
                                                                                                       'compiled '
                                                                                                       'scripts '
                                                                                                       'were '
                                                                                                       'never '
                                                                                                       'decompiled: '
                                                                                                       'they have '
                                                                                                       'no .rpy '
                                                                                                       'source '
                                                                                                       '(%s). '
                                                                                                       'unrpyc or '
                                                                                                       'rpycdec '
                                                                                                       'can '
                                                                                                       'rebuild '
                                                                                                       'them, and '
                                                                                                       'then '
                                                                                                       "Ren'Py "
                                                                                                       '8.5.3 '
                                                                                                       'compiles '
                                                                                                       'the game '
                                                                                                       'with '
                                                                                                       'Python 3 '
                                                                                                       '(experimental)',
 '已将 %d 个 Python 2 脚本的代码块迁移为 Python 3，并修复旧版语法空块；源文件未修改': 'Migrated the code in %d Python 2 scripts to Python 3 '
                                                         'and fixed legacy empty blocks; the source files are '
                                                         'unchanged',
 '--precompile：请先解决游戏不受支持的原因：': '--precompile: fix why the game is unsupported first: ',
 '（已选择）': ' (chosen)',
 '%d 条问题，详见加载器': '%d issues, see the loader for details',
 '无法支持的原因': 'Unsupported',
 '警告': 'Warning',
 '；若能在其他工具中打开，请在那里解压后导入该文件夹': '; if another tool can open it, extract it there and import that folder',
 '压缩包条目含绝对路径：%r': 'Archive entry has an absolute path: %r',
 '压缩包条目逃逸出压缩包（..）：%r': 'Archive entry escapes the archive (..): %r',
 '压缩包条目的名称在 Windows 上无法安全创建：%r': 'Archive entry name cannot be created safely on Windows: %r',
 '压缩包有 %d 个条目（上限 %d）': 'Archive has %d entries (limit %d)',
 '压缩包解压后为 %d 字节（上限 %d）': 'Archive unpacks to %d bytes (limit %d)',
 '压缩包条目 %r 压缩率为 %d:1（上限 %d:1），疑似解压炸弹，已拒绝': 'Archive entry %r compresses at %d:1 (limit %d:1), a possible zip '
                                           'bomb; refused',
 '7z 无法打开 %s：%s%s': '7z cannot open %s: %s%s',
 '压缩包有密码保护；请自行解压后导入该文件夹': 'The archive is password-protected; extract it yourself and import that folder',
 '7z 解压 %s 失败：%s': '7z failed to extract %s: %s',
 '%s 不存在': '%s does not exist',
 '不支持的来源 %r：请提供文件夹、.zip、.7z 或 .rar': 'Unsupported source %r: give a folder, .zip, .7z or .rar',
 '无法读取 %s：%s': 'Cannot read %s: %s',
 '跳过符号链接 %s（不会导出链接）': 'Skipped symbolic link %s (links are never exported)',
 '压缩包解出了符号链接：%s': 'The archive unpacked a symbolic link: %s',
 '压缩包符号链接 %r 指向压缩包之外（%r）': 'Archive symbolic link %r points outside the archive (%r)',
 '压缩包包含链接（%r）；请自行解压后导入该文件夹': 'The archive contains a link (%r); extract it yourself and import that folder',
 '压缩包中 %r 出现两次（仅大小写不同？）': '%r appears twice in the archive (differing only in case?)',
 '无法解压 %r：%s': 'Cannot extract %r: %s',
 '压缩包解出了硬链接：%s': 'The archive unpacked a hard link: %s',
 '未导出：没有任何运行时能运行此游戏': 'Not exported: no runtime can run this game',
 '%s 已存在；请加 --replace 以更新其 game/ 和 manifest.json（存档保留）': '%s already exists; add --replace to update its game/ '
                                                         'and manifest.json (saves are kept)',
 '%s/%s 已存在；请替换以更新其 game/ 和 manifest.json（存档保留）': '%s/%s already exists; replace it to update its game/ and '
                                                  'manifest.json (saves are kept)',
 '%s/.old-%s 是一次中途停止的更新留下的，可能存有存档（game/saves）；请先取出需要的内容，删除它，然后重试': '%s/.old-%s was left by an interrupted update '
                                                                   'and may hold saves (game/saves); take out '
                                                                   'anything you need, delete it, then retry',
 '重编码 %d/%d': 'Re-encoding %d/%d',
 '上传已完成，但将其移动到位时中断了（%s）。在 %s 中，.uploading-%s 是新副本，.old-%s 是上一个副本，其中含其 game/saves': 'The upload finished, but '
                                                                                   'moving it into place was '
                                                                                   'interrupted (%s). In %s, '
                                                                                   '.uploading-%s is the new copy '
                                                                                   'and .old-%s the previous one, '
                                                                                   'which holds its game/saves',
 '%s 没有 manifest.json（其中记录图片）；请先在导入器中导出它': '%s has no manifest.json (which names its pictures); export it in the '
                                           'importer first',
 '%s：重编码失败（%s）；已按原样复制': '%s: re-encoding failed (%s); copied as it was',
 '%s 不在游戏库中': '%s is not in the library',
 '文件夹中有 %d 个文件，游戏库记录的来源有 %d 个': 'The folder has %d files; the library recorded %d from its source',
 '文件夹中%s %s': 'The folder has %s %s',
 '多': 'a surplus of',
 '少': 'a shortfall of',
 '无法删除 %s 的全部内容': 'Cannot delete all of %s',
 'Switch 地址 %r：请使用 主机:端口 格式，例如 192.168.1.20:%d': 'Switch address %r: use HOST:PORT, e.g. 192.168.1.20:%d',
 '%s/%s 不存在': '%s/%s does not exist',
 '{:,} 个字节': '{:,} bytes',
 '%d 个脚本或压缩包不同：%s': '%d scripts or archives differ: %s',
 '无法连接 %s:%d 上的 Switch（%s）。请在加载器中按 + 打开《通过 Wi-Fi 添加游戏》（或打开 sphaira 的 FTP 服务器），并核对它显示的地址；地址可能会变化。': 'Cannot '
                                                                                                   'connect to '
                                                                                                   'the Switch at '
                                                                                                   '%s:%d (%s). '
                                                                                                   'In the loader '
                                                                                                   'press + to '
                                                                                                   'open "Add '
                                                                                                   'games over '
                                                                                                   'Wi-Fi" (or '
                                                                                                   'start '
                                                                                                   "sphaira's FTP "
                                                                                                   'server), and '
                                                                                                   'check the '
                                                                                                   'address it '
                                                                                                   'shows; the '
                                                                                                   'address may '
                                                                                                   'change.',
 '游戏库中没有 manifest.json（手动复制）': 'No manifest.json in the library (copied by hand)',
 '游戏库中的副本来自 %s': 'The copy in the library came from %s',
 '游戏库中的 manifest.json 来自更旧或修改过的导入器': "The library's manifest.json came from an older or modified importer",
 '未找到 FFmpeg': 'FFmpeg not found',
 '封面需要 FFmpeg；请安装它或用 --tools 指定其文件夹': 'The cover needs FFmpeg; install it or point --tools at its folder',
 '封面': 'Cover',
 '%d 张图片超出 Switch GPU 的 %dx%d 纹理上限，可能无法显示（%s）；请手动缩小它们，游戏的布局依赖其尺寸': "%d images exceed the Switch GPU's %dx%d "
                                                                   'texture limit and may not display (%s); '
                                                                   "shrink them by hand, as the game's layout "
                                                                   'depends on their size',
 '%d 个压缩包无法检查（%s）：其中的图像和视频可能不适合 Switch': '%d archives cannot be checked (%s): the images and videos inside may '
                                         'not suit the Switch',
 '%d 个视频未检查：请安装 FFmpeg（ffprobe）或用 --tools 指定其文件夹，以便为 Switch 检查尺寸和帧率': '%d videos were not checked: install FFmpeg '
                                                                      '(ffprobe) or point --tools at its folder '
                                                                      'to check sizes and frame rates for the '
                                                                      'Switch',
 '%d fps（原为 %.0f）': '%d fps (was %.0f)',
 '%d 个视频高于 %d fps（%s）：CPU 负担和发热大；--optimize 会以相同尺寸重编码为 %d fps': '%d videos are above %d fps (%s): high CPU load '
                                                                'and heat; --optimize re-encodes them to %d fps '
                                                                'at the same size',
 '%d 个高于 %d fps 的视频 --optimize 无法重编码（带 alpha 或编解码器不支持）：%s': '%d videos above %d fps cannot be re-encoded by '
                                                            '--optimize (alpha or unsupported codec): %s',
 '%d 个视频高于 1920x1080（%s）：在 Switch 上难以流畅解码；不会自动缩小，因为游戏可能按原尺寸显示它们': '%d videos are above 1920x1080 (%s): hard to '
                                                                  'decode smoothly on the Switch; they are not '
                                                                  'shrunk automatically, as the game may show '
                                                                  'them at full size',
 '%d 个视频高于 1280x720 掌机屏幕（%s）：解码负担约为 720p 的两倍': '%d videos are above the 1280x720 handheld screen (%s): the '
                                               'decoding load is about twice that of 720p',
 '%d 个视频使用 HEVC/AV1（%s）：软件解码开销最大': '%d videos use HEVC/AV1 (%s): the most expensive to decode in software',
 '--optimize 将以 %d fps 重编码 %d 个视频（相同尺寸、恒定质量）：%s': '--optimize will re-encode %d videos at %d fps (same size, '
                                                  'constant quality): %s',
 "其中 %d 个来自压缩包，将导出为散装文件，Ren'Py 会优先于压缩包副本加载它们": '%d of them come from archives and are exported as loose files, '
                                               "which Ren'Py loads in preference to the archive copies",
 '无法从 %s 生成 %s：%s': 'Cannot make %s from %s: %s',
 '（位于 %s）': ' (in %s)',
 '%s（位于 %s）': '%s (in %s)',
 ' 或 ': ' or ',
 '；': '; ',
 '不安全的 MTP 路径：%r': 'Unsafe MTP path: %r',
 '请从已连接的 rpynx USB 设备中选择一个 MTP 目标。': 'Choose an MTP target from the connected rpynx USB devices.',
 '无法删除 MTP 存储根目录。': 'Cannot delete the MTP storage root.',
 '--precompile：桌面预编译目前仅支持 Windows': '--precompile: desktop precompiling is currently Windows-only',
 '--precompile：没有可预编译的松散脚本；如脚本在归档中，请先解包或反编译': '--precompile: no loose scripts to precompile; if they are inside '
                                              'an archive, extract or decompile them first',
 '--precompile：多个源脚本对应同一编译文件': '--precompile: several source scripts map to the same compiled file',
 '--precompile：SDK 缺少 Windows x64 编译器': '--precompile: the SDK lacks the Windows x64 compiler',
 '--precompile：SDK 的大小或 SHA-256 不匹配': '--precompile: the SDK size or SHA-256 does not match',
 "--precompile：Ren'Py 编译失败：\n": "--precompile: Ren'Py compilation failed:\n",
 "--precompile：无法解包 Ren'Py SDK：%s": "--precompile: cannot unpack the Ren'Py SDK: %s",
 "--precompile：Ren'Py 编译超过 %d 秒": "--precompile: Ren'Py compilation exceeded %d seconds",
 "--precompile：无法启动 Ren'Py SDK：%s": "--precompile: cannot start the Ren'Py SDK: %s",
 '--precompile：%s 与将导出的源脚本不匹配': '--precompile: %s does not match the source script to be exported',
 'SDK 下载大小不符': 'SDK download size mismatch',
 '--precompile：下载 SDK 失败：%s': '--precompile: SDK download failed: %s',
 '--precompile：没有生成有效的 %s': '--precompile: no valid %s was produced',
 '下载超时': 'Download timed out',
 "已重新编译：Ren'Py 8.5.3 会在首次启动时用 Python 3 编译 .rpy 源码（仅首次较慢）。Python 2 代码在 Python 3 下可能不报错而行为不同（例如整数相除 / 会得到浮点数），请检查游戏逻辑；原游戏的存档将无法读取": 'Recompiled: '
                                                                                                                                  "Ren'Py "
                                                                                                                                  '8.5.3 '
                                                                                                                                  'compiles '
                                                                                                                                  '.rpy '
                                                                                                                                  'sources '
                                                                                                                                  'with '
                                                                                                                                  'Python '
                                                                                                                                  '3 '
                                                                                                                                  'on '
                                                                                                                                  'first '
                                                                                                                                  'launch '
                                                                                                                                  '(slow '
                                                                                                                                  'only '
                                                                                                                                  'the '
                                                                                                                                  'first '
                                                                                                                                  'time). '
                                                                                                                                  'Python '
                                                                                                                                  '2 '
                                                                                                                                  'code '
                                                                                                                                  'may '
                                                                                                                                  'behave '
                                                                                                                                  'differently '
                                                                                                                                  'under '
                                                                                                                                  'Python '
                                                                                                                                  '3 '
                                                                                                                                  'without '
                                                                                                                                  'raising '
                                                                                                                                  'errors '
                                                                                                                                  '(for '
                                                                                                                                  'example, '
                                                                                                                                  'integer '
                                                                                                                                  'division '
                                                                                                                                  '/ '
                                                                                                                                  'yields '
                                                                                                                                  'a '
                                                                                                                                  'float); '
                                                                                                                                  'check '
                                                                                                                                  'the '
                                                                                                                                  'game '
                                                                                                                                  'logic. '
                                                                                                                                  'The '
                                                                                                                                  'original '
                                                                                                                                  "game's "
                                                                                                                                  'saves '
                                                                                                                                  'cannot '
                                                                                                                                  'be '
                                                                                                                                  'read',
 '%s（其索引无法读取：%s）': '%s (its index cannot be read: %s)',
 '%s（自定义格式；其索引无法读取）': '%s (custom format; its index cannot be read)',
 '%s 载有 %d 个已编译脚本（%s）': '%s holds %d compiled scripts (%s)',
 '--recompile: %s；反编译器无法处理压缩包内部 - 请加 --unrpa 解开压缩包（解开到导出暂存区，源目录不变）': '--recompile: %s; the decompiler cannot work '
                                                                     'inside an archive - add --unrpa to extract '
                                                                     'the archives (into the export staging area, '
                                                                     'leaving the source untouched)',
 '--recompile：%d 个已编译脚本没有 .rpy 源文件（%s）；请加 --decompile 用 unrpyc 重建它们': '--recompile: %d compiled scripts have no '
                                                                      '.rpy source (%s); add --decompile to '
                                                                      'rebuild them with unrpyc',
 '--recompile 略过了 %d 个没有源文件的已编译翻译文件（%s）；这些翻译可能会缺失': '--recompile skipped %d compiled translation files without '
                                                    'sources (%s); these translations may be missing',
 'Python 2 源码转换需要 Fissix；请安装 importer/requirements.txt 中的依赖': 'Python 2 source conversion needs Fissix; install '
                                                              'the dependencies in importer/requirements.txt',
 '无法自动转换：': 'Cannot convert automatically: ',
 '%s:%d 使用了已从 Python 3 移除的模块 %r': '%s:%d uses the module %r, removed from Python 3',
 '无法将 Python 2 代码转换为 Python 3（%s：%s）': 'Cannot convert Python 2 code to Python 3 (%s: %s)',
 '--decompile：未找到 unrpyc；打包程序应自带它（重新构建时请先运行 third_party/unrpyc/fetch.sh），从源码运行时同样需要 fetch.sh 获取，并用 Python 3.9+ 运行导入器': '--decompile: '
                                                                                                                       'unrpyc '
                                                                                                                       'not '
                                                                                                                       'found; '
                                                                                                                       'the '
                                                                                                                       'packaged '
                                                                                                                       'program '
                                                                                                                       'should '
                                                                                                                       'embed '
                                                                                                                       'it '
                                                                                                                       '(when '
                                                                                                                       'rebuilding, '
                                                                                                                       'run '
                                                                                                                       'third_party/unrpyc/fetch.sh '
                                                                                                                       'first); '
                                                                                                                       'running '
                                                                                                                       'from '
                                                                                                                       'source '
                                                                                                                       'also '
                                                                                                                       'needs '
                                                                                                                       'fetch.sh, '
                                                                                                                       'and '
                                                                                                                       'Python '
                                                                                                                       '3.9+ '
                                                                                                                       'to '
                                                                                                                       'run '
                                                                                                                       'the '
                                                                                                                       'importer',
 '--decompile：未找到 rpycdec；打包程序应自带它（重新构建时请 `pip install rpycdec`），从源码运行时也请安装它（需要 Python 3.10+），或用 --rpycdec-python 指定其解释器': '--decompile: '
                                                                                                                           'rpycdec '
                                                                                                                           'not '
                                                                                                                           'found; '
                                                                                                                           'the '
                                                                                                                           'packaged '
                                                                                                                           'program '
                                                                                                                           'should '
                                                                                                                           'embed '
                                                                                                                           'it '
                                                                                                                           '(when '
                                                                                                                           'rebuilding, '
                                                                                                                           '`pip '
                                                                                                                           'install '
                                                                                                                           'rpycdec`); '
                                                                                                                           'running '
                                                                                                                           'from '
                                                                                                                           'source, '
                                                                                                                           'install '
                                                                                                                           'it '
                                                                                                                           'too '
                                                                                                                           '(needs '
                                                                                                                           'Python '
                                                                                                                           '3.10+), '
                                                                                                                           'or '
                                                                                                                           'name '
                                                                                                                           'its '
                                                                                                                           'interpreter '
                                                                                                                           'with '
                                                                                                                           '--rpycdec-python',
 '--decompile：%s 失败：%s': '--decompile: %s failed: %s',
 '--decompile：%s 未能在 %d 秒内完成': '--decompile: %s did not finish within %d seconds',
 '--decompile：无法运行 %s：%s': '--decompile: cannot run %s: %s',
 '--decompile：%s 无法反编译 %d 个脚本中的 %d 个（%s）%s；游戏需要它们': '--decompile: %s could not decompile %d of %d scripts (%s)%s; '
                                                    'the game needs them',
 '%s 无法反编译 %d 个翻译文件（%s）%s；它们被略过，这些翻译可能会缺失': '%s could not decompile %d translation files (%s)%s; they were '
                                            'skipped, and these translations may be missing',
 '用 %s 反编译了 %d 个没有 .rpy 源文件的脚本（%s）。反编译结果可能与原文件不同：注释和格式会丢失，语句也可能被误读，请通玩整个游戏以确认': 'Decompiled %d scripts without '
                                                                                '.rpy sources with %s (%s). The '
                                                                                'result may differ from the '
                                                                                'original files: comments and '
                                                                                'formatting are lost, and '
                                                                                'statements may be misread; play '
                                                                                'through the whole game to '
                                                                                'confirm',
 "%d 个反编译脚本的空 ATL 块已修复为 Ren'Py 8 语法（%s）；原游戏在 Ren'Py 6.x 接受空块": 'Fixed the empty ATL blocks of %d decompiled '
                                                               "scripts to Ren'Py 8 syntax (%s); the original "
                                                               "game accepted empty blocks under Ren'Py 6.x",
 '%s 无法反编译 %s 中的部分语句；这些语句以注释形式保留（搜索 %r），游戏可能在该处出错': '%s could not decompile some statements in %s; they are kept '
                                                    'as comments (search %r), and the game may fail there',
 'pickle 操作码 %s 不是纯数据': 'pickle opcode %s is not plain data',
 'pickle 缺少 STOP': 'pickle is missing STOP',
 '压缩包索引过大或不完整': 'Archive index is too large or incomplete',
 '索引条目无效：%r': 'Invalid index entry: %r',
 '条目 %r 位于压缩包之外': 'Entry %r lies outside the archive',
 '%s（索引不是纯数据：%s）': '%s (index is not plain data: %s)',
 '%s（自定义格式）': '%s (custom format)',
 '压缩包 %s 有不安全的名称，已跳过：%r': 'Archive %s has an unsafe name; skipped: %r',
 '--unrpa：%s 是自定义格式的压缩包，无法解开': '--unrpa: %s is an archive in a custom format and cannot be extracted',
 '未找到 7-Zip；请安装（Windows：7-zip.org，macOS：`brew install p7zip`，Linux：p7zip-full），或用 --tools 指定其文件夹': '7-Zip not '
                                                                                                   'found; '
                                                                                                   'install it '
                                                                                                   '(Windows: '
                                                                                                   '7-zip.org, '
                                                                                                   'macOS: `brew '
                                                                                                   'install '
                                                                                                   'p7zip`, '
                                                                                                   'Linux: '
                                                                                                   'p7zip-full), '
                                                                                                   'or point '
                                                                                                   '--tools at '
                                                                                                   'its folder',
 "分析 Ren'Py 游戏并导出，供 Switch 上的 rpynx 运行。": "Analyse a Ren'Py game and export it to run in rpynx on the Switch.",
 '游戏文件夹、.zip、.7z 或 .rar': 'game folder, .zip, .7z or .rar',
 '列出已连接的 rpynx USB MTP 导出目标': 'list connected rpynx USB MTP export targets',
 '导出根目录，如 SD 卡；经 FTP 则为 ftp://主机:端口（加载器的“经 Wi-Fi 添加游戏”，或 sphaira）；写入 switch/renpy-loader/games/<id>': 'export '
                                                                                                      'root, e.g. '
                                                                                                      'the SD '
                                                                                                      'card; over '
                                                                                                      'FTP, '
                                                                                                      'ftp://HOST:PORT '
                                                                                                      '(the '
                                                                                                      "loader's "
                                                                                                      '"Add games '
                                                                                                      'over '
                                                                                                      'Wi-Fi", or '
                                                                                                      'sphaira); '
                                                                                                      'writes '
                                                                                                      'switch/renpy-loader/games/<id>',
 '游戏 ID（文件夹名）；默认由标题生成': 'game ID (folder name); made from the title by default',
 '更新已有的导出（保留存档）': 'update an existing export (keeps saves)',
 '将高于 30 fps 的视频重编码为 30 fps（尺寸不变，需要 FFmpeg）': 're-encode videos above 30 fps to 30 fps (same size; needs FFmpeg)',
 "自带 .rpy 源码的 Python 2 游戏：导出时不带 .rpyc，由 Ren'Py 8 重新编译": 'Python 2 game with its .rpy sources: export without '
                                                        ".rpyc and let Ren'Py 8 recompile them",
 '同 --recompile，并先用 --decompiler 将没有 .rpy 的 .rpyc 重建出 .rpy，反编译器作为独立进程运行': 'like --recompile, and first rebuild '
                                                                          '.rpy from .rpyc files that have none '
                                                                          'with --decompiler, run as a separate '
                                                                          'process',
 '配合 --decompile：unrpyc（默认；third_party/unrpyc/fetch.sh）或 rpycdec （pip install rpycdec，Python 3.10+）；两者对比见 docs/IMPORTER.md': 'with '
                                                                                                                             '--decompile: '
                                                                                                                             'unrpyc '
                                                                                                                             '(default; '
                                                                                                                             'third_party/unrpyc/fetch.sh) '
                                                                                                                             'or '
                                                                                                                             'rpycdec '
                                                                                                                             '(pip '
                                                                                                                             'install '
                                                                                                                             'rpycdec, '
                                                                                                                             'Python '
                                                                                                                             '3.10+); '
                                                                                                                             'see '
                                                                                                                             'docs/IMPORTER.md '
                                                                                                                             'for '
                                                                                                                             'the '
                                                                                                                             'comparison',
 '装有 rpycdec 的 Python 3.10+ 解释器；默认为 PATH 上的 rpycdec': 'a Python 3.10+ interpreter with rpycdec; defaults to '
                                                      'rpycdec on PATH',
 '解开游戏的 .rpa 压缩包成散文件（导出到暂存区，源目录不变）：脚本在压缩包内的 Python 2 游戏（如 Steam 版 DDLC）借此可反编译并重新编译；导出不再含 .rpa': 'extract the '
                                                                                                "game's .rpa "
                                                                                                'archives into '
                                                                                                'loose files '
                                                                                                '(into the export '
                                                                                                'staging area, '
                                                                                                'leaving the '
                                                                                                'source '
                                                                                                'untouched): a '
                                                                                                'Python 2 game '
                                                                                                'whose scripts '
                                                                                                'are inside '
                                                                                                'archives (such '
                                                                                                "as Steam's DDLC) "
                                                                                                'can then be '
                                                                                                'decompiled and '
                                                                                                'recompiled; the '
                                                                                                'export holds no '
                                                                                                '.rpa',
 "Windows：下载并使用 Ren'Py 8.5.3 在电脑上预编译脚本。会以你的权限运行游戏初始化代码，只对信任的游戏使用。": "Windows: download and use Ren'Py 8.5.3 to "
                                                                    'precompile scripts on the computer. It runs '
                                                                    "the game's init code with your permissions; "
                                                                    'use it only for games you trust.',
 '游戏在加载器中的封面，FFmpeg 能读取的任意图片（2:3 海报比例最合适；其他比例会从中间裁剪）；默认：其菜单立绘的中间部分。--replace 会保留之前选定的封面': "the game's cover in "
                                                                                          'the loader: any image '
                                                                                          'FFmpeg can read (a 2:3 '
                                                                                          'poster ratio fits '
                                                                                          'best; other ratios are '
                                                                                          'cropped from the '
                                                                                          'middle); default: the '
                                                                                          'middle of its menu '
                                                                                          'art. --replace keeps a '
                                                                                          'cover chosen before',
 '存放 ffmpeg/ffprobe 或 7z 的文件夹（若不在 PATH 上）': 'folder holding ffmpeg/ffprobe or 7z (if not on PATH)',
 '请指定游戏来源，或使用 --mtp-list 列出 USB 设备。': 'Give a game source, or use --mtp-list to list USB devices.',
 '已导出到 %s': 'Exported to %s',
 '错误：%s': 'Error: %s',
 'rpynx 导入器': 'rpynx importer',
 '请先分析一个游戏。': 'Analyse a game first.',
 '请先选择导出位置。': 'Choose an export location first.',
 '请先选择目标。': 'Choose a target first.',
 '窗口需要 pywebview：python3 -m pip install pywebview\n（命令行无需额外依赖：python3 importer/rsl_import.py --help）': 'The '
                                                                                                       'window '
                                                                                                       'needs '
                                                                                                       'pywebview: '
                                                                                                       'python3 '
                                                                                                       '-m pip '
                                                                                                       'install '
                                                                                                       'pywebview\n'
                                                                                                       '(the '
                                                                                                       'command '
                                                                                                       'line '
                                                                                                       'needs no '
                                                                                                       'extra '
                                                                                                       'dependencies: '
                                                                                                       'python3 '
                                                                                                       'importer/rsl_import.py '
                                                                                                       '--help)',
 '没有 manifest.json：手动复制，或非本导入器导出。': 'No manifest.json: copied by hand, or exported by another importer.',
 "游戏 ID：只能使用小写字母 a-z、数字 0-9 和 '-'（try %r）。": "Game ID: use only lowercase letters a-z, digits 0-9 and '-' (try "
                                             '%r).',
 '游戏压缩包 (*.zip;*.7z;*.rar)': 'Game archives (*.zip;*.7z;*.rar)',
 '所有文件 (*.*)': 'All files (*.*)',
 '图片 (*.png;*.jpg;*.jpeg;*.webp;*.bmp)': 'Images (*.png;*.jpg;*.jpeg;*.webp;*.bmp)',
 '比较已取消。': 'Comparison cancelled.',
 '导出已取消。%s 保持原样。': 'Export cancelled. %s is unchanged.',
 'SD 卡': 'SD card',
 '导入器': 'Importer',
 '工作区': 'Workspace',
 '游戏库': 'Library',
 '导入': 'Import',
 '目标': 'Target',
 '与文件夹比较…': 'Compare with folder…',
 '未选择目标': 'No target selected',
 '请选择 SD 卡，或通过 Wi-Fi 连接 Switch，以查看其中已有的游戏。': 'Choose an SD card, or connect to the Switch over Wi-Fi, to see the '
                                             'games already on it.',
 '选择目标…': 'Choose target…',
 '删除…': 'Delete…',
 '删除': 'Delete',
 '将从': 'This removes',
 '中移除': 'from',
 '。 除非勾选下方选项，其存档将保留在': '. Unless the option below is checked, its saves stay in',
 '中。': '.',
 '？': '?',
 '同时删除其存档': 'Also delete its saves',
 '保留': 'Keep',
 '更换封面…': 'Change cover…',
 '比较对象：': 'Comparing with:',
 '重新比较': 'Compare again',
 '取消': 'Cancel',
 '只读：不会复制或删除任何内容。为检查压缩包，需要先将其解压，可能需要一些时间。': 'Read-only: nothing is copied or deleted. Checking archives unpacks '
                                            'them first, which can take a while.',
 '游戏': 'Game',
 '状态': 'Status',
 '详情': 'Details',
 '导入游戏': 'Import a game',
 "仅支持无 DRM 的 Ren'Py 游戏。源文件永远不会被修改；加密或自定义的压缩包会被报告，但不会被打开。": "DRM-free Ren'Py games only. Source files are never "
                                                           'modified; encrypted or custom archives are reported, '
                                                           'not opened.',
 "将 Ren'Py 游戏拖放到此处": "Drop a Ren'Py game here",
 '文件夹、ZIP、7Z 或 RAR': 'Folder, ZIP, 7Z or RAR',
 '选择文件夹…': 'Choose folder…',
 '选择压缩包…': 'Choose archive…',
 '选项': 'Options',
 '优化：将高于 30 fps 的视频重新编码为 30 fps（需要 FFmpeg）': 'Optimise: re-encode videos above 30 fps to 30 fps (needs FFmpeg)',
 '建议用于此游戏': 'Suggested for this game',
 '开启': 'Turn on',
 "重新编译：自带 .rpy 源文件的 Ren'Py 7 游戏": "Recompile: Ren'Py 7 games with their .rpy sources",
 "反编译：为没有 .rpy 的 .rpyc 重建 .rpy，然后重新编译（Ren'Py 7）": 'Decompile: rebuild .rpy from .rpyc files that have none, then '
                                                  "recompile (Ren'Py 7)",
 '（默认）': ' (default)',
 "优点：历史悠久的 Ren'Py 反编译器；无法读取的脚本会被指出，其余脚本仍会被反编译；打包程序已内置。": "Pros: the long-standing Ren'Py decompiler; scripts it "
                                                         'cannot read are pointed out and the rest are still '
                                                         'decompiled; embedded in the packaged program.',
 "缺点：从源码运行导入器时不在 pip 上，需先运行一次 third_party/unrpyc/fetch.sh；面向 Ren'Py 8 设计，可读取 7 和 6.18 或更新的版本。": 'Cons: not on pip '
                                                                                                'when running the '
                                                                                                'importer from '
                                                                                                'source, so run '
                                                                                                'third_party/unrpyc/fetch.sh '
                                                                                                'once; designed '
                                                                                                "for Ren'Py 8, it "
                                                                                                'reads 7 and 6.18 '
                                                                                                'or newer.',
 "优点：已通过重新编译其输出，在 Ren'Py 7.6.3 和 8.x 上测试；打包程序已内置。": "Pros: tested on Ren'Py 7.6.3 and 8.x by recompiling its "
                                                    'output; embedded in the packaged program.',
 '缺点：从源码运行导入器时需先 pip install rpycdec（Python 3.10+，不能用 macOS 自带的 python3）；遇到第一个无法读取的脚本就会停止，因此一个损坏的文件会阻止整个游戏。': 'Cons: '
                                                                                                              'running '
                                                                                                              'the '
                                                                                                              'importer '
                                                                                                              'from '
                                                                                                              'source '
                                                                                                              'needs '
                                                                                                              'pip '
                                                                                                              'install '
                                                                                                              'rpycdec '
                                                                                                              'first '
                                                                                                              '(Python '
                                                                                                              '3.10+, '
                                                                                                              'not '
                                                                                                              'the '
                                                                                                              'python3 '
                                                                                                              'that '
                                                                                                              'ships '
                                                                                                              'with '
                                                                                                              'macOS); '
                                                                                                              'it '
                                                                                                              'stops '
                                                                                                              'at '
                                                                                                              'the '
                                                                                                              'first '
                                                                                                              'unreadable '
                                                                                                              'script, '
                                                                                                              'so '
                                                                                                              'one '
                                                                                                              'broken '
                                                                                                              'file '
                                                                                                              'blocks '
                                                                                                              'the '
                                                                                                              'whole '
                                                                                                              'game.',
 '解开 .rpa 压缩包：脚本在压缩包内的游戏（如 Steam 版 DDLC）借此可反编译；导出不再含 .rpa': 'Extract .rpa archives: lets a game whose scripts are '
                                                            "inside archives (such as Steam's DDLC) be "
                                                            'decompiled; the export holds no .rpa',
 '在 Windows 上预编译脚本（首次自动下载约 155 MiB）': 'Precompile scripts on Windows (the first time downloads about 155 MiB)',
 '只对信任的游戏开启：预编译会以你的权限运行游戏初始化代码，暂存副本不是沙箱。': "Turn on only for games you trust: precompiling runs the game's init "
                                           'code with your permissions, and the staged copy is not a sandbox.',
 '游戏 ID': 'Game ID',
 '留空：由标题生成': 'Leave empty: made from the title',
 '工具': 'Tools',
 '工具文件夹…': 'Tools folder…',
 '分析': 'Analyse',
 '默认使用游戏菜单中的图片，除非你选择了其他图片；所选图片在之后替换游戏时仍会保留。': "Uses the picture from the game's menu by default unless you choose "
                                              'another; a chosen picture is kept when the game is replaced later.',
 '导出': 'Export',
 '未选择': 'Not selected',
 '更改…': 'Change…',
 '替换此游戏的已有导出（存档将保留）': "Replace this game's existing export (saves are kept)",
 '游戏库的读取位置，以及游戏的导出位置。游戏将导出到 switch/renpy-loader/games/<id>。': 'Where the library is read from, and where games '
                                                              'are exported. Games go to '
                                                              'switch/renpy-loader/games/<id>.',
 '当前': 'Current',
 '其顶层文件夹：Windows 上为 E:\\，macOS 上为 /Volumes/<名称>。': 'Its top-level folder: E:\\ on Windows, /Volumes/<name> on '
                                                   'macOS.',
 '通过 Wi-Fi 连接 Switch（FTP）': 'Connect to the Switch over Wi-Fi (FTP)',
 '在 Switch 上的加载器中，按下 +，打开“通过 Wi-Fi 添加游戏”，然后输入 其显示的地址。发送游戏期间请保持该页面打开；地址可能会变化。sphaira 的 FTP 服务器同样可用（约 1–2 MB/s）。': 'In '
                                                                                                                 'the '
                                                                                                                 'loader '
                                                                                                                 'on '
                                                                                                                 'the '
                                                                                                                 'Switch, '
                                                                                                                 'press '
                                                                                                                 '+, '
                                                                                                                 'open '
                                                                                                                 '"Add '
                                                                                                                 'games '
                                                                                                                 'over '
                                                                                                                 'Wi-Fi", '
                                                                                                                 'then '
                                                                                                                 'enter '
                                                                                                                 'the '
                                                                                                                 'address '
                                                                                                                 'it '
                                                                                                                 'shows. '
                                                                                                                 'Keep '
                                                                                                                 'that '
                                                                                                                 'page '
                                                                                                                 'open '
                                                                                                                 'while '
                                                                                                                 'games '
                                                                                                                 'are '
                                                                                                                 'sent; '
                                                                                                                 'the '
                                                                                                                 'address '
                                                                                                                 'may '
                                                                                                                 'change. '
                                                                                                                 "sphaira's "
                                                                                                                 'FTP '
                                                                                                                 'server '
                                                                                                                 'works '
                                                                                                                 'too '
                                                                                                                 '(about '
                                                                                                                 '1-2 '
                                                                                                                 'MB/s).',
 '连接': 'Connect',
 '通过 USB 连接 Switch（MTP）': 'Connect to the Switch over USB (MTP)',
 '用 USB 数据线连接 Switch，在加载器的 + 菜单中打开 USB 传输。': 'Connect the Switch with a USB cable and open USB transfer in the '
                                             "loader's + menu.",
 '查找 USB 设备': 'Find USB devices',
 '意外错误，请报告此问题：': 'Unexpected error, please report this: ',
 '无清单': 'No manifest',
 '不支持': 'Unsupported',
 '需检查': 'Needs review',
 '实验性': 'Experimental',
 '正在从 Switch 读取游戏库…': 'Reading the library from the Switch…',
 '1 个游戏': '1 game',
 ' 个游戏': ' games',
 '。': '.',
 '正在生成图片并发送到 Switch…': 'Generating the picture and sending it to the Switch…',
 '正在生成图片…': 'Generating the picture…',
 '背景': 'background',
 '{what}（{name}）已更改；加载器将在下次启动时显示。': '{what} for {name} changed; the loader shows it on its next launch.',
 '已选择{what}；导出时将写入。': '{what} chosen; it is written on export.',
 '已找到': 'Found',
 '未找到': 'Not found',
 '选好选项后，点击分析。': 'Choose the options, then click Analyse.',
 '正在分析并预编译…首次需要下载 Ren’Py SDK。': 'Analysing and precompiling…the first time downloads the Ren’Py SDK.',
 '正在分析…解压压缩包和检查视频可能需要一些时间。': 'Analysing…unpacking archives and checking videos can take a while.',
 ' 建议的选项已在上方标出。': ' Suggested options are marked above.',
 '此游戏无法按原样导出：': 'This game cannot be exported as it is: ',
 '已就绪，可以导出。': 'Ready to export.',
 '已就绪。请选择目标，然后导出。': 'Ready. Choose a target, then export.',
 '正在上传到 Switch…': 'Uploading to the Switch…',
 '正在导出…': 'Exporting…',
 '已导出到 ': 'Exported to ',
 '正在删除…通过 FTP 可能需要一分钟。': 'Deleting…over FTP this can take a minute.',
 '正在删除…': 'Deleting…',
 '已删除 {name}。': 'Deleted {name}.',
 ' 其存档也已删除。': ' Its saves were deleted too.',
 ' 其存档已保留。': ' Its saves were kept.',
 '正在连接…': 'Connecting…',
 '已连接：Switch 上有 ': 'Connected: the Switch has ',
 '不同': 'Different',
 'Switch 上没有': 'Not on the Switch',
 '仅在 Switch 上': 'Only on the Switch',
 '无法比较': 'Cannot compare',
 '不是游戏': 'Not a game',
 '相同': 'Same',
 '正在读取游戏库…': 'Reading the library…',
 '文件夹和游戏库中没有游戏。': 'No games in the folder or the library.',
 '正在检查第 {item} / {total} 个：{name}': 'Checking item {item} of {total}: {name}',
 '{note}：{name}（第 {file} / {total} 个文件；可能需要数分钟）': '{note}: {name} (file {file} of {total}; this may take minutes)',
 '正在上传第 {file} / {total} 个{part}：{name}': 'Uploading file {file} of {total}{part}: {name}',
 '正在复制第 {file} / {total} 个{part}：{name}': 'Copying file {file} of {total}{part}: {name}',
 '正在查找 USB 设备…': 'Finding USB devices…',
 '未找到 rpynx USB 设备。': 'No rpynx USB device found.',
 '已连接：{name}（USB MTP）': 'Connected: {name} (USB MTP)',
 '正在取消，将在当前游戏完成后停止…': 'Cancelling; it stops after the current game finishes…',
 '正在取消…': 'Cancelling…',
 '将在当前文件完成后取消…': 'Cancelling after the current file finishes…',
 '使用 ': 'Use ',
 'Windows MTP 适配器的版本与此导入器不兼容。': 'The Windows MTP adapter version is incompatible with this importer.',
 '缺少 Windows MTP 适配器。请使用完整的导入器安装包。': 'The Windows MTP adapter is missing. Use the complete importer package.',
 '找不到 MTP 对象或设备。': 'The MTP object or device was not found.',
 'MTP 目标已存在。': 'The MTP destination already exists.',
 'Windows MTP 错误 %d：%s': 'Windows MTP error %d: %s',
 'MTP 列表过大。': 'The MTP list is too large.',
 '找不到 rpynx USB 设备。': 'No rpynx USB device found.',
 'USB 设备已更换，请重新扫描。': 'The USB device changed. Scan again.'}
