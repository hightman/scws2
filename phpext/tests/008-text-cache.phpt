--TEST--
Text cache reuse, source invalidation, damaged cache recovery and memory fallback
--SKIPIF--
<?php if (DIRECTORY_SEPARATOR === '\\') die('skip POSIX cache'); ?>
--FILE--
<?php
require __DIR__ . '/common.inc';
$root = fixtureRoot();
$path = $root . '/user.txt';
file_put_contents($path, "中文 100000000 n\n");
$a = (new Scws2())->setDict($path);
$cache = glob($root . '/scws2-text-*/*.xdb');
check(count($cache) === 1, 'cache created');
$cache = $cache[0];
$inode = fileinode($cache);
$b = (new Scws2())->setDict($path, Scws2::DICT_MEMORY);
check(fileinode($cache) === $inode, 'cache reused by memory mode');
check($a->segment('中文') === $b->segment('中文'), 'both modes equal');
// Replace cache by rename, never truncate a file that a still maps.
file_put_contents($root . '/broken', 'broken');
rename($root . '/broken', $cache);
$c = (new Scws2())->setDict($path);
check($c->segment('中文')[0]['pos'] === 'n', 'damaged cache rebuilt');
file_put_contents($path, "中文 100000000 v\n新增词\n");
$d = (new Scws2())->setDict($path);
check($d->segment('中文')[0]['pos'] === 'v', 'changed source recompiled');
check($a->segment('中文')[0]['pos'] === 'n', 'old mapping intact');
check($b->segment('中文')[0]['pos'] === 'n', 'old memory copy intact');
check(count(glob($root . '/scws2-text-*/*.xdb')) === 2, 'new fingerprint');
putenv('TMPDIR=' . $root . '/missing/directory');
$e = (new Scws2())->setDict($path);
check($e->segment('中文')[0]['pos'] === 'v', 'cache unavailable falls back');
echo "OK\n";
?>
--EXPECT--
OK
