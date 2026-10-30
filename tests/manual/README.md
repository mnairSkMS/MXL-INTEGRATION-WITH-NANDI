# Manual verification harnesses

Scenarios that are run by hand, not by `tests/run.sh`. The runner scans `tests/tests` for `test.sh`,
so nothing here is collected automatically.

They exist for behaviour the automated suites do not assert — usually because the assertion needs a
human eye, real-world media, or a long run.

| harness | what it exercises |
| --- | --- |
| `ad-insertion` | SCTE-35 ad insertion with real 1080p commercials over a six-break, five-minute run. Reproduces both open defects recorded in `doc/codebase/34-scte-ad-insertion.md`, neither of which `tests/tests/ad_insertion` can catch. |

## Rules for anything added here

- **Generated media never goes in the repository.** Write it to a scratch directory outside the tree
  (`ad-insertion` uses `MEDIA_DIR`, default `/var/tmp/utpl-ad-demo`, and generates ~350 MB).
- Keep the checkout side to scripts, configs, and a README.
- Say plainly in the README what the harness proves and what it does not.
