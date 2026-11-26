# scws2 PHP 扩展

扩展名 `scws2`，仅提供一个 `final class Scws2`，最低 PHP 7.0。无函数式调用、公开属性或 C 资源句柄；扩展动态链接已安装的共享库 libscws2，分词核心不编译进模块。完整契约见 [DESIGN.md](DESIGN.md)。

## 构建

先安装核心共享库及头文件，再进入 `phpext/`。构建需要目标 PHP 的开发头文件、phpize、php-config、C11 编译器、Make 和 phpize 所需的 Autoconf；不需要 Python 或规则生成器。`--with-scws2=DIR` 显式指定核心安装前缀，检查头文件、共享库及链接符号，缺失时报错，不回退内嵌编译。
```sh
cd phpext
phpize
./configure --with-scws2=/usr/local/scws2
make -j2
make test

# 安装至该 PHP 的 extension_dir，需要相应目录权限。
make install
```

在该 PHP 使用的 php.ini 中加入 `extension=scws2.so`，通过 `php --ri scws2` 查看模块信息。本扩展不安装词典、不新增 INI 配置。需要中文词典时自行准备 XDB2 或文本词表并传入路径。

机器上存在多个 PHP 时，应使用同一安装目录的工具：

```sh
/path/to/php/bin/phpize
./configure --with-scws2=/usr/local/scws2 --with-php-config=/path/to/php/bin/php-config
make -j2
TEST_PHP_EXECUTABLE=/path/to/php/bin/php make test
```

不同 PHP 版本的模块不可混用；切换版本时使用干净源码副本重新 phpize。也支持在单独构建目录执行生成的 configure。engine 目录只保存一行 include 的编译入口，不复制核心实现；扩展的对象文件和规则表与根目录构建独立，修改核心源、头文件或规则后重新 make 即可更新。

本次受限 macOS 环境中，旧 PHP 自带 libtool 无法读取 kern.argmax，需要在 configure 前指定 `lt_cv_sys_max_cmd_len=65536`，例如 `lt_cv_sys_max_cmd_len=65536 ./configure --with-scws2=/usr/local/scws2`。这是旧构建工具的环境适配，不改变扩展接口或核心行为。

扩展使用 --with-scws2 指定前缀中的头文件和共享库；同一机器上的其他 libscws2 安装不应混入该构建。运行时仍需确认共享库解析到预期安装位置。

## 用法

```php
$scws = (new Scws2())
    ->setDict('/srv/app/dict/dict.xdb')
    ->addDict('/srv/app/dict/user.txt')
    ->setOptions(Scws2::KEEP_SYMBOLS);

$tokens = $scws->segment('中文分词测试', Scws2::SEARCH);
foreach ($tokens as $token) {
    echo $token['word'], "\t", $token['offset'], "\n";
}
$words = array_column($tokens, 'word');
```

| 方法 | 行为 |
| --- | --- |
| `__construct()` | 创建独立上下文，不自动加载词典 |
| `setDict(string $path, int $flags = 0): self` | 替换全部词典，失败保留原状态 |
| `addDict(string $path, int $flags = 0): self` | 追加叠加词典，同词后加载者优先 |
| `setOptions(int $flags): self` | 覆盖全部选项，默认 `0` |
| `segment(string $text, int $mode = Scws2::STANDARD): array` | 完成整段分词，返回全部 token；空结果为 `[]` |

`Scws2::SEARCH` 增加重叠补词。`Scws2::KEEP_SYMBOLS` 保留符号；`Scws2::FALLBACK_BIGRAM` 仅在 SEARCH 下为连续未知汉字单字补二元片段，默认关闭。选项可按位或，词性不参与评分。

每个 token 固定包含：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `word` | string | 原文词面 |
| `offset`、`length` | int | UTF-8 字节跨度，起点从 0 开始 |
| `char_offset`、`char_length` | int | Unicode 标量跨度，不是字形簇或搜索引擎 position |
| `pos` | string | 词性，缺失为空字符串，文本词典缺省为 `un` |
| `kind` | int | `Scws2::PRIMARY`、`SECONDARY`、`FALLBACK` |
| `origin` | int | `Scws2::DICTIONARY`、`BUILTIN`、`UNKNOWN` |

结果按字节起点递增，同起点长词优先。返回数组独立拥有词面及元数据，内部 C 结果已释放；可重复遍历、嵌套分词，并在分词对象销毁后继续使用。一次返回全部结果会有数组内存开销，核心 malloc/mmap 内存也不完整计入 PHP memory_limit。

## 词典加载与错误

文件名后缀 `.txt`（ASCII 大小写不敏感）按 UTF-8 文本解析，其余按 XDB2；格式判断使用调用方路径，不使用符号链接目标的后缀，不探测内容或自动重试格式。文本格式见 [C API](../API.md#文本词典)。

`$flags = 0` 默认请求只读 mmap；`Scws2::DICT_MEMORY` 强制内存加载。PHP 不公开 DICT_TEXT 或 DICT_MMAP。不支持 mmap、禁用 mmap 或映射失败时，核心自动回退内存。mmap 词典不得原地改写或截断，更新应生成新文件再原子替换路径。

文本词典的编译缓存、跨进程复用、失效、锁和内存回退均由 libscws2 管理，DICT_MEMORY 不禁用磁盘编译缓存。PHP 只对调用者传入的源词典路径检查 open_basedir；核心内部缓存访问不经过该检查，仍受操作系统权限和核心自身检查约束。仅支持本地文件系统路径，不支持 stream wrapper。

参数值或 UTF-8 错误抛 `InvalidArgumentException`，加载、格式或资源错误抛 `RuntimeException`，code 对应 `Scws2::EINVAL`、`EUTF8`、`EIO`、`EFORMAT`、`ENOMEM`、`ELIMIT`、`ENOTSUP`。参数类型和数量由 PHP 参数解析器报告；不返回 false，不提供 last-error 查询。重复构造、未初始化对象操作及属性写入拒绝；禁止 clone、serialize 和 unserialize（旧 PHP 反序列化可能以 warning/false 拒绝）。

## 验证范围

测试为离线 PHPT，使用自带微型 XDB2 构建器及文本词表，不依赖上游数据、根目录构建或正式词典。覆盖接口、原文跨度、非法 UTF-8、重叠补词、失败原子性、对象/结果生命周期、路径权限、缓存及数组转换内存耗尽。

2026-11-25，在 macOS arm64 的实际构建与 PHPT 结果：

| PHP | 构建 | PHPT |
| --- | --- | --- |
| 7.0.33 | NTS | 9 通过，1 跳过（PHP 8 命名参数） |
| 7.0.33 | ZTS | 9 通过，1 跳过（PHP 8 命名参数） |
| 7.4.33 | NTS | 9 通过，1 跳过（PHP 8 命名参数） |
| 8.4.12 | NTS | 10 全部通过 |

另已验证：无根目录构建产物的源码副本直接构建、核心/规则的增量更新、模块仅导出 get_module 且不依赖系统 libscws2；根目录 make check 的 66 项测试通过。PHP 7.0 环境从官方 7.0.33 源码构建，使用兼容当前 macOS SDK 的探测参数与系统 libresolv，不修改仓库核心。

Linux、其他 PHP 小版本、其他版本的 ZTS 及真实 FPM/mod_php 服务进程尚未完成验收；Windows 构建不在首版范围。接口测试通过不代表真实检索质量验收通过。

共享库 ABI 由 Libtool 的版本信息管理，与项目版本分开。扩展应链接构建时匹配的核心；升级不兼容 ABI 时需重新构建扩展。自定义前缀必须配置运行时库搜索路径（Linux 可使用系统 ldconfig 或 LD_LIBRARY_PATH，macOS 使用对应 dylib 安装路径）。可用 `ldd modules/scws2.so`（Linux）或 `otool -L modules/scws2.so`（macOS）检查动态依赖。模块版本取自核心安装的 scws2_version.h，与 VERSION 一致。
