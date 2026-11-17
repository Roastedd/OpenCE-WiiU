# Compression files for Wii U map import

Added unchanged from [madler/zlib v1.3.2](https://github.com/madler/zlib/tree/da607da739fa6047df13e66a2af6b8bec7c2a498),
the same version as the vendored inflater. The existing zlib licence
in LICENSE applies. The files are compiled with Z_PREFIX to keep them
separate from the original game's zlib 1.1.3. Used by the first-run map
importer to write converted maps.

```text
ea9ca4eb51b1a71aaf214b99f4a9fab177253d6268cae8e4c26d44d1812e52c2  deflate.c
21f4ea0973dab272fb26e93305ccf8628c8dc07e6c5df09a9861825b1920ba5d  deflate.h
a16217d5ea6d8342ff08afe5207b1dab620f05610e121dda55b4f9aae9de4f68  trees.c
bb0a9d3ca88ee00c81adb7c636e73b97085f6ef1b52d6d58edbe2b6dc3adeb4d  trees.h
```
