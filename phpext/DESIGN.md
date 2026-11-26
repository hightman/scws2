> 构建契约已更新：本扩展通过 `--with-scws2=DIR` 动态链接已安装的 libscws2，不再内嵌 engine 源码或生成规则表。具体构建步骤以 README.md 为准，以下对象及数据接口契约继续适用。

# scws2 PHP 扩展设计

状态：首版已实现。本文维护 PHP 接口及内部边界；构建方式和实际验证范围见 [README.md](README.md)。

## 1. 目标与取舍

- 扩展名 `scws2`，最低 PHP 7.0；同时面向 PHP 7.x 和 PHP 8.x，按各目标 PHP 版本分别编译。
- 仅提供 OOP 接口：一个 `final class Scws2`，无全局分词函数、资源句柄和旧版兼容别名。
- 对象管理词典与配置，`segment()` 一次返回普通 PHP 数组；调用方无需管理 C 上下文、结果游标或资源释放。
- 首版仅暴露构造、替换词典、追加词典、设置普通选项、执行分词五个方法。高级未知单字代价暂不开放。
- 严格沿用核心的 UTF-8、词典格式、评分及主词/补词语义，不在 PHP 层重新实现分词。

返回数组比新增结果类、Token 类和 Iterator 更符合首版最小接口目标：可直接 `foreach`、`count()`、`array_column()` 或 JSON 编码，同一结果可重复遍历和嵌套遍历。代价是一次性生成全部 token 数组，不能宣称流式或低内存；大文本由应用按业务边界分批处理，跨批分词和偏移合并由应用负责。

## 2. 公开接口

以下为接口签名示意，不是待直接执行的 PHP 实现。方法使用 PHP 风格 camelCase，不机械复制 C 函数前缀；扩展名和类名统一标识 scws2。

```php
final class Scws2
{
    public function __construct();

    public function setDict(string $path, int $flags = 0): self;
    public function addDict(string $path, int $flags = 0): self;
    public function setOptions(int $flags): self;
    public function segment(string $text, int $mode = self::STANDARD): array;
}
```

构造函数无返回类型；设置方法成功返回当前对象，可链式调用。方法的参数名称作为后续 PHP 8 命名参数契约维护。无公开属性，不提供静态工厂、公开析构方法、`close()`、`free()`、`reset()`、`getError()`、`getResult()` 或同义便利方法。

### 2.1 类常量

常量全部位于 `Scws2` 内，不注册全局 `SCWS2_*` 常量。

| 分组 | 常量与值 | 用途 |
| --- | --- | --- |
| 模式 | `STANDARD = 0`、`SEARCH = 1` | `segment()` 的模式参数，不是位标志 |
| 词典标志 | `DICT_MEMORY = 1` | 强制内存加载；`0` 默认请求 mmap，格式由路径后缀推断 |
| 普通选项 | `KEEP_SYMBOLS = 1`、`FALLBACK_BIGRAM = 2` | 可按位或；`0` 关闭全部选项 |
| token 种类 | `PRIMARY = 0`、`SECONDARY = 1`、`FALLBACK = 2` | 解释 `kind` |
| token 来源 | `DICTIONARY = 0`、`BUILTIN = 1`、`UNKNOWN = 2` | 解释 `origin` |
| 错误码 | `EINVAL = 1`、`ENOMEM = 2`、`EIO = 3`、`EFORMAT = 4`、`EUTF8 = 5`、`ELIMIT = 6`、`ENOTSUP = 7` | 对应核心状态，用于异常 `getCode()` |

不同参数的常量分组不可混用。PHP 仅看到整数，数值重合时无法识别调用方使用了哪个常量名；扩展校验取值和有效位。无需公开 `OK`，成功以正常返回表示。版本通过 `phpversion('scws2')` 查询，构建信息通过 `php --ri scws2` 查询。

### 2.2 构造与配置

`new Scws2()` 创建独立上下文，普通选项为 `0`，不自动寻找词典、不读取环境变量指定的默认词典、不加载当前目录文件。未加载词典时，仍可使用核心内置英文、数字、数量规则和未知字兜底。

`setDict()` 成功后替换全部词典；`addDict()` 按调用顺序叠加，同一完整词以后加载的定义为准。首次使用 `addDict()` 合法。加载失败保留原词典，配置失败保留原选项。

PHP 与核心采用一致的加载策略，格式由 PHP 根据后缀补充：`$flags = 0` 默认请求只读 mmap，`Scws2::DICT_MEMORY` 强制完整读入私有内存。仅公开 `DICT_MEMORY`，不提供 `DICT_MEM` 同义别名，也不公开 `DICT_MMAP`、`DICT_TEXT`。负数、未知位及超出范围的值在转换为 C 参数前拒绝。

每次调用独立决定格式及加载方式，不记忆上次设置：

- 以调用方传入路径的最后一个文件名后缀判断格式，ASCII 大小写不敏感；`.txt` 为 UTF-8 文本，其余（包括 `.xdb`、无后缀、其他后缀）均按 XDB2 解析。
- 不探测文件内容，不自动重试其他格式。文本文件未使用 `.txt` 后缀或 XDB2 文件误用 `.txt` 后缀时，按选定解析器的错误报告加载失败。
- 格式判断使用调用方路径，路径解析和符号链接规范化用于访问权限检查及实际加载，不改用链接目标的后缀重新判断。

| 路径后缀 | PHP flags | 内部 C flags | 首版行为 |
| --- | --- | --- | --- |
| 非 `.txt` | `0`（默认） | `0` | XDB2，请求只读 mmap，不可用时回退内存 |
| 非 `.txt` | `Scws2::DICT_MEMORY` | `SCWS2_DICT_MEMORY` | XDB2，完整读取到私有内存，不尝试 mmap |
| `.txt` | `0`（默认） | `SCWS2_DICT_TEXT` | UTF-8 文本，编译/复用缓存 XDB2，默认请求 mmap；缓存或映射不可用时回退内存 |
| `.txt` | `Scws2::DICT_MEMORY` | `SCWS2_DICT_TEXT \| SCWS2_DICT_MEMORY` | UTF-8 文本，编译/复用缓存 XDB2，强制内存加载 |

默认请求 mmap，使 FPM 多进程及 mod_php 多进程/线程部署中的 XDB2 词典可共享文件页，避免各对象通过 fread 持有整库私有副本；上下文和分词工作内存仍独立。`DICT_MEMORY` 适用于长期运行 CLI 等希望主动持有私有副本的场景，不意味着 CLI 必须使用内存模式，也不承诺其速度优于 mmap。

加载后不自动检测文件更新；需要更新时重新调用加载方法。mmap 使用期间不得原地改写或截断词典，部署更新应生成新文件再原子替换路径。

`setOptions()` 覆盖全部普通选项，只影响后续分词。`FALLBACK_BIGRAM` 仅在 `SEARCH` 下生效，在 `STANDARD` 下允许设置但不生成二元补词。未知位、负数或无法表示的标志值拒绝，不截断为 C 的 unsigned。

## 3. 分词结果契约

`segment()` 同步完成分词并返回从 `0` 开始连续编号的 token 数组。每个 token 是具有以下固定字段的关联数组，不按模式省略字段：

| PHP 字段 | 类型 | 含义 |
| --- | --- | --- |
| `word` | `string` | 从原始输入按字节跨度复制的词面，保留原始大小写及全角形式 |
| `offset` | `int` | 原文 UTF-8 字节起点，从 `0` 开始 |
| `length` | `int` | 字节长度，等于 `strlen($token['word'])` |
| `char_offset` | `int` | 原文 Unicode 标量起点，从 `0` 开始 |
| `char_length` | `int` | Unicode 标量数量，不是字形簇数量 |
| `pos` | `string` | 最多两个 ASCII 字符；无标签时为 `''`，文本词典缺省为 `un` |
| `kind` | `int` | `PRIMARY`、`SECONDARY` 或 `FALLBACK` |
| `origin` | `int` | `DICTIONARY`、`BUILTIN` 或 `UNKNOWN` |

字符跨度不是搜索引擎 token position。保留 C 字段名，便于对照公共契约；字段顺序按上表生成，应用应按键名访问。

- 默认 `STANDARD` 返回主切分；`SEARCH` 保留主词并增加重叠补词。
- 顺序与 C 核心一致：按字节起点递增，同起点长词优先，同一跨度只输出一次。不在 PHP 层排序或去重。
- 空字符串、全部被忽略的空白或符号返回 `[]`，不返回 `false` 或 `null`。
- 字符串按显式字节长度传入核心；输入内的 NUL 是合法分隔符，不使用 `strlen()` 计算传入长度。
- 非法 UTF-8 抛异常，不修复、不转码、不返回部分结果。
- 返回值完全独立。再次分词、改变配置、替换词典、发生错误或 `unset($scws)` 都不影响已有数组。
- 搜索补词存在歧义边界，应用可按 `kind` 区分主词与补词，不能假定全部输出都应作为必选查询条件。

### 使用示例

```php
$scws = (new Scws2())
    ->setDict('/srv/app/dict/dict.xdb')
    ->addDict('/srv/app/dict/user.txt')
    ->setOptions(Scws2::KEEP_SYMBOLS);

$tokens = $scws->segment('中文分词测试', Scws2::SEARCH);
foreach ($tokens as $token) {
    echo $token['word'], "\t", $token['offset'], "\n";
}

// 只需要词面时，使用 PHP 自带数组操作。
$words = array_column($tokens, 'word');

// 长期运行 CLI 可按需强制内存加载；此对象独立拥有词典。
$cli = (new Scws2())->setDict('/srv/app/dict/dict.xdb', Scws2::DICT_MEMORY);

// 内层分词不改变外层结果。
foreach ($tokens as $token) {
    $inner = $scws->segment($token['word']);
}
unset($scws);
// $tokens、$words、$inner 仍是普通 PHP 值。
```

## 4. 错误与对象行为

复用 PHP 内置异常，不另建扩展异常体系，不暴露上下文的 last-error 状态。

| 情况 | PHP 行为 |
| --- | --- |
| 参数类型不符合声明 | `TypeError`；按目标 PHP 的标量参数规则处理弱类型转换与严格类型调用 |
| 参数数量错误 | 使用目标 PHP 的参数解析异常；PHP 7.0 不依赖后续版本才提供的 `ArgumentCountError` |
| 无效模式、标志、空路径、路径含 NUL、不支持的 wrapper | `InvalidArgumentException`，code 为 `EINVAL` |
| 核心 `EINVAL`、`EUTF8` | `InvalidArgumentException`，code 为对应核心状态 |
| 核心 `EIO`、`EFORMAT`、`ENOMEM`、`ELIMIT`、`ENOTSUP` | `RuntimeException`，code 为对应核心状态 |
| `open_basedir` 拒绝 | `RuntimeException`，code 为 `EIO` |
| 未初始化对象调用、重复显式调用构造函数 | `LogicException`，不替换或泄漏已有 ctx |
| clone、序列化、反序列化 | 明确拒绝；不复制内部指针或恢复无效句柄 |

异常消息应包含方法名及核心错误说明，不依赖消息字符串做程序判断。核心调用失败后立即读取状态和错误信息，完成清理再抛异常。加载失败不额外发扩展自定义 warning；PHP 自身的路径检查若产生 warning，保留其原生诊断，不通过全局错误处理器屏蔽。

PHP/Zend 自身的内存耗尽可能直接终止请求，不能承诺全部 OOM 都可捕获；表中 `ENOMEM` 仅指核心返回的可恢复失败。

类设为 final，无用户可写属性；跨版本使用对象 handler 拒绝动态属性。禁用克隆与序列化需使用目标版本可用的 handler/标志及必要的私有防护方法，不能只依赖 PHP 7.4 才有的 `__serialize()`。这些防护不是供应用调用的业务接口；也应防止通过反射绕过构造获得可用的空句柄对象。

## 5. PHP 边界与内部封装

### 路径与文本词典缓存

仅接受本地文件系统路径；拒绝 NUL 和 `php://`、`http://`、`phar://` 等 stream wrapper，不将路径交给 PHP stream 解包。相对路径按 PHP 当前工作目录解析为绝对路径后交给核心，避免 PHP virtual CWD 与 libc 工作目录不一致。检查原路径及规范化目标的 `open_basedir` 权限，符号链接不能绕过检查；路径失败不调用核心加载，也不改变原词典。

**保留 libscws2 现有文本磁盘缓存，不为 PHP 添加核心缓存目录接口，不在 PHP 层禁用缓存。** 缓存目录选择、失效判断、并发锁、原子发布及不可用时的内存回退均交给核心；PHP 不新增缓存目录配置、控制方法，也不修改进程的 `TMPDIR`。

PHP 的 `open_basedir` 检查仅覆盖用户传入的源词典路径。核心通过 libc 访问其内部缓存目录，不经过 PHP 的路径检查；这是本扩展明确采用的边界，不承诺内部缓存位于 `open_basedir` 范围内。核心仍受操作系统权限及自身的目录/文件检查约束。

macOS/Linux 上，核心按现有规则使用 `$TMPDIR/scws2-text-<uid>/`，未设置 TMPDIR 时使用 `/tmp/scws2-text-<uid>/`。`.txt` 首次加载编译并缓存为 XDB2，后续可跨上下文、跨请求及跨进程复用；默认请求 mmap，`DICT_MEMORY` 只改变加载方式，不禁用编译缓存。缓存不可用时直接编译并使用内存，mmap 不可用时也回退内存。具体权限、锁、缓存版本和失效条件沿用 [C API 的缓存契约](../API.md#临时缓存与加载模式)。

路径检查不是文件描述符级原子授权，不承诺消除恶意并发替换路径的竞态；应用不得让不可信主体同时改写词典目录。`open_basedir` 本身也不是完整安全隔离机制，见 [PHP 官方说明](https://www.php.net/manual/en/ini.core.php#ini.open-basedir)。

### 所有权与内存

- 每个 PHP 对象内部独占一个 `scws2_t`，不使用持久上下文或全局词典池；文本编译磁盘缓存由核心管理，可跨请求复用。ZTS 下不同对象也不共享可变分词状态。
- 在对象分配阶段将 ctx 初始化为 NULL，构造时创建；`free_obj` 统一释放非空 ctx，再完成 Zend 对象清理。构造失败、异常路径和请求结束均须安全。
- 每次分词在内部持有一个临时 `scws2_result_t`，转换为 PHP 数组后立即释放；调用期间读取原输入，复制词面后不再保留输入或 ctx 引用。
- 转换期间发生可恢复失败时释放 C 结果和部分 PHP 数组，不返回半成品。为 Zend bailout 路径设置明确的临时结果清理保护，避免长期 PHP worker 泄漏 C 内存。
- 所有 C `size_t` 转 PHP `zend_long` 前检查范围，不能溢出为负数；无法表示的结果抛 `RuntimeException(ELIMIT)`。预分配数组时同时检查 Zend 容量上限。
- 核心使用 malloc/mmap 的内存不完整计入 PHP `memory_limit`，不能将其视为总内存硬限制；数组转换还会产生显著额外内存。

不引入用户回调和异步接口；同一个核心上下文不并发操作。多个 PHP 对象与已有结果相互独立。不修改核心评分、文本词典解析或结果生命周期契约。

## 6. PHP 7.0 与构建方案

API、示例及测试辅助脚本以 PHP 7.0 可用语法为基线。无需 nullable、`void`、`iterable`、typed properties、union types、attributes 或 enum；`void` 和 nullable 类型从 PHP 7.1 才加入，见 [PHP 7.1 迁移说明](https://www.php.net/manual/en/migration71.new-features.php)。

扩展使用手写 PHP 7.0 兼容 arginfo；PHP 8 的参数解析、对象分配及序列化防护差异集中在小型兼容头中，业务方法共用实现。以实际 PHP 7.0 头文件为基线，不能仅在 PHP 7.4 编译后宣称兼容；[PHP 7.0 Zend API 源码](https://github.com/php/php-src/blob/PHP-7.0/Zend/zend_API.h)是实现时的核对来源。

当前构建方案：

1. 先构建并安装核心共享库，再使用 phpext/ 构建扩展；扩展不包含核心实现，不面向 PECL 独立源码包发布。
2. 提供 config.m4，使用 phpize / ./configure --with-scws2=DIR / make / make test。指定安装前缀缺少头文件或共享库时 configure 报错。
3. config.m4 接收 --with-scws2=DIR，仅编译 PHP 封装并动态链接指定前缀的 libscws2；不编译 engine 源码或生成规则。核心能力由已安装共享库决定。
4. 扩展 configure/make 不调用 Python，不生成规则，也不依赖核心源码目录；规则和平台能力由已安装的核心库提供。
5. 使用匹配的 phpize、php-config 及 PHP 可执行文件构建和测试。模块版本来自 SCWS2_VERSION；MINFO 显示版本、UTF-8、mmap 回退语义及共享核心链接方式。

```sh
cd phpext
phpize
./configure --with-scws2=/usr/local/scws2 --with-php-config=/path/to/php-config
make -j2
make test
make install
```

在相应 PHP 配置中启用 `extension=scws2.so`。扩展不安装词典、不新增 INI 指令。Windows `config.w32` 与二进制分发不纳入首版平台验收；最低 PHP 版本不等于所有操作系统都已受支持。

文件组织为 `scws2.c`（模块、类、方法和生命周期）、`php_scws2.h`、`compat.h`（必要版本适配）、`config.m4`、`tests/*.phpt`。无需拆出配置框架、结果类或通用封装层。

## 7. 实施顺序与验收

实现及验证按以下依赖组织：

1. 私有核心构建及类注册：PHP 7.0 可编译、模块可加载、无全局分词函数。
2. 对象与词典：生命周期、路径边界、两种文件格式与两种加载策略的四种组合、失败保留原状态。
3. 分词与异常：数组字段、原文跨度、嵌套调用、清理及错误映射。
4. PHPT、跨版本构建和使用文档；更新根 README 的状态时以实测结果为准。

验收至少覆盖：

| 范围 | 必须验证的可观察结果 |
| --- | --- |
| API | 五个公开业务方法、类常量、final、返回 `$this`、默认值、无公开属性及函数式别名 |
| 基本分词 | 无词典行为、离线小型 XDB2 / TEXT、standard/search、重叠补词、kind/origin、所有字段 |
| 输入与偏移 | 空字符串、空白/符号、中文、英数、全角、非 BMP 字符、组合字符、内嵌 NUL、非法 UTF-8 |
| 词典与配置 | 替换、叠加及同词覆盖、两种格式与两种加载策略的四种组合、默认 mmap、强制内存、mmap 回退、后缀大小写/无后缀/格式不匹配、坏文件、无效位、加载/配置失败后原状态可用 |
| PHP 路径 | 相对路径与虚拟工作目录、符号链接及链接名后缀选择、open_basedir、NUL、wrapper 拒绝；源词典权限检查与核心内部缓存边界 |
| 文本缓存 | 命中复用、源文件变化失效、损坏重建、跨进程并发加载、缓存不可用回退、两种加载策略；源词典获准而内部缓存位于 open_basedir 外时保持核心原有行为 |
| 生命周期 | 重复构造防护、clone/序列化拒绝、反射绕过防护、动态属性拒绝、多个对象错误隔离 |
| 结果独立 | 重复/嵌套遍历，同对象嵌套分词，后续失败/词典替换/对象销毁后已有数组仍有效 |
| 资源清理 | 反复创建销毁、分词失败、转换中失败、受控 bailout 的 native 内存无持续增长；必要时 ASan/UBSan |
| 兼容性 | PHP 7.0、7.4 与实施时的 PHP 8.x 各目标分支实际构建和 PHPT；Linux/macOS，覆盖 NTS/ZTS 构建 |

PHPT 使用自带小词表或离线生成的微型 XDB2，不下载上游词典、不依赖本机正式词典。涉及核心构建集成时运行根目录 `make check`；PHP 测试单独报告。不能以 PHP 7.4 的本机结果代替 PHP 7.0 验收，也不能将接口正确性等同于词典或检索质量验收。

首版不包含关键词提取、IDF、字符集切换、词性倍率、姓名地名规则、运行时单词增删、外露词典句柄、高级代价调参、懒加载结果对象或持久词典池。后续需求以真实调用场景决定，不预留无实际用途的公共入口。
