# scws2 评测

使用 SIGHAN 2005 的 PKU、MSR 公共标注测试集。背景见 [原始论文](https://aclanthology.org/I05-3017/)，下载使用[数据发布镜像的固定提交](https://github.com/yuikns/icwb2-data/tree/873a51fa6234c644126d2f2c5c603a97809d2e49)。
`sources.json` 锁定文件 URL、SHA-256 和尺寸。语料只进 `.cache/evaluation/`，使用范围以原发布说明为准，不提交语料或二进制。

## 运行

需要 C 编译器、Python 3.9+、Perl 和 `/usr/bin/diff`。先准备当前 XDB2 词典：

```sh
make
make dict                       # 需要已获取的词典源
make fetch-eval                 # 显式下载锁定的公开语料和评分脚本
make eval                       # 只评测当前 scws2
make eval EVAL_ARGS='--samples 3 --seconds 0.2'  # 缩短试跑
```

`make eval` 不联网，不依赖其它分词器或旧词典。默认每种配置测 7 轮、每样本目标 1 秒。
自备 UTF-8 标注文件时，每行一句，用空白分隔标准词：

```sh
python3 tools/evaluate.py --reference /path/to/gold.utf8 --output build/evaluation/custom
```

工具去除标注空白生成相同原文，忽略空行。测试集支持 `--dataset pku` 或 `--dataset msr`。

## 指标与计时

正确词须与 gold 的起止位置完全一致，含标点。Precision = 正确词数 / 输出词数，Recall = 正确词数 / 标注词数，F1 = 2PR / (P+R)；汇总全语料词数后计算。
驱动直接返回原文跨度，标准模式的 memory/mmap 输出必须一致。公开集结果与发布版 Perl 评分脚本的三位小数交叉核对；原脚本以 diff 对齐词序列，这里以严格跨度计数为准。训练词表仅供该脚本统计 OOV，不参与词典训练或调参。

速度采用原生单线程 C 驱动，先预加载语料和词典并完整预热一遍。计时包含分词、期间内存分配、结果遍历及 `scws2_result_free()`，排除编译、词典加载、输入读取和格式化输出。配置按种子 0 打乱顺序交错测量，取每遍耗时中位数。
MiB/s 按去除标注空白、不含换行的 UTF-8 输入字节数计算（1 MiB = 1,048,576 字节）。search 含重叠词，只测吞吐，不套用唯一切分的 F1，也不把该指标当作检索召回率。

当前测试开启符号输出，关闭未知字二元补词。CPU 调度、频率和后台负载会影响速度；这是热缓存短句吞吐，不是 PHP 请求吞吐或冷启动性能。

## 当前数据与记录

简介只展示当前指标，见根目录 [README](../../README.md)。`pku.json`、`msr.json` 只保留当前实现的计数、计时样本、环境、源码与词典哈希；公开集分别有 1,944 / 3,985 个非空句、104,372 / 106,873 个标注词。

运行输出在 `build/evaluation/{pku,msr}/`：

- `results.json`：本次指标和计时；`source_sha256` 标识实际源码，`source_modified` 说明测量时是否有未提交的源码改动。
- `.spans`、`.utf8`：原文跨度和可读分词。
- `*-sighan.txt`：发布版评分脚本输出。
- `compile.log`：当前驱动编译命令。

分词标准不同会造成数量整体与数值/单位分拆等粒度差异；也有真实歧义、英数边界和未登录词错误。公开集较早，未核验词典来源与其是否重叠，不能作为闭集训练榜单成绩或当前社区/CMS 检索效果的证明。正式发布仍需真实检索验证。

`pku.json`、`msr.json` 是独立命名前保存的实测快照，保留原提交 ID、文件路径、源码哈希及配置名称用于追溯，不改写为迁移后的名称，也不表示已重新计时。当前工具使用 `scws2` 命名；仅名称和仓库拆分不改变切分算法。原提交 ID 对应来源仓库新版开发分支的迁移前记录。
