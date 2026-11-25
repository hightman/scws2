<?php
// The independent fixture builder needs PCRE splitting, never executable JIT memory.
ini_set('pcre.jit', '0');
function check($condition, $message)
{
    if (!$condition) {
        throw new RuntimeException($message);
    }
}

function expectException($class, $call, $code = null)
{
    try {
        $call();
    } catch (Throwable $error) {
        check($error instanceof $class, get_class($error) . ': ' . $error->getMessage());
        if ($code !== null) {
            check($error->getCode() === $code, 'exception code');
        }
        return;
    }
    throw new RuntimeException('expected ' . $class);
}

function removeTree($path)
{
    if (is_dir($path) && !is_link($path)) {
        foreach (scandir($path) as $name) {
            if ($name !== '.' && $name !== '..') {
                removeTree($path . '/' . $name);
            }
        }
        rmdir($path);
    } elseif (file_exists($path) || is_link($path)) {
        unlink($path);
    }
}

function fixtureRoot()
{
    $root = sys_get_temp_dir() . '/scws2-php-' . uniqid('', true);
    mkdir($root, 0700);
    putenv('TMPDIR=' . $root);
    register_shutdown_function(function () use ($root) { removeTree($root); });
    return $root;
}

// Independent, little-endian XDB2 builder for tiny offline test dictionaries.
// One bucket, balanced tree, every UTF-8 prefix represented explicitly.
function fixtureXdb($path, $entries)
{
    $words = [];
    foreach ($entries as $entry) {
        list($word, $cost, $pos) = $entry;
        $words[$word] = [$cost, 3, $pos];
    }
    foreach (array_keys($words) as $word) {
        $prefix = '';
        $chars = preg_split('//u', $word, -1, PREG_SPLIT_NO_EMPTY);
        array_pop($chars);
        foreach ($chars as $char) {
            $prefix .= $char;
            if (!isset($words[$prefix])) {
                $words[$prefix] = [0, 0, ''];
            }
            $words[$prefix][1] |= 4;
        }
    }
    uksort($words, 'strcmp');
    $keys = array_keys($words);
    $offsets = [];
    $size = 72; // 64-byte header + one 8-byte bucket.
    foreach ($words as $word => $value) {
        $offsets[$word] = $size;
        $size += 27 + strlen($word);
    }
    $links = [];
    $tree = function ($start, $end) use (&$tree, &$links, $keys, $offsets) {
        if ($start >= $end) {
            return 0;
        }
        $middle = intdiv($start + $end, 2);
        $word = $keys[$middle];
        $links[$word] = [$tree($start, $middle), $tree($middle + 1, $end)];
        return $offsets[$word];
    };
    $root = $tree(0, count($keys));
    $data = pack('a4V8', 'XDB2', 4, 64, 0, 0xf422f, 1, count($words), $size + 8, 10)
        . str_repeat("\0", 28) . pack('V2', $root, 0);
    foreach ($words as $word => $value) {
        list($left, $right) = $links[$word];
        $data .= pack('V4C', $left, 0, $right, 0, strlen($word)) . $word
            . pack('V2a2', $value[0], $value[1], $value[2]);
    }
    file_put_contents($path, $data . "XDB2END\n");
}
