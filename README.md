# SCWS 2.0

面向社区、CMS 等通用全文检索的纯 C 词典分词工具包。仅支持 UTF-8；运行时无第三方依赖。词典保留最多两个 ASCII 字符的词性，不包含 IDF。

当前阶段：词典流水线已完成，UTF-8 核心库开发中，PHP 扩展随后重写。开发状态见 [README-2.0.md](README-2.0.md)。

## 词典

```sh
mkdir -p build
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror tools/scws2-dict.c -o build/scws2-dict
python3 tools/dict.py fetch
python3 tools/dict.py build
python3 -m unittest discover -s tests -p 'test_dictionary.py' -v
```

来源锁定、加工、许可及使用限制见 [dict/README.md](dict/README.md)，文件格式见 [dict/FORMAT.md](dict/FORMAT.md)。只有显式 `fetch` 联网。

## 规则与迁移

简体、繁体规则分别使用 `etc/rules.ini`、`etc/rules_cht.ini`，文件内容均为 UTF-8。旧词性语法规则的后续用途尚未确定，保留数据不代表已在新核心中执行。

2.0 不兼容 1.x 的 API 和 XDB。旧字符集代码、旧引擎、历史 PHP 演示及配套构建已移除，可从 Git 历史或主分支查阅。旧 PHP 扩展不再作为 2.0 可构建组件发布。

引擎代码许可证见 [COPYING](COPYING)，词典另附上游数据许可。
