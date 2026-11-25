--TEST--
PHP 8 named arguments and reflected defaults follow the public contract
--SKIPIF--
<?php if (PHP_VERSION_ID < 80000) die('skip named arguments require PHP 8'); ?>
--FILE--
<?php
require __DIR__ . '/common.inc';
$root = fixtureRoot();
file_put_contents($root . '/user.txt', "中文 100000000 n\n");
$s = (new Scws2())->setDict(flags: Scws2::DICT_MEMORY, path: $root . '/user.txt');
$s->addDict(path: $root . '/user.txt');
$s->setOptions(flags: Scws2::KEEP_SYMBOLS);
check(array_column($s->segment(mode: Scws2::SEARCH, text: '中文！'), 'word') === ['中文', '！'], 'named arguments');
$dict = new ReflectionMethod('Scws2', 'setDict');
check((string) $dict->getReturnType() === 'self', 'self return type');
check($dict->getParameters()[1]->getDefaultValue() === 0, 'dict default');
$segment = new ReflectionMethod('Scws2', 'segment');
check($segment->getParameters()[1]->getDefaultValue() === Scws2::STANDARD, 'mode default');
echo "OK\n";
?>
--EXPECT--
OK
