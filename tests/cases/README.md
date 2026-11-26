# 分词用例

`segmentation.json` 保存 16 个句子，分别测试 standard、search，共 32 项。
使用默认选项（不保留符号、不启用未知字二元补词）。预期基于当前锁定词典及已接受的 scws2 分词结果。

```sh
make cases                       # 逐句观察，差异不影响退出码
make check-cases                 # 严格比较，有差异时失败
make cases CASES=/path/to/cases.json CASE_DICT=/path/to/dict.xdb
python3 tools/check_segments.py --case case-07-standard --case case-07-search
```

先执行 `make` 并准备词典。工具逐句显示输入、分词、词性和差异；search 另列主词与补词。
`?` 表示未知词性。数组差异和原文字符范围采用零基半开区间；词性差异的 token 编号从 1 开始。

## 配置字段

UTF-8 JSON 顶层为 `{"cases": [...]}`，每项支持：

| 字段 | 含义 |
| --- | --- |
| `id`、`text` | 唯一标识与输入句子，必填 |
| `mode` | `standard`（默认）或 `search` |
| `expected` | 完整输出词序列，含搜索补词，按输出顺序比较 |
| `expected_primary` | 只检查主路径，可与 `expected` 同时或单独使用 |
| `expected_pos` | 可选词性数组，须与 `expected` 等长；空字符串表示未知 |
| `symbols`、`bigrams` | 默认 false；bigrams 仅用于 search |
| `note` | 显示用例备注 |

例如：

```json
{"cases": [{"id": "simple", "text": "中文分词", "mode": "standard", "expected": ["中文", "分词"]}]}
```

无任何预期时只展示，标记 REVIEW；空数组表示期望无输出。额外 `-d` 按顺序添加叠加词典。
退出码：0 无差异，1 存在差异，2 配置或执行错误；`--observe` 允许差异，但不忽略执行错误。
工具只读配置，每句启动一次 CLI，不用于性能测量。

## 已知缺陷

第 7 句“哪个人生下来就会算算术呢”应切为“哪个／人／生下／来／就／会／算／算术／呢”，
当前主路径误切为“哪个／人生／下来／就／会／算／算术／呢”。两种模式均保留失败，
search 仅以正确主路径为预期，避免补词掩盖切分缺陷。
这两项不加入 `make check` 的自动化测试门槛；`make check-cases` 会持续报告，直到缺陷修复。
