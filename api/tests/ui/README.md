# Operator UI smoke test

Loads the real `operator.html`, `operator.js` and `rundown.js` under jsdom with `fetch` stubbed
against **captured live API responses**, and drives the page the way an operator would.

It exists because a truthiness bug (`ops` plus `document: {}` returning 202 instead of 422) reached a
live demo while the Python suite was green: the suite only ever sent a truthy document. Clicking
through found what the unit tests could not.

## Run it

```bash
cd api/tests/ui
docker run --rm -v "$PWD:/w" -v "$PWD/../../src/transcoder_api/static:/s:ro" -w /w \
  node:20-alpine sh -c "npm i --silent jsdom && node drive.mjs enabled"
```

Modes, run all five:

| Mode | Covers |
|---|---|
| `enabled` | The grid: projection, expansion, the 1 Hz no-rebuild guarantee, Validate/Apply, YAML import preview. |
| `anchor` | Pins the **last** program on air so rows exist above it. Without this the next-cycle branch of the projection is never reached, and its assertion passes vacuously whenever the fixtures were captured with the first clip playing. |
| `wallclock` | Graphics tied to no program still have a home in the table. |
| `disabled` | `ENABLE_RUNDOWN_SERVICE=false` hands the table back to the plain queue renderer. |
| `ssai` | The SSAI panel: the Segmentation Message combobox (five alphabetical matches, search by name or hex id, mouse and keyboard commit, Escape restoring the committed value) syncing both ways with `segmentation_type_id`, the reserved-id label, the UPID gate on Splice Signaling, the break-end guard on Time Signal, the Table 23 reference panel and its three dismissals, and the `segmentation_type_id` actually posted by Cue Out. |

Exit code is non-zero if any assertion fails.

## The browser pass — `browser.mjs`

`drive.mjs` runs under jsdom, which has no layout, no painting and no hit testing. That blind
spot is real: the first run of `browser.mjs` found `.modal{display:flex}` overriding the
`hidden` attribute, leaving an invisible full-page backdrop at `z-index:1000` that swallowed
every click on the operator page — while every jsdom assertion passed, because `el.hidden`
was true.

So this one drives real Chromium against a **running stack** and asserts only what needs one:
geometry, stacking, what is actually painted at a point, sticky headers, computed style. It
also writes screenshots to `shots/`.

```bash
cd api/tests/ui
docker run --rm --network host -v "$PWD:/w" -w /w mcr.microsoft.com/playwright:latest \
  sh -c "npm i --silent playwright@1.46.1 && node browser.mjs"
```

Pin the npm package to the image's own Playwright version — the `:latest` image trails the
`latest` npm release, and a mismatch fails with "Executable doesn't exist". Check the image's
version first if that command errors.

Behaviour a DOM alone can settle belongs in `drive.mjs`, which needs no server and runs far
faster. Keep the split.

## Refresh the fixtures

```bash
python3 capture_fixtures.py            # against a running stack on :8080
```

Do this whenever a response shape changes. Replaying stale fixtures is how a harness starts agreeing
with itself instead of with the server.

## Before trusting a change to the harness

Inject the regression it is supposed to catch into a **copy** of the static files, confirm exit 1,
then delete the copy. A test that has never failed has not been shown to work.
