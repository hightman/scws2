--TEST--
Zend memory exhaustion during array conversion exits normally through request shutdown
--INI--
memory_limit=8M
--FILE--
<?php
register_shutdown_function(function () { echo "request shutdown\n"; });
$s = new Scws2();
$s->segment(str_repeat('中', 20000));
echo "unexpected success\n";
?>
--EXPECTF--
Fatal error: Allowed memory size of 8388608 bytes exhausted (tried to allocate %d bytes) in %s on line %d
request shutdown
