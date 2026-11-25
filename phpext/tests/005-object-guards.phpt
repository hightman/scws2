--TEST--
Object ownership guards reject cloning, serialization, properties and repeated construction
--FILE--
<?php
require __DIR__ . '/common.inc';
$s = new Scws2();
expectException(LogicException::class, function () use ($s) { $s->__construct(); });
expectException(Throwable::class, function () use ($s) { $copy = clone $s; });
expectException(Throwable::class, function () use ($s) { serialize($s); });
// Older engines reject O-format with a warning/false; newer ones throw.
set_error_handler(function ($severity, $message) { throw new ErrorException($message, 0, $severity); });
expectException(Throwable::class, function () { unserialize('O:5:"Scws2":0:{}'); });
expectException(Throwable::class, function () { unserialize('C:5:"Scws2":0:{}'); });
restore_error_handler();
expectException(LogicException::class, function () use ($s) { $s->ctx = 1; });
expectException(LogicException::class, function () use ($s) { $s->ctx[] = 1; });
expectException(LogicException::class, function () use ($s) { $value =& $s->ctx; });
check(get_object_vars($s) === [], 'no properties leaked');
try {
    $uninitialized = (new ReflectionClass('Scws2'))->newInstanceWithoutConstructor();
    expectException(LogicException::class, function () use ($uninitialized) { $uninitialized->segment('abc'); });
    $uninitialized->__construct();
    check(count($uninitialized->segment('abc')) === 1, 'safe delayed initialization');
} catch (ReflectionException $error) {
    // Some PHP versions deny bypassing construction of internal final classes.
}
check(count($s->segment('abc')) === 1, 'guards preserve valid object');
for ($i = 0; $i < 1000; $i++) {
    $temporary = new Scws2();
    $tokens = $temporary->segment('中文 C++ ２０２６年', Scws2::SEARCH);
    unset($temporary);
    check(count($tokens) > 0, 'repeated lifetime');
}
echo "OK\n";
?>
--EXPECT--
OK
