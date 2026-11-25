--TEST--
Scalar argument parsing respects strict_types
--FILE--
<?php
declare(strict_types=1);
require __DIR__ . '/common.inc';
$s = new Scws2();
expectException(TypeError::class, function () use ($s) { $s->segment(123); });
expectException(TypeError::class, function () use ($s) { $s->segment('abc', '1'); });
expectException(TypeError::class, function () use ($s) { $s->setOptions('1'); });
expectException(TypeError::class, function () use ($s) { $s->setDict(123); });
echo "OK\n";
?>
--EXPECT--
OK
