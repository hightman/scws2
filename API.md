# SCWS 2 C API

公共头文件为 `include/scws.h`，仅接受 UTF-8，与 1.x API 不兼容。运行时无第三方依赖；mmap 使用操作系统接口。无字符集切换、关键词提取和 IDF 接口。

## 类型与接口

`scws_t` 是不透明指针句柄（`typedef struct scws *scws_t`），直接写 `scws_t ctx`，无需再加 `*`。
枚举使用 `scws_status_t`、`scws_dict_mode_t`、`scws_mode_t`、`scws_kind_t`、`scws_origin_t`；结果值类型为 `scws_token_t`，读取时使用 `const scws_token_t *`。这些类型均由本库定义，`_t` 是项目命名惯例。

| 函数 | 用途 |
| --- | --- |
| `scws_t scws_new(void)` | 创建上下文，分配失败返回 NULL |
| `void scws_free(scws_t ctx)` | 释放上下文、全部词典及结果 |
| `scws_status_t scws_set_dict(scws_t ctx, const char *path, scws_dict_mode_t mode)` | 替换全部词典 |
| `scws_status_t scws_add_dict(scws_t ctx, const char *path, scws_dict_mode_t mode)` | 追加叠加词典 |
| `scws_status_t scws_set_options(scws_t ctx, unsigned flags)` | 覆盖行为选项 |
| `scws_status_t scws_set_unknown_cost(scws_t ctx, uint32_t cost)` | 高级未知单字代价调节 |
| `scws_status_t scws_segment(scws_t ctx, const char *text, size_t length, scws_mode_t mode)` | 执行分词 |
| `const scws_token_t *scws_next(scws_t ctx)` | 依次读取结果，结束返回 NULL |
| `size_t scws_count(scws_t ctx)` | 返回总结果数；空上下文返回 0 |
| `const char *scws_strerror(scws_status_t status)` | 返回静态错误说明 |

## 最小用法

```c
#include <stdio.h>
#include <string.h>
#include "scws.h"

int main(void)
{
    scws_t ctx = scws_new();
    if (!ctx) return 1;
    const char *text = "中文分词测试";
    scws_status_t status = scws_set_dict(ctx, "build/dict/dictionary.xdb", SCWS_DICT_MEMORY);
    if (!status) status = scws_set_options(ctx, SCWS_KEEP_SYMBOLS);
    if (!status) status = scws_segment(ctx, text, strlen(text), SCWS_SEARCH);
    if (!status) {
        const scws_token_t *token;
        while ((token = scws_next(ctx)) != NULL) {
            fwrite(text + token->offset, 1, token->length, stdout);
            printf("\t%s\n", token->pos);
        }
    }
    if (status) fprintf(stderr, "%s\n", scws_strerror(status));
    scws_free(ctx);
    return status != SCWS_OK;
}
```

构建后可用 `cc -Iinclude example.c build/libscws.a -o example` 链接静态库。

## 词典与生命周期

`scws_new()` 不加载词典，创建后即可使用内置英文、数字、数量规则及未知汉字单字兜底。需要正常中文词典切分时调用 `scws_set_dict()`。词典结构和打开/关闭函数属于内部接口，调用者仅管理上下文。

`scws_set_dict(ctx, path, mode)` 成功后替换全部已有词典；`scws_add_dict()` 按顺序追加，同一完整词以后添加的定义为准，前缀信息取并集。两者失败均保留已有词典。`path` 不可为空，不自动寻找词典或隐式加载默认文件。修改词典影响后续分词，不重新计算已保存的结果。

加载方式显式传入，默认模式值为 0：

- `SCWS_DICT_MEMORY`（0）：整库 `fread` 到私有缓冲区；加载后不依赖原文件。
- `SCWS_DICT_MMAP`（1）：只读映射 `PROT_READ`、`MAP_SHARED`，不整库复制或预读，多进程可共享文件页缓存。失败不自动退回 MEMORY；不支持时返回 `SCWS_ENOTSUP`。POSIX 构建启用，可用 `-DSCWS_NO_MMAP` 禁用。

两种模式均读取 XDB2 schema 4，最多接受 512 MiB；只检查头部、尺寸、桶表范围、节点数量基本范围及尾标记，不遍历词条。完整检查由构建流水线执行。查询保留节点范围及遍历步数保护，不保证发现所有中间数据损坏。

映射使用期间不得原地修改或截断文件。更新应生成新文件再替换路径，已有上下文继续引用旧文件，重新加载才使用新文件。两种模式都不自动监测更新。

`scws_free()` 自动关闭全部词典并释放结果、上下文。各上下文独立拥有加载的数据；内存模式多次加载同一文件会产生独立副本，目前没有全局缓存或跨上下文词典句柄共享。不同线程可分别使用各自上下文，同一个上下文不能并发操作。

叠加词典可把完整词标为只补词以阻止其参与主切分。当前没有运行时禁用词的 tombstone 标志，构建阶段的 `deny` 不等于删除另一份叠加词典中的词。

## 分词模式与结果

`scws_segment(ctx, text, byte_length, mode)` 完整校验 UTF-8 后计算结果。输入仅在调用期间使用，不保存输入指针。传入明确长度，可包含作为分隔符的 NUL。非法 UTF-8、非法参数或分配失败会清空旧结果，不返回部分切分。

- `SCWS_STANDARD`：以词典候选、明确的数字及数量候选构成 DAG，通过动态规划输出不重叠的主切分。
- `SCWS_SEARCH`：保留主词，增加有 SEARCH 资格的多字符词典匹配（可以跨主词边界）和英文/数字片段。

评分仅使用切分代价，不使用词性。默认未知字符代价为 18432，英文、数字及数量整体代价为 8192，均按 1024 缩放；这是初始参数，尚未通过真实检索评估调优。同分时依次优先更少词、较长的当前词和词典来源。

`scws_set_options(ctx, flags)` 覆盖全部标志，默认均关闭，不影响评分参数：

- `SCWS_KEEP_SYMBOLS`：输出符号；空白和控制分隔符始终跳过。
- `SCWS_FALLBACK_BIGRAM`：仅在搜索模式下，对主路径中连续的未知汉字单字增加二元片段，词性为空。默认关闭，不跨已识别词或空白生成片段。

`scws_set_unknown_cost(ctx, cost)` 为高级调参接口，默认值由创建函数设为 18432，正常使用无需调用。cost 必须大于零；越低越容易采用未知单字。两个设置接口只影响后续分词，参数无效时保持原配置。

结果字段（`scws_token_t`）：

| 字段 | 含义 |
| --- | --- |
| `offset`, `length` | 原文 UTF-8 字节跨度 |
| `char_offset`, `char_length` | Unicode 标量跨度，不是字形簇或搜索引擎 token position |
| `pos` | 最多两个 ASCII 字符，C 字符串以 NUL 结尾；未知为空 |
| `kind` | `SCWS_PRIMARY`、`SCWS_SECONDARY`、`SCWS_FALLBACK` |
| `origin` | `SCWS_DICTIONARY`、`SCWS_BUILTIN`、`SCWS_UNKNOWN` |

同一原文跨度只输出一次，主词优先；结果按字节起点递增，同起点长词在前。`scws_next` 返回上下文拥有的结果指针，下一次 `scws_segment` 或释放上下文时失效。`scws_count` 返回总结果数，不随遍历递减。

英文和数字可与内部 `- _ . / & '` 连接，字母片段可带末尾 `+ #`；例如 `GPT-4o`、`C++`、`Wi-Fi`、`iPhone16Pro`。全角 ASCII 仅采用相同的字符分类，不改写词面或原文偏移。没有内置大小写转换、简繁转换或完整 Unicode 词边界模型。

中文数字支持明确的数字串、位值整数、金融数字、小数、序数与数字加单位。`每` 必须带数字和单位；单个中文数字仅在带前缀或单位时生成候选。整体候选标为 `m` 或 `mq`，来源为 `SCWS_BUILTIN`，仍与词典竞争。搜索模式对选中的数字跨度只补出完整数值和单位，不拆小数、不补跨数字边界的词典片段。范围/约数、负数、分数、多级规模和数值转换不在当前范围。

## 当前检索边界

搜索模式的词典补词尚无经过评估的歧义置信度过滤。例如旧基础词典可能包含“京市”“据库”，某些上下文也会召回不同切分路径的词。应用应区分主词、补词与兜底片段，不能把所有输出无差别当成必选查询条件。需要真实检索评估后再确定默认补词策略和权重。

`src/rules.ini` 统一保留简繁符号、数字和数量单位数据。`make` 根据规则源和生成器的修改时间自动生成 `build/rules_data.h`，重新编译后生效；生成代码不进入 Git，规则源不作为运行配置安装，运行时不读取 INI。旧词性倍率、姓名、地名及统计规则已移除。数据约束和支持边界见 `src/RULES.md`。

## 状态码

除创建、释放和结果读取外，操作返回 `scws_status_t`。`scws_strerror()` 返回静态错误说明，无需释放。

| 状态码 | 含义 |
| --- | --- |
| `SCWS_OK` | 成功 |
| `SCWS_EINVAL` | 空上下文、无效参数、模式或选项 |
| `SCWS_ENOMEM` | 内存分配失败 |
| `SCWS_EIO` | 文件读取或映射等 I/O 错误 |
| `SCWS_EFORMAT` | 词典格式错误 |
| `SCWS_EUTF8` | 输入不是有效 UTF-8 |
| `SCWS_ELIMIT` | 超出资源或尺寸限制 |
| `SCWS_ENOTSUP` | 当前构建不支持指定加载方式 |

`scws_new()` 分配失败返回 `NULL`。`scws_free(NULL)` 可安全调用。
