# nanopb runtime

The C runtime of [nanopb](https://github.com/nanopb/nanopb), vendored
unchanged. The mesh vendor model encodes every mesh message with it.

| | |
|---|---|
| Release | `nanopb-0.4.9.1` |
| Source | https://github.com/nanopb/nanopb/archive/refs/tags/nanopb-0.4.9.1.tar.gz |
| SHA-256 of the tarball | `0d3d090c6237d5026de2926105a5bfca505f6eaeaff01d5b0104861b2d1e2209` |
| Files | `pb.h`, `pb_common.c/.h`, `pb_decode.c/.h`, `pb_encode.c/.h`, `LICENSE.txt` |
| License | zlib (`LICENSE.txt`) |

The code generator must be the same release: `nanopb==0.4.9.1` in
`proto/requirements.txt`. To update, replace these files from the new
release's tarball, update the table, and bump the generator pin to match.
