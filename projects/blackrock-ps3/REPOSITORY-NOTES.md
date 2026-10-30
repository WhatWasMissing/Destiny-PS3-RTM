# Repository copy notes

This directory preserves the supplied edited BlackrockPS3 source snapshot as a
separate project within the Destiny RTM repository. The source archive's outer
folder has been removed so `CMakeLists.txt`, `src/`, `data/`, `tests/`, and
`third_party/` sit directly under this directory.

`PS3MAPI-stability.patch` is included as a review record of the source changes.
It is a diff from the unmodified source snapshot; do not apply it to this already
patched tree. See `PATCH_NOTES.md` for scope, build instructions, and test limits.

The attached preview executable predates the patch and is intentionally not
included. This source has not been verified with a rebuilt Windows executable or
a live PS3.
