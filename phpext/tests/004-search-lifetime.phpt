--TEST--
Search overlaps, search-only fallback bigrams, option replacement and result independence
--FILE--
<?php
require __DIR__ . '/common.inc';
$root = fixtureRoot();
fixtureXdb($root . '/base.xdb', [['中文', 100, 'n'], ['测试', 100, 'vn'], ['中文测试', 1000, 'nz']]);
$s = (new Scws2())->setDict($root . '/base.xdb');
$tokens = $s->segment('中文测试', Scws2::SEARCH);
check(array_column($tokens, 'word') === ['中文测试', '中文', '测试'], 'search ordering');
check(array_column($tokens, 'kind') === [Scws2::SECONDARY, Scws2::PRIMARY, Scws2::PRIMARY], 'search kinds');
$snapshot = $tokens;
foreach ($tokens as $token) {
    $s->segment($token['word']);
    expectException(InvalidArgumentException::class, function () use ($s) { $s->segment("\xff"); });
}
check($tokens === $snapshot, 'nested results independent');
unset($s);
check($tokens === $snapshot && count($tokens) === 3, 'results survive context');
$s = new Scws2();
check(count($s->segment('甲乙丙', Scws2::SEARCH)) === 3, 'bigrams off by default');
$s->setOptions(Scws2::FALLBACK_BIGRAM);
check(count($s->segment('甲乙丙', Scws2::STANDARD)) === 3, 'standard ignores bigrams');
$search = $s->segment('甲乙丙', Scws2::SEARCH);
check(array_column($search, 'word') === ['甲乙', '甲', '乙丙', '乙', '丙'], 'fallback order');
check($search[0]['kind'] === Scws2::FALLBACK && $search[0]['pos'] === '', 'fallback metadata');
check(count($s->segment('甲 乙', Scws2::SEARCH)) === 2, 'no crossing whitespace');
$s->setOptions(Scws2::KEEP_SYMBOLS);
check(count($s->segment('甲乙丙！', Scws2::SEARCH)) === 4, 'options replace all bits');
expectException(InvalidArgumentException::class, function () use ($s) { $s->setOptions(PHP_INT_MAX); }, Scws2::EINVAL);
check(count($s->segment('！')) === 1, 'invalid flags keep options');
$other = new Scws2();
check($other->segment('！') === [], 'contexts isolated');
echo "OK\n";
?>
--EXPECT--
OK
