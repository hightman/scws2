# Repository Guidelines

## 范围与顺序

当前为 `codex/scws-2.0` 的独立开发 worktree。目标是纯 C、UTF-8 的词典分词库、CLI 和 PHP 扩展，接受新 API 与新词典格式，删除关键词提取和 IDF。严格按“词典 → 核心库及测试 → PHP 扩展”推进，不提前改写 PHP 封装。

## 项目结构与事实来源

2.0 状态以根目录 `README.md` 为准，公共 API 契约见 `API.md`。`include/scws.h` 为公共接口，`src/` 为核心与 XDB2 加载器，`cli/scws.c` 为 CLI。`dict/README.md` 描述词典流水线，`dict/FORMAT.md` 定义 XDB2 字节契约，`dict/sources.lock.json` 锁定上游。`dict/build.py` 负责获取和加工，`cli/scws2-dict.c` 为纯 C 编译器；`tests/` 包含离线词典/核心集成测试及原生 C API 测试。

旧引擎、字符集层、Autotools 构建及历史演示已移除；Git 历史和主分支保留 1.x 实现。`phpext/` 等待核心稳定后重写。`src/rules.ini` 为简繁共用的 UTF-8 规则源，仅维护符号、数字及单位，契约见 `src/RULES.md`。不要修改另一个 worktree 或清理用户原有文件。

## 开发与验证

```sh
make -j2
make check
python3 dict/build.py fetch
python3 dict/build.py build
python3 dict/build.py check build/dict/dictionary.xdb
build/scws -d build/dict/dictionary.xdb --search -i '中文分词测试'
```

只有显式词典 `fetch` 或 `make fetch-eval` 联网；默认测试不依赖上游或本机词典。`build/`、`.cache/` 和 Python 缓存不提交，源词典快照应另行归档。运行库与词典编译器保持纯 C；Python 用于构建时生成规则表、离线加工、校验和测试。`make` 根据 `src/rules.ini` 和生成器的修改时间自动更新 `build/rules_data.h`；该文件及其他生成代码不提交 Git。`make rules` 可手动强制生成。

核心变更需要验证主路径、重叠补词、原文偏移、非法 UTF-8、词典损坏、上下文/结果独立生命周期、嵌套结果遍历、上下文错误隔离、词典替换失败保持原状态及独立上下文线程安全。`CORE_TEST_CFLAGS`、`DICT_TEST_CFLAGS` 可覆盖测试编译选项以启用 ASan/UBSan。不要将代码测试通过等同于真实检索质量验收。

## 编码与数据规范

C11 使用 Tab 缩进和 `snake_case`；Python 使用四空格和标准库。没有额外格式化工具。文件格式采用显式 little-endian 编码，不直接写 C 结构体。新格式变更同步文档、校验器和测试。对源数据、来源许可和人工修订保持追踪，禁止直接合并不同口径的频率或引入 IDF。核心行为变化须用真实切分或检索案例验证。

词性作为最多两个 ASCII 字符的可选元数据保留在加工词表和 XDB2 中，离线词表未知值为空；运行时 `SCWS_DICT_TEXT` 缺省为 `un`，不参与评分。jieba `nrt`、`nrfg` 显式映射为 `nr`；未定义的长标签报错，禁止直接截断。姓名、地名、词性倍率规则已决定移除，不自动恢复。数字规则只处理明确形式，不强行猜测范围、约数或词义。规则数据变更通过 `make` 自动重新生成 C 表后编译；只提交规则源和生成器，不提交生成代码，运行时不读取 INI。

## 提交与交付

提交聚焦一个阶段，简短说明具体变化；PR 写明行为、验证命令、词典来源与剩余限制。不要提交下载的上游数据或未通过质量评估的正式发布标签。默认用简体中文沟通，先说明结果与影响，明确区分基础流水线完成和词典质量验证完成。

公开 C API 使用不透明指针句柄 `scws_t`，其他公开类型统一为 `scws_*_t`。上下文内部拥有全部词典，调用者通过路径加载，由 `scws_free()` 统一释放。普通选项与高级未知单字代价分开设置，默认不开符号和未知字二元补词。

公开语料质量和本机吞吐评测见 `tests/evaluation/README.md`，通过 `make eval` 执行，不纳入默认 `make check`。语料只进 `.cache/evaluation/`，结果和编译产物进 `build/evaluation/`；发布到 README 的数字须注明词级指标、数据集、词典配置和计时口径。

`scws_segment()` 返回独立拥有 token 和游标的 `scws_result_t`，NULL 表示错误，空输入成功返回零 token 结果。调用者使用 `scws_result_free()` 释放，结果不依赖 ctx 或词典生命周期。错误保存在 ctx 中，`scws_error_code(ctx)` / `scws_strerror(ctx)` 只查询；上下文操作成功清错，不使用全局错误变量。

运行时文本词典由 `src/text.c` 编译、`src/text_cache.c` 缓存；缓存版本随解析、评分、标签映射或格式变化递增。词典参数为位标志，仅 TEXT / MMAP，0 表示 XDB 内存加载；不猜测格式或记忆模式，mmap 不可用时退回内存。缓存修改需验证并发锁、原子发布、跨进程复用、失效重建及各标志组合。词性说明维护在 `dict/POS.md`。
