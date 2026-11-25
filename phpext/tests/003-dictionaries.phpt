--TEST--
XDB2/TEXT suffix selection, both storage flags, overlays and failure atomicity
--FILE--
<?php
require __DIR__ . '/common.inc';
$root = fixtureRoot();
fixtureXdb($root . '/base.xdb', [['中文', 100, 'n'], ['测试', 100, 'vn'], ['中文测试', 1000, 'nz']]);
file_put_contents($root . '/user.TXT', "中文 100000000 v\n");
foreach ([0, Scws2::DICT_MEMORY] as $flags) {
    $s = new Scws2();
    check($s->setDict($root . '/base.xdb', $flags) === $s, 'fluent set');
    $primary = $s->segment('中文测试');
    check(array_column($primary, 'word') === ['中文', '测试'], 'lowest cost main path');
    check($primary[0]['origin'] === Scws2::DICTIONARY && $primary[0]['pos'] === 'n', 'dictionary metadata');
    check($s->addDict($root . '/user.TXT', $flags) === $s, 'fluent add and uppercase suffix');
    check($s->segment('中文')[0]['pos'] === 'v', 'overlay override');
    file_put_contents($root . '/bad.txt', "broken -1 n\n");
    foreach (['setDict', 'addDict'] as $method) {
        expectException(RuntimeException::class, function () use ($s, $root, $method) { $s->$method($root . '/bad.txt'); }, Scws2::EFORMAT);
        check($s->segment('中文')[0]['pos'] === 'v', 'failed load keeps dictionaries');
    }
    $s->setDict($root . '/base.xdb', $flags);
    check($s->segment('中文')[0]['pos'] === 'n', 'set replaces overlays');
    $textOnly = (new Scws2())->addDict($root . '/user.TXT', $flags);
    check($textOnly->segment('中文')[0]['pos'] === 'v', 'add as first dictionary');
}
copy($root . '/base.xdb', $root . '/no-extension');
copy($root . '/base.xdb', $root . '/other.bin');
copy($root . '/base.xdb', $root . '/wrong.txt');
copy($root . '/user.TXT', $root . '/wrong.xdb');
foreach (['no-extension', 'other.bin'] as $path) {
    check((new Scws2())->setDict($root . '/' . $path)->segment('中文')[0]['pos'] === 'n', 'non-txt is XDB2');
}
foreach (['wrong.txt', 'wrong.xdb', 'missing.xdb'] as $path) {
    expectException(RuntimeException::class, function () use ($root, $path) { (new Scws2())->setDict($root . '/' . $path); });
}
foreach ([-1, 2, 4, PHP_INT_MAX] as $flags) {
    expectException(InvalidArgumentException::class, function () use ($s, $root, $flags) { $s->setDict($root . '/base.xdb', $flags); }, Scws2::EINVAL);
}
echo "OK\n";
?>
--EXPECT--
OK
