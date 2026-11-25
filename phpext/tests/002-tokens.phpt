--TEST--
Token fields, original byte/scalar spans, empty input, NUL and UTF-8 errors
--FILE--
<?php
require __DIR__ . '/common.inc';
$s = new Scws2();
check($s->segment('') === [] && $s->segment(" \t！") === [], 'empty results');
check(array_column($s->segment('中文 C++ 2026年'), 'word') === ['中', '文', 'C++', '2026年'], 'builtins');
$s->setOptions(Scws2::KEEP_SYMBOLS);
$text = "😀𠀀 e\u{0301}\0２０２６年！";
$raw = $s->segment($text);
$chars = preg_split('//u', $text, -1, PREG_SPLIT_NO_EMPTY);
foreach ($raw as $i => $token) {
    check(array_keys($token) === ['word', 'offset', 'length', 'char_offset', 'char_length', 'pos', 'kind', 'origin'], 'all fields');
    check(substr($text, $token['offset'], $token['length']) === $token['word'], 'byte span');
    check(implode('', array_slice($chars, $token['char_offset'], $token['char_length'])) === $token['word'], 'scalar span');
    foreach (['offset', 'length', 'char_offset', 'char_length', 'kind', 'origin'] as $field) {
        check(is_int($token[$field]), 'integer field');
    }
}
check(array_column($s->segment("中\0文"), 'offset') === [0, 4], 'NUL kept in original offset');
foreach (["\xff", "\xed\xa0\x80", "\xf0\x80\x80\x80", "\xe4\xb8"] as $bad) {
    expectException(InvalidArgumentException::class, function () use ($s, $bad) { $s->segment($bad); }, Scws2::EUTF8);
}
expectException(InvalidArgumentException::class, function () use ($s) { $s->segment('中文', 9); }, Scws2::EINVAL);
check($s->segment('') === [], 'usable after failure');
echo "OK\n";
?>
--EXPECT--
OK
