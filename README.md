# SCWS 2.0

面向社区、CMS 等通用全文检索的纯 C 词典分词工具包。仅支持 UTF-8；运行时无第三方依赖。词典保留最多两个 ASCII 字符的词性，不包含 IDF。

当前已提供词典流水线、UTF-8 核心库和 CLI 原型，PHP 扩展随后重写。尚未通过真实检索质量验收。开发状态见 [README-2.0.md](README-2.0.md)，C 接口见 [API.md](API.md)。

## 构建与运行

需要 C11 编译器、Make 和 ar；当前 Makefile 支持 macOS/Linux，Windows 构建尚未验证。Python 3.9+ 仅用于词典加工及测试。

```sh
make -j2
make check
build/scws -d build/dict/dictionary.xdb --search -i '中文分词测试'
printf '南京市长江大桥\n' | build/scws -d build/dict/dictionary.xdb
```

构建生成静态库、共享库、CLI 和词典编译器。`make install PREFIX=/path/to/prefix` 安装库、头文件、工具及规则数据，不自动安装下载词典。CLI 默认从标准输入读取全部 UTF-8 文本，以 TSV 输出字节跨度、字符跨度、词性、种类、来源和原词。`--bigrams` 可在搜索模式开启未知汉字二元兜底，`--symbols` 保留符号。

## 词典

```sh
mkdir -p build
make build/scws2-dict
python3 tools/dict.py fetch
python3 tools/dict.py build
python3 -m unittest discover -s tests -p 'test_dictionary.py' -v
```

来源锁定、加工、许可及使用限制见 [dict/README.md](dict/README.md)，文件格式见 [dict/FORMAT.md](dict/FORMAT.md)。只有显式 `fetch` 联网。

## 规则与迁移

简体、繁体规则分别使用 `etc/rules.ini`、`etc/rules_cht.ini`，均为 UTF-8，只保留符号、中文数字和数量单位。内置识别支持明确的中文整数、小数、金融数字、序数和数字加单位，不进行姓名、地名猜测或词性倍率打分。修改数据后执行 `make rules && make`，运行时不依赖 INI；详细边界见 [etc/README.md](etc/README.md)。

2.0 不兼容 1.x 的 API 和 XDB。旧字符集代码、旧引擎、历史 PHP 演示及配套构建已移除，可从 Git 历史或主分支查阅。旧 PHP 扩展不再作为 2.0 可构建组件发布。

引擎代码许可证见 [COPYING](COPYING)，词典另附上游数据许可。
