# scws2

面向社区、CMS 等通用全文检索的纯 C 词典分词工具包。仅支持 UTF-8；运行时无第三方依赖，不提供关键词提取和 IDF。C 接口见 [API.md](API.md)。

scws2 源自 [SCWS 1.x](https://github.com/hightman/scws) 的词典分词思路，是独立维护的新项目，不是原项目的兼容升级。C API、PHP 扩展接口及 XDB2 词典格式均与 SCWS 1.x 不兼容，不能直接替换；库名为 `libscws2`，头文件为 `scws2.h`，C 符号使用 `scws2_` 前缀，PHP 扩展命名为 `scws2`，以支持两代实现并存。

本仓库历史从新版词典流水线的首次提交开始，不包含 SCWS 1.x 提交历史；原项目代码、历史及旧 PHP 扩展请查阅上述来源仓库。原作者信息及许可证保留在 `AUTHORS` 和 `COPYING`。

## 本机初步评测

2026-11-25，Apple M5 Pro、48 GiB、macOS 27.2.1，Apple clang 21、`-O2`。使用 [SIGHAN 2005](https://aclanthology.org/I05-3017/) 的 PKU/MSR 公开测试集，按 standard 模式的完整词跨度计算精确率、召回率与 F1，含标点。

| 测试集 | Precision | Recall | F1 | memory 吞吐 MiB/s | mmap 吞吐 MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| PKU | 88.37% | 82.01% | 85.08% | 11.78 | 11.35 |
| MSR | 86.70% | 84.26% | 85.46% | 13.31 | 13.42 |

速度为单线程、热缓存、7 轮交错计时中位数，排除词典加载、语料读取和输出；本机负载会影响数值。search 输出重叠补词，不套用唯一切分的 F1，速度及详细记录见 [评测说明](tests/evaluation/README.md)。

部分误差来自标注粒度差异，例如“4／届”与“4届”、数值与单位是否合并；也存在真实的歧义、英数边界和未登录词错误。两套语料并非新语料，不能简单把差距归因于词典老旧。这些指标反映对标注规范的符合程度，不能直接等同于社区/CMS 全文检索效果，后者仍需实测。数量词采用整体保留、search 另补数值及单位的策略。

同口径下曾与 SCWS 1.2.3 和同源词典的 jieba 做对照，当前精度与吞吐保持可比水平；对照仅作开发参考，不作为其它实现的通用排名。

## 开发状态

按“词典 → 核心库及测试 → PHP 扩展”的顺序推进：

| 模块 | 当前状态 |
| --- | --- |
| 词典 | 已提供来源锁定、人工修订、可复现 XDB2 构建、校验及词表差异报告；仍需补充新词来源和质量评估 |
| 核心库与 CLI | 已实现上下文内部管理词典、严格 UTF-8、英文数字处理、候选 DAG、动态规划、standard/search 模式、叠加词典及可区分来源的补词；已有生命周期和独立上下文并发测试 |
| PHP 扩展 | 已实现 PHP 7.0+ 的 Scws2 对象接口，segment 一次返回全部 token 数组；构建与实测范围见 [phpext/README.md](phpext/README.md) |

当前是可运行原型，尚未通过真实检索质量和完整跨平台验收。搜索补词的歧义筛选仍需评估，算法替换不会自动解决旧词和新词不足问题。正式发布前须用分词及小型检索评估集衡量召回、误召回、词数、内存和速度；未验证的词典更新不自动发布。

## 构建与安装

源码发行包 `scws2-0.x.y.tar.gz` 已包含 configure、构建模板和生成好的规则表。常规构建只需 C11 编译器、Make、ar 和系统开发工具，不需要 Python、Autoconf、Automake 或 Libtool。macOS 已本机验证，Linux/macOS CI 已配置；实际 CI 通过范围以 Actions 结果为准，其他 Unix 尚未完成验收。

```sh
./configure --prefix="$HOME/.local/scws2"
make -j2
make install
```

库为 `libscws2`，公共头文件安装到 `include/`，库和 pkg-config 文件到 `lib/`，演示程序 `scws2` 到 `bin/`。`make install DESTDIR=/tmp/scws2-package` 可用于打包暂存；安装前缀通过 configure 的 `--prefix` 指定。核心构建和安装不下载、构建或安装词典。

mmap 和文本缓存接口由 configure 检测；可显式使用 `--disable-mmap`、`--disable-text-cache`。mmap 不可用或映射失败时退回内存。共享库使用 Libtool 管理 ABI 版本（初始 ABI 0），与项目版本分开维护。自定义安装路径需让应用能在运行时找到共享库。

Git 源码开发需要额外安装 Autoconf、Automake、Libtool 和 Python 3.9+：

```sh
./bootstrap.sh                  # 生成规则表、configure 及构建模板，不提交生成文件
./configure --prefix="$HOME/.local/scws2"
make -j2
make check                      # 开发测试需要 Python，不联网
```

修改规则后 make 会重新生成规则表，此时需要 Python。发行包中的规则表已生成，普通用户无需重新生成。`make clean` 清理核心编译产物，不清理独立词典。

辅助程序位于 `tools/`。`scws2` 仅供分词测试：`-d file` 将 `.txt`（大小写不敏感）视为文本，其余为 XDB2；`--mmap` 请求 mmap，默认使用内存。`--search` 开启搜索补词，`--symbols` 保留符号，`--bigrams` 开启搜索模式的未知汉字二元补词。

```sh
./scws2 -d dict/build/dict.xdb --search -i '中文分词测试'
```

PHP 扩展单独通过 phpize 构建，动态链接已安装的共享库，无需 Python，不再嵌入核心源文件：

```sh
cd phpext
phpize
./configure --with-scws2="$HOME/.local/scws2"
make -j2
make test
make install
```

扩展的 PHP 版本要求及接口见 [phpext/README.md](phpext/README.md)。

## 独立词典

词典位于同仓库 `dict/` 子项目，构建需要 Python 3.9+ 和 C11 编译器，不依赖已编译的核心库：

```sh
cd dict
make                            # 校验缓存，按需下载锁定源，生成 build/dict.xdb
make install PREFIX="$HOME/.local/scws2"
```

可省略 PREFIX 自动探测父项目已配置的安装前缀，再依次检查 `/usr/local`、`/usr/local/scws2`、`~/.local/scws2`；自动候选必须存在 `include/scws2.h`。没有有效候选时报错，需显式指定 PREFIX；不会只因 `/usr/local` 目录存在就选用。支持 `DESTDIR`，安装为 `$PREFIX/etc/dict.xdb` 并附带许可和清单；目标同样新或更新则跳过，以原子替换更新 XDB。

预构建词典单独发布为 `scws2-dict-YYYYMMDD.tar.gz`，包含 `dict.xdb`、许可、清单和安装脚本，安装无需 Python 或编译器。调用方仍须显式指定词典路径。详细来源、处理规则及安装说明见 [dict/README.md](dict/README.md)。

## 版本与发布

版本事实来源为 `VERSION`；初始开发版为 `0.1.1`，首次正式发布为 `0.1.2`。patch 奇数为开发版，不打发布 tag、不创建 Release；偶数为正式版。C 库、CLI、PHP 扩展使用同一版本来源。

```sh
./release.sh --dry-run           # 检查版本选择，0.1.1 -> 0.1.2 -> 0.1.3
./release.sh                     # 构建、测试并准备 dist/ 资产；不打 tag 或推送
./release.sh 0.2.0               # 可显式指定更高的偶数 patch 版本
./release.sh --publish           # 验证后提交正式版本、打 tag、推送并创建 GitHub Release
```

发布者需要完整开发环境、PHP 开发工具及 Python；`--publish` 还需已登录的 GitHub CLI `gh` 和仓库推送权限。脚本要求工作区干净。正式 tag 为 `v0.1.2` 这种形式，与源码包版本一致；发布后立即提交下一奇数开发版本，正式 tag 不移动或覆盖。脚本会验证源码包在禁用 Python 的目录外构建中可编译安装，并测试 PHP 动态链接。

同一 Release 可同时包含核心包、日期命名的词典包和 SHA256SUMS。词典按最终 XDB、许可、来源及加工元数据判断变化；未变时在 Release 说明中链接此前资产，不重复上传。`dict/LAST_RELEASE.json` 记录已发布词典。词典日期使用 UTC+8；同日不同内容不能覆盖同名资产。

网络发布失败时，脚本保留已创建的正式提交/tag 和本地资产，不强制回滚或覆盖远程历史。应先检查 GitHub Release/远程 tag 状态，补齐失败步骤，再将 VERSION 更新为下一奇数并提交；不要直接再次运行以覆盖已有 tag。

## 分词用例对照

```sh
make cases                       # 逐句观察，有差异也正常结束
make check-cases                 # 严格比较，有差异返回失败
python3 tools/check_segments.py --case case-07-standard --case case-07-search
```

逐句显示输入、分词、词性与预期差异，支持自定义 JSON 用例和叠加词典。
样例集中在 `tests/cases/`。每句分别验证 standard 和 search，预期以已接受的新版结果为准；第 7 句保留已知主路径缺陷，严格比较会报告失败。配置与退出码见 [用例说明](tests/cases/README.md)。

## 词典

核心词典默认请求只读 mmap；C API 先用 `scws2_t ctx = scws2_new()` 创建上下文，再用 `scws2_set_dict(ctx, path, 0)` 加载词典，或传 `SCWS2_DICT_MEMORY` 强制整库 fread 到私有内存；`scws2_free(ctx)` 统一释放词典和上下文。`scws2_segment()` 返回独立的 `scws2_result_t`，用 `scws2_result_next()` 遍历、`scws2_result_free()` 释放，可嵌套分词；结果在 ctx 释放后仍有效。契约见 [API.md](API.md)。mmap 不可用时退回内存。

运行时可用 `scws2_add_dict(ctx, "user.txt", SCWS2_DICT_TEXT)` 加载 UTF-8 自定义词表。一行一个词，支持空格/Tab 分隔的 `词条 词频 词性` 或 `词条 词性 词频`；缺省词性为 `un`，缺省及 `0` 词频按最低值 `1` 处理。文本自动编译并缓存为临时 XDB，后续加载跨进程复用；默认请求 mmap，按位组合 `SCWS2_DICT_TEXT | SCWS2_DICT_MEMORY` 可强制内存加载，不记忆前次加载方式。格式、词频参考及缓存说明见 [API.md](API.md#文本词典)，词性说明见 [dict/POS.md](dict/POS.md)。

当前唯一接入的上游是 jieba 标准词典，THUOCL 尚未融入。词性最多两个 ASCII 字符，`nrt`、`nrfg` 在加工时归并为 `nr`，缺失时为空；词性不参与切分评分。

来源锁定、加工、许可及使用限制见 [dict/README.md](dict/README.md)，文件格式见 [dict/FORMAT.md](dict/FORMAT.md)。可单独运行词典测试：

```sh
python3 -m unittest discover -s tests -p 'test_dictionary.py' -v
```

## 规则与迁移

规则统一在 `src/rules.ini`，同一份 UTF-8 数据同时维护简繁写法，只保留符号、中文数字和数量单位。内置识别支持明确的中文整数、小数、金融数字、序数和数字加单位，不进行姓名、地名猜测或词性倍率打分。

`make` 按规则源和生成器的修改时间自动更新 `generated/rules_data.h` 并重新编译，`make rules` 可手动强制生成。生成代码不进入 Git，运行时不读取 INI；详细边界见 [src/RULES.md](src/RULES.md)。

2.0 不兼容 1.x 的 API 和 XDB。旧字符集代码、旧引擎、Autotools 构建及历史 PHP 演示已移除，可从 [原 SCWS 仓库](https://github.com/hightman/scws)查阅。旧 PHP 扩展不再作为 2.0 可构建组件发布。

## 许可

引擎代码许可证见 [COPYING](COPYING)，词典包另附各上游数据来源的许可。
