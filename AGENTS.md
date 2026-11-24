# Repository Guidelines

## 范围与顺序

当前为 `codex/scws-2.0` 的独立开发 worktree。目标是纯 C、UTF-8 的词典分词库、CLI 和 PHP 扩展，接受新 API 与新词典格式，删除关键词提取和 IDF。严格按“词典 → 核心库及测试 → PHP 扩展”推进，不提前改写 PHP 封装。

## 项目结构与事实来源

2.0 状态以 `README-2.0.md` 为准。`dict/README.md` 描述词典流水线，`dict/FORMAT.md` 定义 XDB2 字节契约，`dict/sources.lock.json` 锁定上游。`tools/dict.py` 负责获取和加工，`tools/scws2-dict.c` 为无第三方依赖的 C 编译器，`tests/test_dictionary.py` 为离线集成测试。

旧引擎、字符集层、Autotools 构建及历史演示已移除；Git 历史和主分支保留 1.x 实现。`phpext/` 等待核心稳定后重写。`etc/rules.ini`、`etc/rules_cht.ini` 均为 UTF-8，暂不执行旧 INI 规则。不要修改另一个 worktree 或清理用户原有文件。

## 开发与验证

```sh
mkdir -p build
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror tools/scws2-dict.c -o build/scws2-dict
python3 tools/dict.py fetch
python3 tools/dict.py build
python3 tools/dict.py check build/dict/dictionary.xdb
python3 -m unittest discover -s tests -p 'test_dictionary.py' -v
```

只有显式 `fetch` 联网；测试不依赖上游或本机词典。`build/`、`.cache/` 和 Python 缓存不提交，源词典快照应另行归档。运行库与编译器保持纯 C；Python 只用于离线加工、校验和测试。

## 编码与数据规范

C11 使用 Tab 缩进和 `snake_case`；Python 使用四空格和标准库。没有额外格式化工具。文件格式采用显式 little-endian 编码，不直接写 C 结构体。新格式变更同步文档、校验器和测试。对源数据、来源许可和人工修订保持追踪，禁止直接合并不同口径的频率或引入 IDF。核心行为变化须用真实切分或检索案例验证。

词性作为最多两个 ASCII 字符的可选元数据保留在加工词表和 XDB2 中，未知值为空，不参与评分。jieba `nrt`、`nrfg` 显式映射为 `nr`；未定义的长标签报错，禁止直接截断。旧 `rules.ini` 词性规则的设计尚未确定，不因保留标签而自动迁移或启用。

## 提交与交付

提交聚焦一个阶段，简短说明具体变化；PR 写明行为、验证命令、词典来源与剩余限制。不要提交下载的上游数据或未通过质量评估的正式发布标签。默认用简体中文沟通，先说明结果与影响，明确区分基础流水线完成和词典质量验证完成。
