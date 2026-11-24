# XDB2 dictionary format, version 3

All integers are unsigned little-endian values. No native C structures or floats
are serialized. The extension remains `.xdb`; this is a new SCWS 2 format,
not a drop-in SCWS 1 XDB. The header deliberately makes the legacy reader's
file-size check fail. Readers must check magic and schema before reading nodes.

## Header (64 bytes)

| Byte offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `XDB2` |
| 4 | 4 | Schema version, 3 |
| 8 | 4 | Header length, 64 |
| 12 | 4 | Required features, 0 |
| 16 | 4 | Hash base, `0x000f422f` |
| 20 | 4 | Bucket count, 16381 for the current compiler |
| 24 | 4 | Unique node count, including prefix-only entries |
| 28 | 4 | Exact file length, at most `INT32_MAX` in this version |
| 32 | 4 | Fixed value length, 10 (cost, flags, POS) |
| 36 | 28 | Reserved; must be zero |

The header is followed by `bucket_count` root pointers. Each pointer is
`offset:u32, length:u32`; `(0, 0)` means empty. Offsets are absolute.

## Hash buckets and nodes

Start with `h = base`. Traverse key bytes in reverse and apply
`h = ((h * 33) XOR byte) AND 0x7fffffff`. The bucket is `h % bucket_count`.

Each bucket is a balanced binary search tree. Keys compare lexicographically
as unsigned UTF-8 bytes, with the shorter key first when it is a prefix.

Each node is exactly `27 + key_length` bytes:

```text
left_pointer:8 | right_pointer:8 | key_length:1 | key:N | cost:4 | flags:4 | pos:2
```

Keys contain 1..240 bytes of valid scalar UTF-8, without NUL or whitespace.
The source pipeline additionally rejects Unicode control/format/private-use/
unassigned characters. Dictionary terms are not Unicode-normalized.

`cost` is a nonnegative fixed-point natural-log cost scaled by 1024.
Smaller costs prefer more probable terms. There is no IDF field.

`pos` is optional dictionary metadata, not a contextual tagging result. It
does not affect costs, path selection or the flags below. `pos` stores at
most two ASCII characters, right-padded with zero bytes. Two zero bytes mean
unknown (not the literal tag `x`). Nonempty tags must match
`[A-Za-z][A-Za-z0-9_-]?`; embedded zeros are invalid. A two-character label
does not have an on-disk terminator; C readers need three bytes to return a
NUL-terminated string.

The source pipeline maps jieba `nrt` and `nrfg` explicitly to `nr`; all other
supported tags are preserved. An unmapped source label longer than two
characters fails instead of being truncated. The manifest records this
mapping as `jieba-compact-v1`; it does not claim full compatibility with
SCWS 1 rule semantics. Original source labels remain in the locked upstream
snapshot. A term stores one default tag; no POS disambiguation or
`rules.ini` evaluation is implemented.

| Flag | Value | Meaning |
| --- | --- | --- |
| MAIN | 1 | Eligible for main-path segmentation; also requires SEARCH |
| SEARCH | 2 | Eligible as a whole term in search output |
| PREFIX | 4 | A proper prefix of at least one whole term |

Flags can be combined. Prefix-only nodes and search-only terms have zero
cost. A prefix is generated at every Unicode scalar boundary, including the
first character. No prefix ends inside a multibyte encoding.
Prefix-only nodes must have an empty POS. If a prefix is also a whole term,
it retains that term's own tag; it never inherits the longer term's tag.

Nodes are serialized in bucket/key order; tree links choose the midpoint of
each sorted subrange. There are no gaps, timestamps or trailing bytes.
Input order does not affect binary output. The compiler rejects duplicate
whole terms; repeated generated prefixes are merged.

## Validation and evolution

The independent checker verifies exact file length, reserved fields, pointer
bounds, cycles/shared nodes, ordering, hash membership, flags, POS encoding and padding, node count,
nonoverlapping complete byte coverage, and the prefix closure. A SHA-256 of
the entire file is recorded in the external build manifest, not inside XDB2.

Future C loaders must perform structural validation before using offsets,
and impose reasonable resource limits when loading untrusted files. Unknown
schemas or required features must fail explicitly. This prototype schema
may change before the first stable 2.0 release; bump the schema when it does.
Schema 1 used an 8-byte POS-free value; schema 2 stored a variable-length
POS label of up to 31 bytes. Both are rejected by current binary validation.
Rebuild old dictionaries from their locked source; the bundle diff command
can still compare normalized lexicons from schemas 1, 2 and 3.
