# Offline inventory snapshot comparison

## What it does

The comparison panel accepts two BLES 01.24 character QZ payload files. It
checks each file's exact size, type hash, and body length using the same decoder
as the existing snapshot viewer. It compares occupied rows by inventory slot
and the signed item-index field, then reports added, removed, or changed rows.
Rows with the same slot and item index count as unchanged.

The comparison does not read the QZ SOID, retain file contents, show instance
or owner identifiers, access the console, send network requests, or modify an
inventory. Files are read into page memory and the file inputs are cleared when
the operation finishes. Only slot, item index, and equipped-position metadata
are kept for display.

## Limits

Because instance IDs are intentionally suppressed, two distinct instances of
the same item index in the same slot are treated as unchanged. A move between
slots is reported as a removal plus an addition. The account QZ block remains
an unverified vault-like candidate and is not included in this comparison.
This feature can review observed snapshots but cannot perform or validate an
inventory action by itself.

The parser and comparison logic are covered by
`tests/offline-inventory-comparison.test.cjs`.
