# SCWS 2 C API

公共头文件为 `include/scws.h`。仅接受 UTF-8，API 与 1.x 不兼容。运行时只使用 C 标准库；无字符集切换、关键词提取和 IDF 接口。

## 最小用法

```c
#include <stdio.h>
#include <string.h>
#include "scws.h"

int main(void)
{
    scws_dict *dict = NULL;
    scws *ctx = NULL;
    const char *text = "中文分词测试";
    scws_status status = scws_dict_open("build/dict/dictionary.xdb", &dict);
    if (!status) status = scws_new(dict, &ctx);
    if (!status) status = scws_segment(ctx, text, strlen(text), SCWS_SEARCH);
    if (!status) {
        const scws_token *token;
        while ((token = scws_next(ctx)) != NULL) {
            fwrite(text + token->offset, 1, token->length, stdout);
            printf("\t%s\n", token->pos);
        }
    }
    scws_free(ctx);
    scws_dict_close(dict);
    return status != SCWS_OK;
}
```

构建后可用 `cc -Iinclude example.c build/libscws.a -o example` 链接静态库。

## 词典与生命周期

`scws_dict_open(path, &dict)` 读取 XDB2 schema 4，只检查头部、声明文件尺寸、桶表范围、节点数量的基本范围及固定尾标记；不遍历词条。词典须由构建流水线生成并通过独立检查器验收，打开不保证发现中间数据损坏。查询仅保留节点范围与遍历步数保护。两种模式最多接受 512 MiB 文件；不支持旧 XDB 或旧 schema。

`scws_dict_open()` 等价于 `scws_dict_open_ex(path, SCWS_DICT_MEMORY, &dict)`，默认整库 `fread` 到私有缓冲区。显式选择只读文件映射：

```c
scws_dict_open_ex(path, SCWS_DICT_MMAP, &dict);
```

`SCWS_DICT_MMAP` 使用 `PROT_READ`、`MAP_SHARED` 的只读映射，按需访问文件页，不整库复制或预读；不同进程可共享文件页缓存。两种模式共用查询和轻量检查，结果一致；可分别为主词典和叠加词典选择模式。`scws_dict_close()` 自动释放缓冲区或解除映射。

映射失败不自动回退到内存模式；不支持映射时返回 `SCWS_ENOTSUP`。当前 POSIX 平台启用映射，可用 `-DSCWS_NO_MMAP` 构建仅内存版本。传入未知模式返回 `SCWS_EINVAL`，有有效输出指针时，打开失败将其置为 `NULL`。

映射使用期间不得原地修改或截断词典；请生成新文件再替换路径，已打开的词典继续引用旧文件。内存模式加载完毕后不依赖原文件。两种模式都不自动监测文件更新。

打开后的词典不可变，可供多个线程的独立上下文共享。`scws_new(dict, &ctx)` 不复制词典；调用者必须先释放所有引用它的上下文，再调用 `scws_dict_close`。同一上下文不能并发使用。没有隐式的全局词典或临时磁盘缓存。

`scws_add_dict(ctx, overlay)` 添加已加载词典。同一完整词以后添加的定义为准，前缀信息取并集；把完整词标记为只补词可阻止该词参与主切分。当前没有运行时禁用词的 tombstone 标志，构建阶段的 `deny` 不等于可以删除另一份叠加词典里的词。

## 分词模式与结果

`scws_segment(ctx, text, byte_length, mode)` 完整校验 UTF-8 后计算结果。输入仅在调用期间使用，不保存输入指针。传入明确长度，可包含作为分隔符的 NUL。非法 UTF-8、非法参数或分配失败会清空旧结果，不返回部分切分。

- `SCWS_STANDARD`：以词典候选、明确的数字及数量候选构成 DAG，通过动态规划输出不重叠的主切分。
- `SCWS_SEARCH`：保留主词，增加有 SEARCH 资格的多字符词典匹配（可以跨主词边界）和英文/数字片段。

评分仅使用切分代价，不使用词性。默认未知字符代价为 18432，英文、数字及数量整体代价为 8192，均按 1024 缩放；这是初始参数，尚未通过真实检索评估调优。同分时依次优先更少词、较长的当前词和词典来源。

`scws_set_options(ctx, flags, unknown_cost)` 配置非零未知字符代价和以下选项：

- `SCWS_KEEP_SYMBOLS`：输出符号；空白和控制分隔符始终跳过。
- `SCWS_FALLBACK_BIGRAM`：仅在搜索模式下，对主路径中连续的未知汉字单字增加二元片段，词性为空。默认关闭，不跨已识别词或空白生成片段。

结果字段：

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

API 用 `scws_status` 返回参数、内存、I/O、词典格式、UTF-8 和资源限制错误；`scws_strerror` 给出静态错误说明。输出句柄参数在失败时置空。`scws_free(NULL)`、`scws_dict_close(NULL)` 可安全调用。
