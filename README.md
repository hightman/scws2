# SCWS 2.0

面向社区、CMS 等通用全文检索的纯 C 词典分词工具包，项目暂沿用 SCWS 名称。仅支持 UTF-8；运行时无第三方依赖，不提供关键词提取和 IDF。C 接口见 [API.md](API.md)。

## 开发状态

按“词典 → 核心库及测试 → PHP 扩展”的顺序推进：

| 模块 | 当前状态 |
| --- | --- |
| 词典 | 已提供来源锁定、人工修订、可复现 XDB2 构建、校验及词表差异报告；仍需补充新词来源和质量评估 |
| 核心库与 CLI | 已实现上下文内部管理词典、严格 UTF-8、英文数字处理、候选 DAG、动态规划、standard/search 模式、叠加词典及可区分来源的补词；已有生命周期和独立上下文并发测试 |
| PHP 扩展 | 尚未实现，待核心稳定后以对象接口薄封装 C 核心 |

当前是可运行原型，尚未通过真实检索质量和完整跨平台验收。搜索补词的歧义筛选仍需评估，算法替换不会自动解决旧词和新词不足问题。正式发布前须用分词及小型检索评估集衡量召回、误召回、词数、内存和速度；未验证的词典更新不自动发布。

## 构建与运行

需要 C11 编译器、Make、ar 和 Python 3.9+。Makefile 面向 macOS/Linux，Windows 构建尚未验证。Python 用于构建时生成规则表、词典加工及测试，运行时不需要。

```sh
make -j2
make check

# 首次使用：下载锁定的词典源并构建 XDB2
python3 dict/build.py fetch
python3 dict/build.py build

build/scws -d build/dict/dictionary.xdb --search -i '中文分词测试'
printf '南京市长江大桥\n' | build/scws -d build/dict/dictionary.xdb
```

只有显式 `fetch` 联网，构建和测试不会自动下载词典。构建生成静态库、共享库、CLI 和词典编译器。`make install PREFIX=/path/to/prefix` 安装库、头文件和工具，不自动安装下载词典。

CLI 可不传 `-d`，此时仅使用内置规则和未知字符兜底。默认读取全部标准输入，以 TSV 输出字节跨度、字符跨度、词性、种类、来源和原词。`--search` 增加检索补词，`--bigrams` 可在搜索模式开启未知汉字二元兜底，`--symbols` 保留符号。

## 分词用例对照

```sh
make cases                       # 逐句观察，有差异也正常结束
make check-cases                 # 严格比较，有差异返回失败
python3 tools/check_segments.py --case case-07-standard --case case-07-search
```

逐句显示输入、分词、词性与预期差异，支持自定义 JSON 用例和叠加词典。
样例集中在 `tests/cases/`。每句分别验证 standard 和 search，预期以已接受的新版结果为准；第 7 句保留已知主路径缺陷，严格比较会报告失败。配置与退出码见 [用例说明](tests/cases/README.md)。

## 词典

词典默认整库加载到内存；C API 先用 `scws_t ctx = scws_new()` 创建上下文，再用 `scws_set_dict(ctx, path, SCWS_DICT_MEMORY)` 加载词典，或传 `SCWS_DICT_MMAP` 选择只读映射；`scws_free(ctx)` 统一释放，契约见 [API.md](API.md)。映射模式不自动回退到内存加载。

当前唯一接入的上游是 jieba 标准词典，THUOCL 尚未融入。词性最多两个 ASCII 字符，`nrt`、`nrfg` 在加工时归并为 `nr`，缺失时为空；词性不参与切分评分。

来源锁定、加工、许可及使用限制见 [dict/README.md](dict/README.md)，文件格式见 [dict/FORMAT.md](dict/FORMAT.md)。可单独运行词典测试：

```sh
python3 -m unittest discover -s tests -p 'test_dictionary.py' -v
```

## 规则与迁移

规则统一在 `src/rules.ini`，同一份 UTF-8 数据同时维护简繁写法，只保留符号、中文数字和数量单位。内置识别支持明确的中文整数、小数、金融数字、序数和数字加单位，不进行姓名、地名猜测或词性倍率打分。

`make` 按规则源和生成器的修改时间自动更新 `build/rules_data.h` 并重新编译，`make rules` 可手动强制生成。生成代码不进入 Git，运行时不读取 INI；详细边界见 [src/RULES.md](src/RULES.md)。

2.0 不兼容 1.x 的 API 和 XDB。旧字符集代码、旧引擎、Autotools 构建及历史 PHP 演示已移除，可从 Git 历史或主分支查阅。旧 PHP 扩展不再作为 2.0 可构建组件发布。

## 许可

引擎代码许可证见 [COPYING](COPYING)，词典包另附各上游数据来源的许可。
