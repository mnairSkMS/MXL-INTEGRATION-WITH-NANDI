# Running

* `tests/run.sh <path under tests/tests>` runs every `test.sh` below it, for example
  `tests/run.sh mxl` for the MXL SDK tests. Inside `utpl/transcoder-test` the tree is at `/tests`:
  `docker run --rm --user $(id -u):$(id -g) --entrypoint bash -v $PWD/tests:/tests utpl/transcoder-test:<tag> -c 'cd /tests && bash run.sh mxl'`
* `tests/tests/mxl/sdk` tests need only GStreamer and the MXL plugin (`tests/framework/mxl.sh`).
  They return `Skipped` if the image was built with `ENABLE_MXL=0`.
* `tests/tests/mxl/tap` (14 tests of the input source tap, `inputs[n].mxlTap`) and `tests/tests/mxl/programme`
  (6 tests of the programme output, `outputs[n].type = "mxl"`) start the real engine on a generated UDP source,
  so they need the licence at `/tmp/transcoder.lic` (present in `utpl/transcoder-test`) and report `Skipped`
  without it. To test a build made in a scratch directory instead of the engine in the image, mount it and set
  `MXL_ENGINE_BUILD_DIR`. Pass `-e TEST_DISABLE_CLEANUP=1` to keep a failed run's `tmp/` (engine log, config,
  captures). `bash run.sh mxl/tap` runs all of a suite and `bash run.sh mxl/tap/09-redundant-failover` one test;
  the runner takes a directory, not a glob. Some print `RESULT` lines with measurements.
* `tests/manual/mxl/*.sh` need Docker on the host and are not collected by `run.sh`. `tap-soak.sh [minutes]` is
  a long run of the source tap (drift of the flows against the clock, lip-sync, memory, CPU).

# Configuration environment variables

* **TEST_LOG_TO_STDOUT** - Enable logging to stdout
* **TEST_DISABLE_CLEANUP** - Disable cleanup procedure even a test passes
