--TEST--
Scws2 exposes only the minimal object API and class constants
--SKIPIF--
<?php if (!extension_loaded('scws2')) die('skip scws2 not loaded'); ?>
--FILE--
<?php
require __DIR__ . '/common.inc';
$class = new ReflectionClass('Scws2');
check($class->isFinal(), 'final class');
$methods = get_class_methods('Scws2');
sort($methods);
check($methods === ['__construct', 'addDict', 'segment', 'setDict', 'setOptions'], 'minimal methods');
check($class->getProperties() === [], 'no properties');
check((new ReflectionExtension('scws2'))->getFunctions() === [], 'no global functions');
check(!defined('SCWS2_DICT_MEMORY'), 'no global constants');
check(!defined('Scws2::DICT_MMAP') && !defined('Scws2::DICT_TEXT'), 'hidden flags');
check(Scws2::DICT_MEMORY === 1 && Scws2::EUTF8 === 5, 'constant values');
check(is_string(phpversion('scws2')) && phpversion('scws2') !== '', 'version');
$s = new Scws2();
check($s->setOptions(0) === $s, 'fluent options');
foreach ([['segment', []], ['setOptions', []], ['setDict', []], ['__construct', [1]]] as $call) {
    expectException(Throwable::class, function () use ($s, $call) { call_user_func_array([$s, $call[0]], $call[1]); });
}
expectException(TypeError::class, function () use ($s) { $s->segment([]); });
expectException(TypeError::class, function () use ($s) { $s->setOptions([]); });
echo "OK\n";
?>
--EXPECT--
OK
