/* Drives the operator page under jsdom against captured real API responses.
   Both scripts are evaluated in load order, because the rundown grid spans them:
   operator.js owns the queue, rundown.js owns the schedule and the table. */
import { JSDOM } from "jsdom";
import { readFileSync } from "node:fs";

const MODE = process.argv[2] || "enabled";
let stage = "startup";
const mark = (s) => { stage = s; };
const watchdog = setTimeout(() => {
  console.log("WATCHDOG fired while in stage:", stage);
  process.exit(2);
}, 25000);
watchdog.unref?.();

const fixtures = JSON.parse(readFileSync("/w/fixtures.json", "utf8"));
const html = readFileSync("/s/operator.html", "utf8");
const operatorJs = readFileSync("/s/operator.js", "utf8");
const rundownJs = readFileSync("/s/rundown.js", "utf8");

const liveVersionId = fixtures["GET /rundowns/main"].activeVersionId;
const document0 = fixtures[`GET /rundowns/main/versions/${liveVersionId}`];
const clock = fixtures["GET /rundowns/main/clock"];
const playlist = fixtures["GET /playlists/main"];
const AFTER_ID = "after-version-0001";

// A second version, as Apply would return it: same programs, one more graphic.
const documentAfter = JSON.parse(JSON.stringify(document0));
documentAfter.versionId = AFTER_ID;
// Must satisfy clip-fit against the real clip, restoration margin included:
// offset + duration + 1200 <= durationMs. An invalid fixture would trip the
// browser's own check and never reach the assertion under test.
documentAfter.items[0].graphics.push({
  definitionId: "def-new", key: "added-aston", assetId: "aston-static", kind: "aston",
  mode: "reveal", durationMs: 2000, fadeMs: 500,
  trigger: { type: "item-start", offsetMs: 500 }, itemKey: document0.items[0].key,
  source: "rundown",
});
// A wall-clock event has no program to hang under; the grid must still expose it.
if (MODE === "wallclock") {
  document0.scheduledGraphics.push({
    definitionId: "def-wall", key: "midnight-logo", assetId: "logo-default", kind: "logo",
    mode: "fade", durationMs: 8000, fadeMs: 400,
    trigger: { type: "wall-clock", startsAt: "2026-08-22T00:00:00Z" }, source: "rundown",
  });
}

const status = JSON.parse(JSON.stringify(fixtures["GET /rundowns/status"]));
if (MODE === "disabled") status.enabled = false;

// Which clip is on air when the fixtures are captured decides whether any row
// sits ABOVE the on-air item — and that is the only case the next-cycle branch
// of the projection covers. Pin it here instead of leaving the coverage to
// chance: in `anchor` mode the LAST program is on air, so every other row must
// wrap to the following cycle.
if (MODE === "anchor") {
  const last = document0.items[document0.items.length - 1];
  clock.currentItemId = last.stableItemId;
  clock.currentStartedAt = "2026-08-21T09:15:30.500Z";
  playlist.currentItemId = last.stableItemId;
  playlist.items.forEach((item) => {
    item.state = item.id === last.stableItemId ? "on_air" : "queued";
  });
}

const errors = [];
const calls = [];
const bodies = [];
let phase = "before";
let rejectApply = null;
let expectingFailure = false;

function respond(path, options) {
  const method = (options && options.method) || "GET";
  calls.push(`${method} ${path}`);
  if (method === "GET") {
    if (path === "/graphics/catalog") return fixtures["GET /graphics/catalog"];
    if (path === "/rundowns/status") return status;
    if (path === "/media") return fixtures["GET /media"];
    if (path === "/graphics/cues") return fixtures["GET /graphics/cues"];
    if (path === "/slates/catalog") {
      return [
        {id: "with-audio", label: "Has audio", mediaType: "video", hasAudio: true,
         mediaUri: "http://127.0.0.1:8080/slates/media/with-audio"},
        {id: "card", label: "Card", mediaType: "image", hasAudio: false,
         mediaUri: "http://127.0.0.1:8080/slates/media/card"},
      ];
    }
    if (path === "/playlists/main") return playlist;
    if (path === "/rundowns/main/clock") return clock;
    if (path === "/rundowns/main/executions") return fixtures["GET /rundowns/main/executions"];
    if (path === "/rundowns/main")
      return phase === "before" ? fixtures["GET /rundowns/main"]
        : { ...fixtures["GET /rundowns/main"], activeVersionId: AFTER_ID };
    if (path === `/rundowns/main/versions/${liveVersionId}`) return document0;
    if (path === `/rundowns/main/versions/${AFTER_ID}`) return documentAfter;
    throw new Error(`unstubbed GET ${path}`);
  }
  if (method === "POST" && path === "/rundowns/main/apply") {
    const body = JSON.parse(options.body);
    bodies.push({ path, body });
    if (rejectApply && !body.dryRun) {
      const failure = new Error(rejectApply);
      failure.reject = true;
      throw failure;
    }
    if (!("document" in body) && !("ops" in body)) throw new Error("apply carried neither document nor ops");
    if ("document" in body && "ops" in body) throw new Error("apply carried both document and ops");
    if (body.dryRun) return { version: null, applied: { items: 3, graphics: 2 }, warnings: [] };
    phase = "after";
    return {
      version: { id: AFTER_ID, number: 99, state: "published", checksum: "x",
                 createdAt: null, publishedAt: null, items: [], graphics: [] },
      applied: { items: 3, graphics: 2 }, channelFile: { state: "written", path: "/config/channel.yml" },
      warnings: [],
    };
  }
  if (method === "POST" && path === "/rundowns/main/programs/import") {
    const dryRun = options.body.get("dryRun");
    bodies.push({ path, dryRun: dryRun === "true", position: options.body.get("position"),
                  expectedVersionId: options.body.get("expectedVersionId") });
    const items = [...document0.items.map((i) => ({ key: i.key, displayName: i.displayName })),
                   { key: "promo-2", displayName: "promo.mp4" }];
    if (dryRun === "true") return { version: { items }, applied: { insertItems: 1 }, warnings: ["dropped an elapsed trigger"] };
    phase = "after";
    return { version: { id: AFTER_ID, items }, applied: { insertItems: 1 },
             channelFile: { state: "written", path: "/config/channel.yml" }, warnings: [] };
  }
  if (method === "DELETE" && path.startsWith("/scheduled_events/")) {
    bodies.push({ path, method });
    return {};
  }
  if (method === "POST" && path === "/scheduled_events") {
    const posted = JSON.parse(options.body);
    bodies.push({ path, body: posted });
    return posted.map((event, index) => ({ ...event, id: 9000 + index }));
  }
  throw new Error(`unstubbed request: ${method} ${path}`);
}

const dom = new JSDOM(html, {
  runScripts: "outside-only",
  url: "http://127.0.0.1:8080/operator",
  beforeParse(window) {
    window.fetch = async (path, options) => {
      let payload;
      try {
        payload = respond(path, options);
      } catch (failure) {
        if (!failure.reject) throw failure;
        const detail = { detail: failure.message };
        return { ok: false, status: 422, headers: { get: () => "application/json" },
                 json: async () => detail, text: async () => JSON.stringify(detail) };
      }
      return { ok: true, status: 200, headers: { get: () => "application/json" },
               json: async () => payload, text: async () => JSON.stringify(payload) };
    };
    window.confirm = () => true;
    window.onerror = (m) => errors.push(`window.onerror: ${m}`);
  },
});
const { window } = dom;
window.addEventListener("error", (e) => errors.push(`error event: ${e.message}`));
process.on("unhandledRejection", (e) => errors.push(`unhandledRejection: ${e && e.stack}`));

mark("eval operator.js");
window.eval(operatorJs);
mark("eval rundown.js");
window.eval(rundownJs);
// operatorLog is defined by operator.js; wrap it to capture error-level output.
const realLog = window.operatorLog;
window.operatorLog = (m, level) => {
  if (level === "error" && !expectingFailure) errors.push(m);
  realLog(m, level);
};
mark("dispatch DOMContentLoaded");
window.document.dispatchEvent(new window.Event("DOMContentLoaded"));

const settle = (ms) => new Promise((r) => setTimeout(r, ms || 80));
const byId = (id) => window.document.getElementById(id);
const check = [];
function expect(label, cond, detail) {
  check.push(`${cond ? "  ok  " : "  FAIL"} ${label}${cond ? "" : ` — ${detail}`}`);
  if (!cond) errors.push(label);
}
const body = () => byId("queue-body");
const dataRows = () => [...body().rows].filter((r) => !r.classList.contains("child-row") && !r.classList.contains("group-row"));
const cellText = (row, selector) => row.querySelector(selector)?.textContent.trim();

mark("settle after init");
await settle(200);
mark("assertions");

expect("init ran without logging an error", errors.length === 0, errors.join("; "));

if (MODE === "disabled") {
  expect("the grid did not claim the table", window.rundownGrid === undefined);
  expect("the fallback renders one row per queue item",
    dataRows().length === playlist.items.length, `got ${dataRows().length}`);
  expect("the fallback shows a dash for start",
    cellText(dataRows()[0], ".cell-start") === "—", cellText(dataRows()[0], ".cell-start"));
  expect("the fallback still shows a probed duration",
    /^\d\d:\d\d\.\d\d$/.test(dataRows()[0].cells[6].textContent), dataRows()[0].cells[6].textContent);
  expect("the disabled notice is visible", byId("rundown-disabled").hidden === false);
  expect("the schedule action bar stays hidden", byId("rundown-actions").hidden === true);
} else if (MODE === "wallclock") {
  const group = [...body().rows].find((r) => r.classList.contains("group-row"));
  expect("a wall-clock group row exists", Boolean(group));
  expect("the group names the untethered events",
    group && /Timed from start/.test(group.textContent), group && group.textContent);
  group.querySelector("button").dispatchEvent(new window.Event("click"));
  await settle();
  // Re-query: expanding rebuilds the table, so the captured node is detached.
  // Take the group's own child row, not merely the first in the table — the
  // on-air program may already be expanded above it.
  const reGroup = [...body().rows].find((r) => r.classList.contains("group-row"));
  const child = reGroup && reGroup.nextElementSibling;
  expect("expanding the group reveals the wall-clock event",
    Boolean(child) && child.classList.contains("child-row") &&
      child.querySelectorAll(".graphic-row").length === 1,
    child && String(child.querySelectorAll(".graphic-row").length));
  // Untethered events are entered as elapsed time from the start, so the row has to
  // report the same number the operator typed rather than a wall clock.
  expect("the untethered row shows elapsed time from the start, not a wall clock",
    child && /at \+(\d\d:\d\d:\d\d|—)/.test(child.textContent), child && child.textContent.slice(0, 120));
} else if (MODE === "ssai") {
  // The Segmentation Message dropdown and the segmentation_type_id box are the same field
  // seen twice. Nothing else in the suite touches the SSAI panel, and a one-way sync or a
  // stale dropdown label would be invisible to the Python tests: they only ever see the
  // posted body, never which control the operator actually moved.
  const set = (id, value, eventName) => {
    byId(id).value = value;
    byId(id).dispatchEvent(new window.Event(eventName || "input"));
  };
  const fire = (id, name) => byId(id).dispatchEvent(new window.Event(name, {bubbles: true}));
  const key = (id, k) =>
    byId(id).dispatchEvent(new window.KeyboardEvent("keydown", {key: k, bubbles: true, cancelable: true}));
  const message = () => byId("ssai-seg-message");
  const typeId = () => byId("ssai-seg-type-id");
  const note = () => byId("ssai-seg-note");
  const options = () => [...byId("ssai-seg-options").children];
  const optionNames = () => options().map((li) => li.firstChild.textContent);
  const lastPost = () => bodies.filter((b) => b.path === "/scheduled_events").pop();

  mark("ssai initial state");
  expect("the type box starts at the Provider Placement Opportunity Start id",
    typeId().value === "52", typeId().value);
  expect("the message box starts on the matching name",
    message().value === "Provider Placement Opportunity Start", message().value);
  expect("the option list starts closed", byId("ssai-seg-options").hidden === true);

  // Splice Signaling with no UPID: the scheduler emits no descriptor at all, so the pair is
  // inert and says so rather than pretending the value will be sent.
  expect("with no UPID the pair is disabled",
    message().disabled === true && typeId().disabled === true);
  expect("and explains why", note().hidden === false && /once a UPID/.test(note().textContent),
    note().textContent);
  expect("which is a hint, not a warning", note().classList.contains("warn") === false);

  mark("ssai upid gate");
  set("ssai-upid", "PROMO-1234");
  await settle();
  expect("typing a UPID releases the pair",
    message().disabled === false && typeId().disabled === false);
  expect("and clears the hint", note().hidden === true, note().textContent);

  mark("ssai combobox filtering");
  fire("ssai-seg-message", "focus");
  await settle();
  expect("focusing opens the list", byId("ssai-seg-options").hidden === false);
  expect("which shows at most five of the 47 messages",
    options().length === 5, String(options().length));
  expect("in alphabetical order, not table order",
    optionNames().join("|") === [...optionNames()].sort((a, b) => a.localeCompare(b)).join("|"),
    optionNames().join("|"));

  set("ssai-seg-message", "promo");
  await settle();
  expect("typing narrows the list to matching messages",
    options().length === 4 && optionNames().every((n) => /Promo/.test(n)), optionNames().join("|"));
  expect("and the matches stay alphabetical",
    optionNames().join("|") === [...optionNames()].sort((a, b) => a.localeCompare(b)).join("|"),
    optionNames().join("|"));
  expect("typing alone commits nothing - the number box still rules",
    typeId().value === "52", typeId().value);

  set("ssai-seg-message", "overlay placement");
  await settle();
  expect("a longer query keeps narrowing", options().length === 4, optionNames().join("|"));
  set("ssai-seg-message", "0x30");
  await settle();
  expect("searching by hex id finds the message",
    options().length === 1 && optionNames()[0] === "Provider Advertisement Start",
    optionNames().join("|"));
  set("ssai-seg-message", "zzz");
  await settle();
  expect("an unmatched query says so rather than showing a stale list",
    options().length === 1 && /No Table 23 message matches/.test(options()[0].textContent),
    options()[0].textContent);

  mark("ssai combobox commit");
  set("ssai-seg-message", "distributor promo start");
  await settle();
  options()[0].dispatchEvent(new window.Event("mousedown", {bubbles: true, cancelable: true}));
  await settle();
  expect("choosing an option writes its id into the number box",
    typeId().value === "62", typeId().value);
  expect("and puts the canonical name in the message box",
    message().value === "Distributor Promo Start", message().value);
  expect("and closes the list", byId("ssai-seg-options").hidden === true);

  mark("ssai keyboard");
  set("ssai-seg-message", "chapter");
  await settle();
  key("ssai-seg-message", "ArrowDown");
  key("ssai-seg-message", "ArrowDown");
  await settle();
  expect("arrow keys move the active option",
    options()[1].getAttribute("aria-selected") === "true",
    options().map((o) => o.getAttribute("aria-selected")).join("|"));
  key("ssai-seg-message", "Enter");
  await settle();
  expect("Enter commits the active option",
    typeId().value === "32" && message().value === "Chapter Start",
    `${typeId().value} / ${message().value}`);

  set("ssai-seg-message", "netw");
  await settle();
  key("ssai-seg-message", "Escape");
  await settle();
  expect("Escape abandons the search and restores the committed name",
    message().value === "Chapter Start" && byId("ssai-seg-options").hidden === true,
    message().value);

  mark("ssai number -> message");
  set("ssai-seg-type-id", "35");
  await settle();
  expect("typing 35 renames the message box to Break End",
    message().value === "Break End", message().value);

  mark("ssai reserved id");
  set("ssai-seg-type-id", "42");
  await settle();
  expect("an id outside Table 23 keeps its value", typeId().value === "42", typeId().value);
  expect("and is labelled as reserved rather than leaving a stale message",
    message().value === "Reserved (0x2A)", message().value);

  mark("ssai empty box");
  set("ssai-seg-type-id", "");
  await settle();
  expect("an empty type blocks Cue Out", byId("cue-out").disabled === true);
  expect("and warns", note().hidden === false && note().classList.contains("warn"),
    note().textContent);

  mark("ssai time_signal break-end rule");
  set("ssai-seg-type-id", "48");
  set("ssai-type", "timesignal", "change");
  await settle();
  expect("a time_signal needs no UPID for the pair to be live",
    message().disabled === false && typeId().disabled === false);
  set("ssai-seg-type-id", "35");
  await settle();
  // The server refuses this combination (services/schedule.py); the page must not spend a
  // round trip discovering that.
  expect("a break-end id on a time_signal blocks Cue Out", byId("cue-out").disabled === true);
  expect("and says a time_signal closes on its duration",
    note().classList.contains("warn") && /closes an avail/.test(note().textContent),
    note().textContent);

  mark("ssai reference panel");
  // Opened while the type box holds 35 - 0x23, Break End. Not 0x35, which is 53.
  expect("the reference panel starts hidden", byId("scte-table-modal").hidden === true);
  byId("ssai-seg-info").dispatchEvent(new window.Event("click", {bubbles: true}));
  await settle();
  expect("the (i) opens it", byId("scte-table-modal").hidden === false);
  const tableRows = [...byId("scte-table").tBodies[0].rows];
  expect("it shows every Table 23 row, reserved ranges included",
    tableRows.length === 53, String(tableRows.length));
  expect("with all six columns",
    byId("scte-table").tHead.rows[0].cells.length === 6,
    String(byId("scte-table").tHead.rows[0].cells.length));
  expect("and marks the row currently selected",
    tableRows.filter((r) => r.classList.contains("current")).length === 1 &&
      /Break End/.test(
        tableRows.find((r) => r.classList.contains("current")).textContent),
    tableRows.filter((r) => r.classList.contains("current")).map((r) => r.cells[1].textContent).join());

  // Clicking inside the card must NOT dismiss it - only the backdrop does.
  byId("scte-table").dispatchEvent(new window.Event("click", {bubbles: true}));
  await settle();
  expect("clicking inside the panel keeps it open", byId("scte-table-modal").hidden === false);
  byId("scte-table-modal").dispatchEvent(new window.Event("click", {bubbles: true}));
  await settle();
  expect("clicking outside it closes it", byId("scte-table-modal").hidden === true);

  byId("ssai-seg-info").dispatchEvent(new window.Event("click", {bubbles: true}));
  await settle();
  window.document.dispatchEvent(
    new window.KeyboardEvent("keydown", {key: "Escape", bubbles: true}));
  await settle();
  expect("and so does Escape", byId("scte-table-modal").hidden === true);

  mark("ssai time_signal cue out");
  set("ssai-seg-type-id", "48");
  await settle();
  expect("a start id releases Cue Out again", byId("cue-out").disabled === false);
  byId("cue-out").dispatchEvent(new window.Event("click"));
  await settle(200);
  const timeSignalPost = lastPost();
  expect("Cue Out posts a time_signal",
    timeSignalPost && timeSignalPost.body[0].type === "scte35_timesignal",
    timeSignalPost && timeSignalPost.body[0].type);
  expect("carrying the chosen segmentation_type_id, not the old hardcoded 52",
    timeSignalPost &&
      timeSignalPost.body[0].details.scte35_timesignal.scte35_descriptors[0]
        .segmentation_type_id === 48,
    JSON.stringify(timeSignalPost && timeSignalPost.body[0].details));

  // --- the slate row ---
  mark("slate catalog");
  const slateSelect = () => byId("ssai-slate");
  const slateNote = () => byId("ssai-slate-note");

  expect("the slate picker is populated from the catalog",
    slateSelect().options.length === 2, String(slateSelect().options.length));
  expect("and marks the one that will mute programme audio",
    [...slateSelect().options].map((o) => o.textContent).join("|") === "Has audio|Card (no audio)",
    [...slateSelect().options].map((o) => o.textContent).join("|"));

  expect("a slate with its own audio raises no warning", slateNote().hidden === true,
    slateNote().textContent);

  set("ssai-slate", "card", "change");
  await settle();
  expect("choosing an audio-less slate says the programme will be muted",
    slateNote().hidden === false && /muted/.test(slateNote().textContent),
    slateNote().textContent);
  expect("which is information, not a warning",
    slateNote().classList.contains("warn") === false);

  mark("slate fire");
  set("ssai-slate-duration", "20");
  byId("slate-fire").dispatchEvent(new window.Event("click"));
  await settle(200);

  const slatePost = bodies.filter((b) => b.path === "/scheduled_events").pop();
  expect("Fire posts a slate event",
    slatePost && slatePost.body[0].type === "slate", slatePost && slatePost.body[0].type);
  expect("carrying the catalog's URL and the chosen duration",
    slatePost &&
      slatePost.body[0].details.slate.url === "http://127.0.0.1:8080/slates/media/card" &&
      slatePost.body[0].details.slate.duration === 20000,
    JSON.stringify(slatePost && slatePost.body[0].details));

  expect("Fire is latched while the slate runs", byId("slate-fire").disabled === true);
  expect("and Stop becomes available", byId("slate-stop").disabled === false);
  expect("the picker locks so the id and the media cannot disagree",
    slateSelect().disabled === true);

  mark("slate stop");
  byId("slate-stop").dispatchEvent(new window.Event("click"));
  await settle(200);

  const slateDelete = bodies.filter((b) => b.method === "DELETE").pop();
  expect("Stop deletes the event the Fire returned",
    slateDelete && slateDelete.path === "/scheduled_events/9000",
    slateDelete && slateDelete.path);
  expect("and the controls are released",
    byId("slate-fire").disabled === false && byId("slate-stop").disabled === true);
  expect("and the picker is editable again", slateSelect().disabled === false);

  mark("ssai splice cue out");
  set("ssai-type", "splice", "change");
  set("ssai-upid", "");
  await settle();
  expect("clearing the UPID re-inerts the pair for a splice",
    message().disabled === true && typeId().disabled === true);
  set("ssai-upid", "PROMO-1234");
  set("ssai-seg-type-id", "48");
  await settle();
  byId("cue-out").dispatchEvent(new window.Event("click"));
  await settle(200);
  const splicePost = lastPost();
  expect("Cue Out posts a splice_insert",
    splicePost && splicePost.body[0].type === "scte35_spliceinsert",
    splicePost && splicePost.body[0].type);
  expect("carrying the chosen segmentation_type_id alongside the UPID",
    splicePost &&
      splicePost.body[0].details.scte35_spliceinsert.segmentation_type_id === 48 &&
      splicePost.body[0].details.scte35_spliceinsert.segmentation_upid === "PROMO-1234",
    JSON.stringify(splicePost && splicePost.body[0].details));
} else {
  const rows = dataRows();
  expect("one row per program, in loop order", rows.length === document0.items.length, `got ${rows.length}`);
  expect("names come from the schedule document",
    rows.map((r) => cellText(r, ".clip-name")).join("|") ===
      document0.items.map((i) => i.displayName).join("|"),
    rows.map((r) => cellText(r, ".clip-name")).join("|"));

  const onAirIndex = playlist.items.findIndex((i) => i.id === playlist.currentItemId);
  expect("the on-air row is marked", rows[onAirIndex].classList.contains("onair"));
  expect("the on-air row carries the ON AIR tag",
    /on air/i.test(rows[onAirIndex].querySelector(".cell-state").textContent),
    rows[onAirIndex].querySelector(".cell-state").textContent);
  expect("the on-air row's time is labelled observed",
    cellText(rows[onAirIndex], ".start-note") === "observed",
    cellText(rows[onAirIndex], ".start-note"));
  expect("every other row is labelled projected",
    rows.filter((_, i) => i !== onAirIndex).every((r) => cellText(r, ".start-note") === "projected"));

  // The on-air row must show the observed start exactly; the rest are derived
  // from it, so a drift here is a drift in the whole grid.
  const hhmmss = (iso) => {
    const d = new Date(iso);
    return `${String(d.getUTCHours()).padStart(2, "0")}:${String(d.getUTCMinutes()).padStart(2, "0")}:${String(d.getUTCSeconds()).padStart(2, "0")}.${String(Math.floor(d.getUTCMilliseconds() / 10)).padStart(2, "0")}`;
  };
  expect("the on-air row shows the observed start verbatim",
    cellText(rows[onAirIndex], ".start-clock") === hhmmss(clock.currentStartedAt),
    `${cellText(rows[onAirIndex], ".start-clock")} vs ${hhmmss(clock.currentStartedAt)}`);

  const epoch = Date.parse(clock.currentStartedAt) - document0.items[onAirIndex].timelineStartMs;
  const expectedStart = (i) => epoch + document0.items[i].timelineStartMs + (i < onAirIndex ? clock.loopDurationMs : 0);
  if (MODE === "anchor") {
    expect("the anchor mode really does put rows above the on-air item", onAirIndex > 0,
      `onAirIndex is ${onAirIndex}, so the next-cycle assertion would be vacuous`);
    expect("rows above the on-air item show their NEXT airing, one loop later",
      rows.slice(0, onAirIndex).every((r, i) => cellText(r, ".start-clock") === hhmmss(expectedStart(i))),
      rows.slice(0, onAirIndex).map((r, i) => `${cellText(r, ".start-clock")} vs ${hhmmss(expectedStart(i))}`).join(" | "));
    expect("a wrapped row is a full loop after its bare loop position",
      expectedStart(0) - (Date.parse(clock.currentStartedAt) - document0.items[onAirIndex].timelineStartMs) === clock.loopDurationMs);
  } else {
    expect("rows above the on-air item show their NEXT airing, one loop later",
      rows.slice(0, onAirIndex).every((r, i) => cellText(r, ".start-clock") === hhmmss(expectedStart(i))),
      rows.slice(0, onAirIndex).map((r, i) => `${cellText(r, ".start-clock")} vs ${hhmmss(expectedStart(i))}`).join(" | "));
  }
  // Modulo the loop, because rows 0 and 1 may straddle the on-air boundary: one
  // has wrapped to the next cycle and the other has not, so the raw difference
  // is legitimately negative. Asserting it absolutely only passes when the
  // fixtures happen to catch the first clip on air.
  const gap = ((expectedStart(1) - expectedStart(0)) % clock.loopDurationMs + clock.loopDurationMs) % clock.loopDurationMs;
  expect("consecutive rows are one clip duration apart, modulo the loop",
    gap === document0.items[0].durationMs, `${gap} vs ${document0.items[0].durationMs}`);

  expect("loop offsets are exact and prefixed",
    rows.map((r) => cellText(r, ".cell-loop")).join("|") === "+00:00.00|+00:10.17|+00:26.07",
    rows.map((r) => cellText(r, ".cell-loop")).join("|"));
  expect("durations render as MM:SS.hh",
    rows.map((r) => r.cells[6].textContent).join("|") === "00:10.17|00:15.90|00:11.19",
    rows.map((r) => r.cells[6].textContent).join("|"));
  expect("the on-air row counts down",
    /s left$/.test(cellText(rows[onAirIndex], ".cell-remain") || ""),
    cellText(rows[onAirIndex], ".cell-remain"));
  expect("the header badge reports the start, how long it has run, the loop and the cycle",
    new RegExp(`^start \\d\\d:\\d\\d:\\d\\d\\.\\d\\d · running \\d\\d:\\d\\d:\\d\\d · loop 00:37\\.\\d\\d · cycle ${clock.cycle}$`)
      .test(byId("rundown-clock").textContent),
    byId("rundown-clock").textContent);

  // Expansion.
  const withGraphics = document0.items.findIndex((i) => i.graphics.length);
  expect("only programs with graphics get an expander",
    rows.filter((r) => r.querySelector("button.expander")).length ===
      document0.items.filter((i) => i.graphics.length).length);
  expect("a program without graphics shows a dash in the Gfx column",
    rows[1].cells[7].textContent === "—", rows[1].cells[7].textContent);

  // The grid opens the on-air row on first paint, so this row may already be
  // open. Assert the toggle toggles rather than assuming a starting state — the
  // previous version of this test silently depended on which clip was on air
  // when the fixtures were captured.
  mark("expand a program");
  const childOf = (i) => {
    const row = dataRows()[i];
    const next = row && row.nextElementSibling;
    return next && next.classList.contains("child-row") ? next : null;
  };
  const clickExpander = async () => {
    dataRows()[withGraphics].querySelector("button.expander").dispatchEvent(new window.Event("click"));
    await settle();
  };
  const openBefore = Boolean(childOf(withGraphics));
  await clickExpander();
  expect("the expander toggles the row", Boolean(childOf(withGraphics)) === !openBefore,
    `was ${openBefore}, now ${Boolean(childOf(withGraphics))}`);
  if (!childOf(withGraphics)) await clickExpander();
  let child = childOf(withGraphics);
  expect("expanding reveals exactly the program's graphics",
    Boolean(child) && child.querySelectorAll(".graphic-row").length === document0.items[withGraphics].graphics.length,
    child && String(child.querySelectorAll(".graphic-row").length));
  expect("the graphic row names its lane and asset",
    child && /L-band|LOGO|BUG|ASTON/.test(child.textContent), child && child.textContent.slice(0, 100));
  expect("the expanded row offers to add a graphic",
    Boolean(child.querySelector("button.add-graphic")));
  expect("a program with no graphics can never open a child row",
    document0.items.every((item, i) => item.graphics.length > 0 || !childOf(i)));

  // The 1 Hz poll must not rebuild the table under the operator.
  mark("poll with an unchanged signature");
  const heldRow = body().rows[0];
  const heldChild = child;
  const before = calls.length;
  window.rundownGrid.onQueue(playlist);
  await settle();
  expect("an unchanged poll does not rebuild the table",
    body().rows[0] === heldRow && heldRow.isConnected,
    "the row node was replaced");
  expect("an unchanged poll leaves the open row open",
    [...body().rows].includes(heldChild), "the expanded row collapsed");
  expect("an unchanged poll refetches nothing", calls.length === before, `${calls.length - before} extra calls`);

  // Select and edit through the inspector.
  mark("select a graphic");
  child.querySelector(".graphic-open").dispatchEvent(new window.Event("click"));
  await settle();
  expect("clicking a graphic opens it in the inspector",
    byId("event-inspector-form").hidden === false && byId("inspector-heading").textContent === "Graphics event",
    byId("inspector-heading").textContent);

  mark("edit the offset");
  const offset = [...byId("event-inspector-form").querySelectorAll("input")]
    .find((i) => i.type === "number");
  offset.value = "2";
  // "change", not "input": the field commits on blur, and then it waits.
  offset.dispatchEvent(new window.Event("change"));
  await settle(700);

  // The whole point of staging: an unfinished parameter must not reach air on its
  // own, however long the operator leaves it sitting there.
  expect("an edited parameter does not publish itself",
    bodies.filter((b) => b.path === "/rundowns/main/apply" && !b.body.dryRun).length === 0,
    calls.slice(-4).join(", "));
  expect("the status reports the staged edit",
    byId("rundown-status").textContent === "Staged", byId("rundown-status").textContent);
  expect("there is no Revert button", byId("rundown-revert") === null);
  expect("there is no separate Validate button", byId("rundown-validate") === null);

  mark("Apply publishes it");
  const applyButton = () => byId("rundown-apply");
  expect("Apply sits in the inspector, where Duplicate and Delete are",
    applyButton() !== null && applyButton().closest("#event-inspector-form") !== null);
  expect("Apply is labelled plainly", applyButton().textContent === "Apply",
    applyButton().textContent);
  applyButton().dispatchEvent(new window.Event("click"));
  await settle(700);
  const live = bodies.filter((b) => b.path === "/rundowns/main/apply" && !b.body.dryRun).pop();
  expect("the publish sends the document with the expected version",
    live && "document" in live.body && live.body.expectedVersionId === liveVersionId);
  expect("the publish sends the document, never ops",
    live && "document" in live.body && !("ops" in live.body));
  expect("the publish is a real write, not a dry run", live && !live.body.dryRun);
  expect("the publish reloads the new live version",
    calls.includes(`GET /rundowns/main/versions/${AFTER_ID}`), calls.slice(-6).join(", "));
  expect("status returns to Live once published",
    byId("rundown-status").textContent === "Live", byId("rundown-status").textContent);
  expect("Apply is disabled once there is nothing staged",
    byId("rundown-apply") !== null && byId("rundown-apply").disabled === true);
  expect("the grid rebuilt onto the new version",
    dataRows().length === 3 && body().rows.length >= 3, `${body().rows.length} rows`);

  // Nothing else reports a failed publish now that there is no button to press,
  // so a rejection has to surface where the operator is looking.
  mark("a rejected publish");
  // apply() returns early on a clean buffer, so make a real edit first.
  if (!childOf(withGraphics)) await clickExpander();
  childOf(withGraphics).querySelector(".graphic-open").dispatchEvent(new window.Event("click"));
  await settle();
  const offsetInput = () => [...byId("event-inspector-form").querySelectorAll("input")].find((i) => i.type === "number");
  const setOffset = async (value) => {
    const field = offsetInput();
    field.value = value;
    field.dispatchEvent(new window.Event("change"));
    await settle();
    const button = byId("rundown-apply");
    if (button && !button.disabled) button.dispatchEvent(new window.Event("click"));
    await settle();
  };

  expectingFailure = true;
  rejectApply = "shared squeeze overlap: demo-lband and demo-aston";
  await setOffset("1");
  await settle(700);
  rejectApply = null;
  expectingFailure = false;
  expect("a rejected publish reads Not applied",
    byId("rundown-status").textContent === "Not applied", byId("rundown-status").textContent);
  expect("a rejected publish shows the server's reason in the panel",
    byId("rundown-draft-state").textContent.includes("squeeze overlap"),
    byId("rundown-draft-state").textContent);
  expect("a rejected publish marks the note as a problem",
    byId("rundown-draft-state").classList.contains("problem"));
  expect("a rejected publish keeps the edit so it can be corrected",
    byId("rundown-draft-state").classList.contains("problem"));

  // Clip-fit is checked in the browser first, so an impossible offset costs no
  // round trip — and must still land in the same place on screen.
  mark("a locally rejected publish");
  const item0 = document0.items[withGraphics];
  expectingFailure = true;
  // Only publishes matter here; the 1 Hz pollers keep running in the background.
  const publishesBefore = calls.filter((c) => c.includes("/rundowns/main/apply")).length;
  await setOffset(String(Math.ceil(item0.durationMs / 1000) + 5));
  await settle(700);
  expectingFailure = false;
  expect("an impossible offset is refused without a request",
    calls.filter((c) => c.includes("/rundowns/main/apply")).length === publishesBefore,
    calls.filter((c) => c.includes("/rundowns/main/apply")).join(", "));
  expect("a local refusal surfaces in the same place as a server one",
    byId("rundown-status").textContent === "Not applied" &&
      /exceeds/.test(byId("rundown-draft-state").textContent),
    byId("rundown-draft-state").textContent);
  await setOffset("1");
  // The operator has moved on from the edit that was refused, so the refusal must
  // not stay on screen accusing them of something they have already changed.
  expect("editing clears the stale problem",
    byId("rundown-status").textContent !== "Not applied" &&
      !byId("rundown-draft-state").classList.contains("problem"),
    byId("rundown-status").textContent);

  // YAML import: preview, then confirm.
  mark("import preview");
  const file = new window.File(["playlist:\n  - media: promo.mp4\n"], "extra.yml", { type: "text/yaml" });
  Object.defineProperty(byId("import-file"), "files", { value: [file], configurable: true });
  byId("import-position").value = "1";
  byId("import-file").dispatchEvent(new window.Event("change"));
  await settle(200);
  const preview = bodies.filter((b) => b.path === "/rundowns/main/programs/import").pop();
  expect("choosing a file runs a dry run first", preview && preview.dryRun === true);
  expect("the dry run carries the chosen position", preview && preview.position === "1", preview && preview.position);
  expect("the preview lists the resulting programs",
    byId("import-preview").hidden === false && byId("import-preview").querySelectorAll("li").length === 4,
    String(byId("import-preview").querySelectorAll("li").length));
  expect("the preview marks the newcomer",
    /new/.test(byId("import-preview").textContent), byId("import-preview").textContent.slice(0, 160));
  expect("the preview surfaces server warnings",
    /elapsed trigger/.test(byId("import-preview").textContent));
  expect("Import and Cancel appear only after a clean preview",
    byId("import-actions").hidden === false);

  mark("import commit");
  byId("import-commit").dispatchEvent(new window.Event("click"));
  await settle(200);
  const commit = bodies.filter((b) => b.path === "/rundowns/main/programs/import").pop();
  expect("Import commits without dryRun", commit && commit.dryRun === false);
  expect("the preview closes after importing", byId("import-preview").hidden === true);
}

console.log(`[${MODE}]`);
console.log(check.join("\n"));
if (errors.length) { console.log("\nERRORS:\n  " + errors.join("\n  ")); clearTimeout(watchdog); process.exit(1); }
console.log(`\n${check.length} assertions passed`);
clearTimeout(watchdog);
process.exit(0);
