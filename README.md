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

## 构建与运行

需要支持 C11 的 GCC/Clang、GNU Make、ar 和 Python 3.9+。当前已在 macOS 验证；Linux 及其他平台尚未完成验证，系统默认 make 不是 GNU Make 时需使用 `gmake`。Python 用于构建时生成规则表、词典加工及测试，运行时不需要。

```sh
make -j2
make check

# 首次使用：下载锁定的词典源并构建 XDB2
python3 dict/build.py fetch
python3 dict/build.py build

build/scws2 -d build/dict/dictionary.xdb --search -i '中文分词测试'
printf '南京市长江大桥\n' | build/scws2 -d build/dict/dictionary.xdb
```

只有显式 `fetch` 联网，构建和测试不会自动下载词典。构建生成静态库、共享库、CLI 和词典编译器。

辅助程序统一放在 `tools/`：`scws2.c` 用于分词测试，`scws2-dict.c` 用于编译词典，与评测及规则生成工具共同维护。

CLI 主要用于开发测试，调用层显式使用 `SCWS2_DICT_MEMORY`，默认整库读入内存。`-d file` 根据扩展名选择格式：`.txt`（大小写不敏感）为 UTF-8 文本，其余为 XDB2。`--mmap` 为后续词典参数请求 mmap；不可用时退回内存。可重复 `-d` 叠加词典，后加载的同词定义优先。

CLI 可不传 `-d`，此时仅使用内置规则和未知字符兜底。默认读取全部标准输入，以 TSV 输出字节跨度、字符跨度、词性、种类、来源和原词。`--search` 增加检索补词，`--bigrams` 可在搜索模式开启未知汉字二元兜底，`--symbols` 保留符号。

## 安装与构建参数

`make install` 默认安装到 `/usr/local`，`PREFIX` 不必填写。自定义目录可直接传 Make 变量，无需先设置环境变量：

```sh
make install PREFIX="$HOME/.local"

# 打包暂存：写入 /tmp/scws2-package/usr/local/，不修改系统安装目录
make install PREFIX=/usr/local DESTDIR=/tmp/scws2-package
```

安装内容为 `include/scws2.h`、`lib/libscws2.a`、`lib/libscws2.so`（macOS 为 `.dylib`），以及 `bin/scws2`、`bin/scws2-dict`。不安装词典、运行时规则或 PHP 扩展。词典路径由调用方传入；规则已编译进库；PHP 扩展从完整仓库单独通过 phpize 构建，无需先安装 libscws2，见 [扩展说明](phpext/README.md)。

| 变量 | 默认值 | 用途 |
| --- | --- | --- |
| `PREFIX` | `/usr/local` | 安装前缀，影响 include/lib/bin 位置 |
| `DESTDIR` | 空 | 打包暂存根目录，实际位置为 `DESTDIR + PREFIX` |
| `CC` / `AR` | `cc` / `ar` | C 编译器及静态库归档工具 |
| `PYTHON` | `python3` | 构建及测试使用的 Python 解释器 |
| `CFLAGS` | `-O2` | 编译选项；C11、警告和 PIC 选项由 Makefile 另外添加 |
| `LDFLAGS` | 空 | 链接选项 |

例如 `make CC=clang CFLAGS='-O0 -g'` 可生成调试构建。修改编译器或编译选项后先运行 `make clean`，Make 不会自动跟踪变量值变化。`make clean` 会删除整个 `build/`，包括已构建词典和本地评测输出；下载源保留在 `.cache/`，词典可重新构建。命令行变量优先于环境变量，且不会保存到后续 make 调用；需要连续使用时，应重复传入或显式导出环境变量。

当前不使用 configure 探测 mmap；`src/xdb.c` 在 Unix/macOS 平台宏存在且未定义 `SCWS2_NO_MMAP` 时编译 mmap 路径，其余平台使用内存加载。可运行 `make -B CFLAGS='-O2 -DSCWS2_NO_MMAP'` 强制重建为禁用 mmap 的版本，无需删除已有词典；编译选项须在后续构建中保持一致。运行时映射失败也自动回退 fread。

C 程序可使用 `cc -I"$HOME/.local/include" example.c "$HOME/.local/lib/libscws2.a" -o example` 链接本地安装的静态库。使用共享库时，需按目标系统配置运行时库搜索路径。

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

`make` 按规则源和生成器的修改时间自动更新 `build/rules_data.h` 并重新编译，`make rules` 可手动强制生成。生成代码不进入 Git，运行时不读取 INI；详细边界见 [src/RULES.md](src/RULES.md)。

2.0 不兼容 1.x 的 API 和 XDB。旧字符集代码、旧引擎、Autotools 构建及历史 PHP 演示已移除，可从 [原 SCWS 仓库](https://github.com/hightman/scws)查阅。旧 PHP 扩展不再作为 2.0 可构建组件发布。

## 许可

引擎代码许可证见 [COPYING](COPYING)，词典包另附各上游数据来源的许可。
