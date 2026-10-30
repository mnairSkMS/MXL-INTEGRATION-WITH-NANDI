# Running

* `tests/run.sh <path under tests/tests>` runs every `test.sh` below it, for example
  `tests/run.sh mxl` for the MXL SDK tests. Inside `utpl/transcoder-test` the tree is at `/tests`:
  `docker run --rm --user $(id -u):$(id -g) --entrypoint bash -v $PWD/tests:/tests utpl/transcoder-test:<tag> -c 'cd /tests && bash run.sh mxl'`
* `tests/tests/mxl/sdk` tests need only GStreamer and the MXL plugin (`tests/framework/mxl.sh`).
  They return `Skipped` if the image was built with `ENABLE_MXL=0`.
* `tests/manual/mxl/*.sh` need Docker on the host and are not collected by `run.sh`.

# Configuration environment variables

* **TEST_LOG_TO_STDOUT** - Enable logging to stdout
* **TEST_DISABLE_CLEANUP** - Disable cleanup procedure even a test passes
