--TEST--
Local paths, virtual cwd, symlink suffixes and source-only open_basedir checks
--SKIPIF--
<?php if (DIRECTORY_SEPARATOR === '\\') die('skip POSIX symlinks'); ?>
--FILE--
<?php
require __DIR__ . '/common.inc';
$root = __DIR__ . '/tmp-paths';
removeTree($root);
mkdir($root, 0700);
mkdir($root . '/allowed', 0700);
mkdir($root . '/cache', 0700);
putenv('TMPDIR=' . $root . '/cache');
file_put_contents($root . '/allowed/words.txt', "中文 100000000 n\n");
symlink($root . '/allowed/words.txt', $root . '/allowed/alias.TXT');
symlink($root . '/allowed/words.txt', $root . '/allowed/alias.xdb');
file_put_contents($root . '/outside.txt', "外部词\n");
symlink($root . '/outside.txt', $root . '/allowed/escape.txt');
$s = new Scws2();
foreach (['', "words\0.txt", 'php://memory', 'file:///tmp/words.txt', 'http://example.com/dict.xdb'] as $path) {
    expectException(InvalidArgumentException::class, function () use ($s, $path) { $s->setDict($path); }, Scws2::EINVAL);
}
$original = getcwd();
chdir($root . '/allowed');
$s->setDict('alias.TXT');
check($s->segment('中文')[0]['pos'] === 'n', 'relative virtual cwd and symlink');
expectException(RuntimeException::class, function () use ($s) { $s->setDict('alias.xdb'); }, Scws2::EFORMAT);
chdir($original);
// Remove cache before restricting PHP: the core will recreate it outside basedir.
foreach (glob($root . '/cache/scws2-text-*/*') as $path) { unlink($path); }
ini_set('open_basedir', $root . '/allowed');
foreach ([$root . '/outside.txt', $root . '/allowed/escape.txt'] as $path) {
    expectException(RuntimeException::class, function () use ($s, $path) { @$s->setDict($path); }, Scws2::EIO);
}
$s->setDict($root . '/allowed/words.txt');
check($s->segment('中文')[0]['pos'] === 'n', 'internal cache is managed by core');
echo "OK\n";
?>
--CLEAN--
<?php
require __DIR__ . '/common.inc';
check(count(glob(__DIR__ . '/tmp-paths/cache/scws2-text-*/*.xdb')) > 0, 'core cache outside basedir');
removeTree(__DIR__ . '/tmp-paths');
?>
--EXPECT--
OK
