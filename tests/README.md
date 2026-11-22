# Chapter-reading tests

Acceptance tests for the MP4 chapter support added to the `faad` front end.

## What is covered

| Fixture | Chapter representation | Purpose |
| --- | --- | --- |
| `data/chapters.mp4` | Apple `chpl` atom **and** an ISO chapter-reference text track | fixed chapter table, primary code path |
| `data/textchap.mp4` | ISO chapter-reference text track only (`chpl` stripped) | fallback code path |
| `data/nochapters.mp4` | none | empty table must be reported without error |

The expected chapter table is hard-coded in `bin/test_chapters.sh`:

| # | Start | Title |
| --- | --- | --- |
| 1 | `00:00.0` | `Intro / 序章` |
| 2 | `00:00.50` | `Main` |
| 3 | `00:01.50` | `Outro / 終わり` |

Titles intentionally contain multi-byte UTF-8 sequences so that encoding
handling is exercised.

## Running

```sh
ctest --test-dir build --output-on-failure
```

or directly (the binary defaults to `build/faad`):

```sh
tests/bin/test_chapters.sh
FAAD=/path/to/faad tests/bin/test_chapters.sh
```

## Regenerating the fixtures

The checked-in `.mp4` files are binary fixtures committed so the suite runs
without FFmpeg. To regenerate them (requires FFmpeg with the native AAC
encoder and MP4 muxer):

```sh
tests/bin/make_fixtures.sh            # chapters.mp4 + nochapters.mp4
python3 -I tests/bin/strip_chpl.py \
    tests/data/chapters.mp4 tests/data/textchap.mp4
```

All helper Python scripts use the standard library only and should be run
with `python3 -I`.

## Other helpers

- `bin/atomdump.py` — minimal ISO-BMFF atom tree dumper, including a `chpl`
  decoder.
- `bin/make_malformed.py` — builds a corpus of truncated / corrupted MP4s for
  sanitizer robustness runs (not part of the normal test).
