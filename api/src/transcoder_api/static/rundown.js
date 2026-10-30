/* Program schedule, rendered as the Playlist rundown grid.

   The queue and the schedule used to live in two panels: one showed content with
   no time, the other showed time with no content. They are one table now. The
   queue snapshot is the spine — it is what actually plays, in playout order —
   and the schedule document is joined onto it by stable item ID.

   Structured JSON only: YAML remains server-owned. Every edit publishes itself:
   a committed field is coalesced briefly and then goes on air as one atomic
   validated apply, so there is no draft here or on the server, and nothing for
   an operator to remember to press. A rejected publish reloads the live version
   rather than leaving an edit showing that never aired. */
(function () {
  "use strict";
  const byId = (name) => document.getElementById(name);
  const lanes = ["logo", "lband", "bug", "aston"];
  const state = {
    enabled: false, doc: null, selected: null, dirty: false, busy: false,
    problem: null, editRevision: 0, baseline: null,
    bootstrap: null, liveVersionId: null,
    queue: null, clock: null, skewMs: 0, clockFetchedAt: 0,
    expanded: new Set(), autoExpanded: false, signature: null, liveCells: null,
    importFile: null, importBusy: false,
  };
  // The channel file is reapplied on every container start, so whether an edit
  // outlives a restart depends entirely on writeback. Only the second case is
  // worth an operator's attention, so only that one is shown.
  function bootstrapNotice() { const info = state.bootstrap; if (!info || !info.bootstrapManaged) return ""; if (info.bootstrapWriteback) return ""; return `This channel is defined by ${info.bootstrapFile}, and writeback is off. Anything changed here is replaced by that file the next time the container restarts.`; }
  // A banner with nothing in it is still a banner: it takes a strip of the panel
  // and reads as though something is wrong. Show it only when there is something
  // to say.
  function renderBootstrapBanner() {
    const banner = byId("rundown-bootstrap"); const info = state.bootstrap;
    if (!info || !info.bootstrapManaged) { banner.hidden = true; return; }
    if (info.bootstrapState === "failed") {
      banner.textContent = `${info.bootstrapFile} was rejected at startup${info.bootstrapError ? ` (${info.bootstrapError})` : ""}. The channel is running on its previously saved schedule.`;
      banner.style.color = "#aa1f2d"; banner.hidden = false; return;
    }
    banner.style.color = "";
    const warnings = info.bootstrapWarnings || [];
    const text = bootstrapNotice() + (warnings.length ? ` Skipped at startup: ${warnings.join("; ")}` : "");
    banner.textContent = text.trim();
    banner.hidden = banner.textContent === "";
  }
  const log = (message, level) => (window.operatorLog || console.log)(message, level);

  async function api(path, options) {
    const response = await fetch(path, options);
    const body = (response.headers.get("content-type") || "").includes("json") ? await response.json() : await response.text();
    if (!response.ok) { const error = new Error((body && (body.detail || body.message)) || response.statusText); error.status = response.status; throw error; }
    return body;
  }
  function seconds(ms) { if (ms === null || ms === undefined) return ""; return (ms / 1000).toFixed(2).replace(/\.00$/, "").replace(/(\.\d)0$/, "$1"); }
  function millis(value, allowBlank) {
    const text = String(value).trim();
    if (!text && allowBlank) return null;
    if (!/^\d+(?:\.\d{1,2})?$/.test(text)) throw new Error("Enter seconds with at most two decimal places.");
    const result = Math.round(Number(text) * 1000);
    if (result < 0 || (!allowBlank && result === 0)) throw new Error("Enter a value greater than zero.");
    return result;
  }
  // Untethered graphics are entered as elapsed time from the moment this schedule
  // went on air - the same number the Start column shows - rather than as a wall
  // clock, so there is no date, no zone and no DST for an operator to get wrong.
  // Hours are deliberately not capped at 24: the elapsed count keeps rising across
  // loop cycles. Values are held to the hundredth of a second the schedule stores.
  function elapsedMs(text) {
    const match = /^(\d{1,3}):([0-5]\d):([0-5]\d)(?:\.(\d{1,3}))?$/.exec(String(text ?? "").trim());
    if (!match) return null;
    const total = ((Number(match[1]) * 60 + Number(match[2])) * 60 + Number(match[3])) * 1000
      + Number(((match[4] || "") + "000").slice(0, 3));
    return Math.round(total / 10) * 10;
  }
  function elapsedText(ms) {
    if (!Number.isFinite(ms) || ms < 0) return "";
    const rounded = Math.round(ms / 10) * 10;
    const pad = (value, width) => String(value).padStart(width, "0");
    const centis = (rounded % 1000) / 10;
    return `${pad(Math.floor(rounded / 3600000), 2)}:${pad(Math.floor(rounded / 60000) % 60, 2)}:${pad(Math.floor(rounded / 1000) % 60, 2)}${centis ? `.${pad(centis, 2)}` : ""}`;
  }
  const format = () => window.operatorFormat || {};
  const mmss = (ms) => (format().mmss ? format().mmss(ms) : "—");
  const bytes = (value) => (format().bytes ? format().bytes(value) : "");
  // The server sends its own clock so the browser can correct for skew; without
  // it every projected row time is wrong by the difference between the two.
  function now() { return Date.now() + state.skewMs; }
  function zone() { return state.doc?.rundown?.timezone || "UTC"; }
  function parts(value) {
    const date = new Date(value);
    const values = {};
    new Intl.DateTimeFormat("en-CA", { timeZone: zone(), year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23", timeZoneName: "short" }).formatToParts(date).forEach(part => { values[part.type] = part.value; });
    return { date: `${values.year}-${values.month}-${values.day}`, time: `${values.hour}:${values.minute}:${values.second}.${String(date.getUTCMilliseconds()).padStart(3, "0").slice(0, 2)}`, abbreviation: values.timeZoneName || zone(), hour: Number(values.hour), minute: Number(values.minute), second: Number(values.second), ms: date.getUTCMilliseconds() };
  }
  const clockAt = (ms) => (Number.isFinite(ms) ? parts(new Date(ms).toISOString()).time : "—");
  // The Start column counts from the moment this channel started playing out rather
  // than showing a wall clock, so an operator reads a program's position in the
  // playout directly. syncedAt is the first observed playback of this process run -
  // not of the active version - so publishing does not move it, and a value entered
  // as elapsed still means the same moment after an Apply. It keeps rising across
  // loop cycles: the second pass through a ten minute loop starts at 00:10:00.
  function anchorMs() {
    const value = state.clock?.syncedAt ? Date.parse(state.clock.syncedAt) : NaN;
    return Number.isFinite(value) ? value : null;
  }
  function elapsedAt(ms) {
    const anchor = anchorMs();
    if (!Number.isFinite(ms) || anchor === null) return "—";
    const total = Math.max(0, Math.round((ms - anchor) / 1000));
    const pad = (value) => String(value).padStart(2, "0");
    return `${pad(Math.floor(total / 3600))}:${pad(Math.floor(total / 60) % 60)}:${pad(total % 60)}`;
  }
  function rows() { const result = []; (state.doc?.items || []).forEach(item => (item.graphics || []).forEach(event => result.push({ type: "event", event, item }))); (state.doc?.scheduledGraphics || []).forEach(event => result.push({ type: "event", event, item: null })); return result; }
  function selected() { if (!state.selected) return null; if (state.selected.type === "program") return { type: "program", item: state.doc.items.find(item => item.key === state.selected.key) }; return rows().find(row => row.event.key === state.selected.key) || null; }
  function select(value) { state.selected = value; render(); }
  // Edits publish themselves, so there is no unapplied resting state to report -
  // only in-flight and rejected. A rejection has to stay visible here rather than
  // only in the activity log, because nothing else now tells the operator that
  // what they typed is not on air.
  function renderStatus() {
    const note = byId("rundown-draft-state");
    const staged = state.dirty ? state.editRevision : 0;
    byId("rundown-status").textContent = state.busy ? "Publishing…" : state.problem ? "Not applied" : staged ? "Staged" : "Live";
    note.textContent = state.busy ? "Publishing"
      : state.problem ? state.problem
      : staged ? `${staged} unapplied ${staged === 1 ? "change" : "changes"} · Apply in the inspector`
      : "On air";
    note.classList.toggle("problem", Boolean(state.problem));
    refreshApplyButton();
  }

  // Authoring is staged. Adding a graphic and editing its parameters change this
  // tab only, so a half-finished event never reaches air between two fields - Apply
  // in the inspector publishes them. Deleting stays immediate; see removeEvent.
  function markDirty(rebuild) {
    state.problem = null;
    // Adding a graphic and then discarding it leaves the document exactly as it is
    // on air. Comparing against the last published copy keeps that from reading as
    // a staged change the operator has to clear.
    if (state.baseline && JSON.stringify(state.doc) === state.baseline) { state.dirty = false; state.editRevision = 0; }
    else { state.dirty = true; state.editRevision += 1; }
    renderStatus(); if (rebuild) render();
  }
  function markPublished() { state.dirty = false; state.editRevision = 0; state.baseline = JSON.stringify(state.doc); }
  // A publish always sends the whole document, so there is one Apply for the whole
  // schedule even though it is rendered beside the selected graphic: it commits
  // every staged change, not only that one.
  function applyButton() {
    const button = makeButton("Apply", () => apply(), "primary");
    button.id = "rundown-apply";
    button.title = "Publish every staged change and put it on air.";
    refreshApplyButton(button);
    return button;
  }
  // Most edits do not rebuild the inspector - that would fight the form the operator
  // is working in - so the button they will press has to be updated where it stands.
  function refreshApplyButton(button) {
    const target = button || byId("rundown-apply");
    if (!target) return;
    target.disabled = state.busy || !state.dirty;
  }
  function localValidationError() {
    if (state.problem) return state.problem;
    for (const item of state.doc?.items || []) {
      for (const event of item.graphics || []) {
        if (!Number.isFinite(item.durationMs)) return `Program ${item.displayName || item.key} has unknown duration.`;
        if (!Number.isFinite(event.durationMs)) return `Graphic ${event.key} requires a finite duration.`;
        if (!Number.isFinite(event.trigger.offsetMs) || event.trigger.offsetMs < 0) return `Graphic ${event.key} has an invalid offset.`;
        if (!Number.isFinite(event.fadeMs) || event.fadeMs < 0) return `Graphic ${event.key} has an invalid fade.`;
        const margin = event.kind === "lband" || event.kind === "aston" ? 1200 : 0;
        if ((event.trigger.offsetMs || 0) + event.durationMs + margin > item.durationMs) return `Graphic ${event.key} exceeds ${item.displayName || item.key}; shorten its duration or move it earlier.`;
      }
    }
    return null;
  }
  function asset(assetId) { return (window.__graphicsCatalog || []).find(entry => entry.id === assetId); }
  function assetLabel(assetId) { return asset(assetId)?.label || assetId; }
  function laneLabel(kind) { return kind === "lband" ? "L-band" : String(kind || "graphic").toUpperCase(); }
  function defaultMode(entry) {
    if (entry?.defaultMode && entry.supportedModes.includes(entry.defaultMode)) return entry.defaultMode;
    if ((entry?.kind === "logo" || entry?.kind === "bug") && entry.supportedModes.includes("fade")) return "fade";
    if ((entry?.kind === "lband" || entry?.kind === "aston") && entry.supportedModes.includes("reveal")) return "reveal";
    return entry?.supportedModes?.[0] || "snap";
  }
  async function loadCatalog() { try { window.__graphicsCatalog = (await api("/graphics/catalog")).assets || []; } catch (_) { window.__graphicsCatalog = []; } }
  function makeButton(label, handler, className) { const button = document.createElement("button"); button.type = "button"; button.textContent = label; if (className) button.className = className; button.disabled = state.busy; button.addEventListener("click", handler); return button; }
  function optionSelect(options, value) { const element = document.createElement("select"); options.forEach(([id, label]) => { const option = document.createElement("option"); option.value = id; option.textContent = label; option.selected = id === value; element.append(option); }); return element; }
  function input(value, type) { const element = document.createElement("input"); element.type = type || "text"; element.value = value ?? ""; return element; }
  function formField(label, control, hint) { const wrap = document.createElement("label"); wrap.className = "inspector-field"; const title = document.createElement("span"); title.textContent = label; wrap.append(title, control); if (hint) { const note = document.createElement("small"); note.textContent = hint; wrap.append(note); } return wrap; }
  function group(title) { const element = document.createElement("fieldset"); element.className = "inspector-group"; const legend = document.createElement("legend"); legend.textContent = title; element.append(legend); return element; }

  /* ---------------------------------------------------------------- the grid */

  // The queue is the spine: it is what plays, in the order it plays, and it is
  // what insertAfter/moveItem index into. The schedule document is joined onto
  // it by stable item ID — _queue_projection creates each playlist row with
  // id = stable_item_id, so the join is exact.
  // operator.js owns the snapshot and pushes it here, but its boot and this
  // module's init race; read through to the owner so the first paint is not a
  // table of "not queued" rows.
  function queueSnapshot() { return state.queue || (window.operatorQueue && window.operatorQueue.snapshot) || null; }
  function buildRows() {
    const snapshot = queueSnapshot();
    const byStableId = new Map((state.doc?.items || []).map(item => [item.stableItemId, item]));
    const result = (snapshot?.items || []).map((queueItem, index) => ({
      id: queueItem.id, queueItem, index, doc: byStableId.get(queueItem.id) || null,
    }));
    const claimed = new Set(result.map(row => row.doc).filter(Boolean));
    // A scheduled item absent from the queue would otherwise vanish from the UI.
    (state.doc?.items || []).forEach(item => {
      if (!claimed.has(item)) result.push({ id: `doc:${item.key}`, queueItem: null, index: -1, doc: item });
    });
    return result;
  }
  // timelineStartMs is a cumulative sum that treats an unprobed clip as zero, so
  // every position after such a clip is understated. Show nothing rather than a
  // confident wrong time.
  function firstUnknownIndex() {
    const items = state.doc?.items || [];
    const index = items.findIndex(item => !Number.isFinite(item.durationMs));
    return index < 0 ? Infinity : index;
  }
  // The on-air row's time is observed fact; every other row is projected from it.
  // Re-anchoring on the current item at each transition is what keeps the grid
  // from drifting, which projecting forever from a fixed epoch cannot avoid.
  function projectStarts(gridRows) {
    const blank = gridRows.map(() => null);
    const clock = state.clock;
    if (!clock || !clock.currentStartedAt || !state.doc) return blank;
    const anchorIndex = gridRows.findIndex(row => row.queueItem && row.queueItem.id === clock.currentItemId);
    if (anchorIndex < 0 || !gridRows[anchorIndex].doc) return blank;
    const anchor = gridRows[anchorIndex].doc;
    if (!Number.isFinite(anchor.timelineStartMs)) return blank;
    const items = state.doc.items || [];
    const unknown = firstUnknownIndex();
    const loop = clock.loopDurationMs || 0;
    const epoch = Date.parse(clock.currentStartedAt) - anchor.timelineStartMs;
    return gridRows.map((row, index) => {
      if (!row.doc || !Number.isFinite(row.doc.timelineStartMs)) return null;
      if (items.indexOf(row.doc) > unknown) return null;
      // Rows above the on-air item already played this cycle; show the next airing.
      return epoch + row.doc.timelineStartMs + (index < anchorIndex && loop > 0 ? loop : 0);
    });
  }
  function toggleExpand(key) {
    if (state.expanded.has(key)) state.expanded.delete(key); else state.expanded.add(key);
    renderGrid();
  }
  function gridSignature() {
    const snapshot = queueSnapshot();
    return JSON.stringify([
      state.enabled, state.liveVersionId, state.editRevision, state.busy,
      [...state.expanded].sort(), state.selected,
      (state.doc?.items || []).map(item => [item.key, item.durationMs, (item.graphics || []).map(event => event.key)]),
      (state.doc?.scheduledGraphics || []).map(event => event.key),
      (snapshot?.items || []).map(item => [item.id, item.state, item.error, item.media.name]),
      snapshot ? [snapshot.currentItemId, snapshot.desiredRevision, snapshot.appliedRevision] : null,
      window.operatorQueue ? window.operatorQueue.selectedMediaId : null,
    ]);
  }
  // Open the on-air row once so the first paint already shows what rides on it.
  // Only when it has graphics: every row can be expanded now, but opening one that
  // holds nothing would just push the grid down for no information.
  function autoExpandOnAir() {
    if (state.autoExpanded || !state.doc) return;
    const snapshot = queueSnapshot();
    if (!snapshot || !snapshot.currentItemId) return;
    state.autoExpanded = true;
    const item = (state.doc.items || []).find(entry => entry.stableItemId === snapshot.currentItemId);
    if (item && (item.graphics || []).length) state.expanded.add(snapshot.currentItemId);
  }
  function renderGrid() {
    if (!state.enabled) return;
    autoExpandOnAir();
    const signature = gridSignature();
    if (signature !== state.signature) { state.signature = signature; renderGridStructure(); }
    refreshGridLive();
    renderClockBadge();
  }
  function cell(row, className, text) {
    const element = row.insertCell();
    if (className) element.className = className;
    if (text !== undefined) element.textContent = text;
    return element;
  }
  function tag(text, className) {
    const element = document.createElement("span");
    element.className = `tag ${className || ""}`.trim();
    element.textContent = text;
    return element;
  }
  function graphicChildRow(event, item, cells) {
    const line = document.createElement("div");
    line.className = `graphic-row lane-${event.kind || "logo"}${state.selected?.type === "event" && state.selected.key === event.key ? " selected" : ""}`;
    const open = document.createElement("button");
    open.type = "button";
    open.className = "graphic-open";
    const timing = event.trigger?.type === "wall-clock"
      ? `at +${event.trigger.startsAt ? elapsedAt(Date.parse(event.trigger.startsAt)) : "—"}`
      : `@ +${seconds(event.trigger?.offsetMs || 0)}s`;
    open.textContent = [
      laneLabel(event.kind), assetLabel(event.assetId), event.mode, timing,
      `${seconds(event.durationMs) || "hold"}s`, `fade ${seconds(event.fadeMs || 0)}s`,
    ].join(" · ");
    open.addEventListener("click", () => select({ type: "event", key: event.key }));
    const remove = makeButton("×", () => removeEvent({ event, item }), "danger tiny");
    remove.title = event.definitionId ? `Delete ${event.key}` : `Discard ${event.key}, which has not been applied`;
    line.append(open, remove);
    return line;
  }
  function renderGridStructure() {
    const body = byId("queue-body");
    const cells = { starts: new Map(), notes: new Map(), states: new Map(), remains: new Map() };
    body.replaceChildren();
    const snapshot = queueSnapshot();
    const gridRows = buildRows();
    const queue = window.operatorQueue;
    const applying = queue ? queue.isApplying() : true;

    if (!gridRows.length) {
      const row = body.insertRow();
      const empty = row.insertCell();
      empty.colSpan = 10;
      empty.className = "empty";
      empty.textContent = snapshot ? "No clips are configured" : "Playlist state unavailable";
      state.liveCells = cells;
      return;
    }

    gridRows.forEach((entry, index) => {
      const item = entry.doc;
      const queueItem = entry.queueItem;
      const onAir = Boolean(queueItem) && (queueItem.id === snapshot?.currentItemId || queueItem.state === "on_air");
      const graphics = item?.graphics || [];
      // Every program gets an expander, including one carrying no graphics: the
      // panel is how an operator adds the first one, so gating it on there already
      // being a graphic left no way in.
      const expanded = state.expanded.has(entry.id);
      const row = body.insertRow();
      row.className = [onAir ? "onair" : "", expanded ? "expanded" : ""].filter(Boolean).join(" ");

      cell(row, "col-num", queueItem ? String(entry.index + 1) : "—");

      const expander = cell(row, "col-exp");
      if (item) {
        const toggle = makeButton(expanded ? "▼" : "▸", () => toggleExpand(entry.id), "expander");
        toggle.disabled = false;
        toggle.title = expanded
          ? "Collapse"
          : graphics.length ? "Show scheduled graphics" : "Add a graphic to this program";
        toggle.setAttribute("aria-expanded", expanded ? "true" : "false");
        expander.append(toggle);
      }

      const start = cell(row, "cell-start");
      const clockText = document.createElement("div");
      clockText.className = "start-clock";
      const note = document.createElement("small");
      note.className = "start-note";
      start.append(clockText, note);
      cells.starts.set(entry.id, clockText);
      cells.notes.set(entry.id, note);

      cell(row, "cell-loop", item && Number.isFinite(item.timelineStartMs) ? `+${mmss(item.timelineStartMs)}` : "—");

      const name = row.insertCell();
      const title = document.createElement("button");
      title.type = "button";
      title.className = `clip-name${state.selected?.type === "program" && item && state.selected.key === item.key ? " selected" : ""}`;
      title.textContent = item ? (item.displayName || item.key) : queueItem.media.name;
      if (item) title.addEventListener("click", () => select({ type: "program", key: item.key }));
      else title.disabled = true;
      const detail = document.createElement("div");
      detail.className = "sid";
      detail.textContent = item ? (item.relativePath || item.key) : `${bytes(queueItem.media.sizeBytes)} · ${queueItem.id}`;
      name.append(title, detail);
      if (!item && queueItem) {
        // The one-cycle bridge _queue_projection retains: playing, but not part
        // of the schedule, so it has no time, no duration and no graphics.
        name.append(document.createTextNode(" "), tag("bridge", "bridge"));
      }

      cell(row, "col-type", "Video");
      cell(row, "col-dur", item && Number.isFinite(item.durationMs) ? mmss(item.durationMs) : "—");

      const gfx = cell(row, "col-gfx");
      if (graphics.length) {
        const count = makeButton(`${graphics.length} ▾`, () => toggleExpand(entry.id), "gfx-count");
        gfx.append(count);
      } else if (item) {
        gfx.className = "col-gfx dim";
        gfx.append(makeButton("+ ▾", () => toggleExpand(entry.id), "gfx-count"));
      } else { gfx.className = "col-gfx dim"; gfx.textContent = "—"; }

      const status = cell(row, "cell-state");
      if (queueItem && queue) status.append(queue.itemStateTag(queueItem));
      else status.append(tag("not queued", "error"));
      const remain = document.createElement("small");
      remain.className = "cell-remain";
      status.append(remain);
      cells.remains.set(entry.id, remain);
      if (queueItem && queueItem.error) {
        const problem = document.createElement("div");
        problem.className = "sid";
        problem.textContent = queueItem.error;
        status.append(problem);
      }

      const actions = cell(row, "col-act");
      if (queueItem && queue) actions.append(...queue.queueActions(queueItem, entry.index, applying, onAir));

      // A bridge row has no schedule entry, so there is nothing to attach a graphic
      // to and no panel to open.
      if (expanded && item) {
        const child = body.insertRow();
        child.className = "child-row";
        const holder = child.insertCell();
        holder.colSpan = 10;
        const list = document.createElement("div");
        list.className = "graphic-list";
        graphics.forEach(event => list.append(graphicChildRow(event, item, cells)));
        const addEvent = makeButton("+ Add graphic", () => add(item.key), "add-graphic");
        list.append(addEvent);
        holder.append(list);
      }
    });

    // Wall-clock graphics belong to no program. Without a home here they would be
    // unreachable, since the inspector can still move a graphic onto one.
    const scheduled = state.doc?.scheduledGraphics || [];
    if (scheduled.length) {
      const expanded = state.expanded.has("wall-clock");
      const row = body.insertRow();
      row.className = "group-row";
      const holder = row.insertCell();
      holder.colSpan = 10;
      const toggle = makeButton(`${expanded ? "▼" : "▸"} Timed from start · not tied to a program · ${scheduled.length}`, () => toggleExpand("wall-clock"), "group-toggle");
      holder.append(toggle);
      if (expanded) {
        const child = body.insertRow();
        child.className = "child-row";
        const cellHolder = child.insertCell();
        cellHolder.colSpan = 10;
        const list = document.createElement("div");
        list.className = "graphic-list";
        scheduled.forEach(event => list.append(graphicChildRow(event, null, cells)));
        cellHolder.append(list);
      }
    }
    state.liveCells = cells;
  }
  // Runs every second. Writes only the cells whose value is a function of
  // wall-clock time; rebuilding the table at 1 Hz would flicker it and collapse
  // every open row.
  function refreshGridLive() {
    const cells = state.liveCells;
    if (!cells) return;
    const gridRows = buildRows();
    const starts = projectStarts(gridRows);
    const currentId = state.clock?.currentItemId;
    gridRows.forEach((entry, index) => {
      const start = starts[index];
      const clockText = cells.starts.get(entry.id);
      const note = cells.notes.get(entry.id);
      const remain = cells.remains.get(entry.id);
      const observed = Boolean(entry.queueItem) && entry.queueItem.id === currentId;
      if (clockText) {
        clockText.textContent = start === null ? "—" : elapsedAt(start);
        // The wall clock is still what a playout log or a broadcaster asks for, so
        // keep it reachable rather than dropping it entirely.
        clockText.title = start === null ? "" : `${clockAt(start)} ${zone()}`;
      }
      if (note) {
        note.textContent = start === null ? (state.clock?.currentStartedAt ? "unknown duration upstream" : "not yet observed")
          : observed ? "observed" : "projected";
        note.className = `start-note${observed ? " observed" : ""}`;
      }
      if (remain) {
        const total = entry.doc?.durationMs;
        remain.textContent = observed && start !== null && Number.isFinite(total)
          ? `${Math.max(0, (start + total - now()) / 1000).toFixed(1)}s left` : "";
      }
    });
    // The badge carries a running count now, so it ticks with the rest of the row.
    renderClockBadge();
  }
  function renderClockBadge() {
    const badge = byId("rundown-clock");
    const clock = state.clock;
    if (!clock || !clock.syncedAt) { badge.hidden = true; return; }
    badge.hidden = false;
    // "synced" named the field, not the fact. These four numbers are what the Start
    // column and every elapsed entry are measured against, so they say what they mean:
    // when playout started, how long it has been running, how long one pass through
    // the playlist takes, and which pass is on air now.
    const loop = clock.loopDurationMs ? `loop ${mmss(clock.loopDurationMs)}` : "loop length unknown";
    badge.textContent = `start ${parts(clock.syncedAt).time} · running ${elapsedAt(now())} · ${loop} · cycle ${clock.cycle}`;
    badge.title = `Playout started at ${parts(clock.syncedAt).date} ${parts(clock.syncedAt).time} ${parts(clock.syncedAt).abbreviation}. One pass through the playlist takes ${mmss(clock.loopDurationMs)}; pass ${clock.cycle} is on air.`;
  }
  // The current item only changes at a transition, so this does not need a 1 Hz
  // request; the countdown ticks locally against the corrected clock.
  async function loadClock(force) {
    const snapshot = queueSnapshot();
    const changed = state.clock && snapshot && state.clock.currentItemId !== snapshot.currentItemId;
    if (!force && !changed && Date.now() - state.clockFetchedAt < 30000) return;
    try {
      const clock = await api("/rundowns/main/clock");
      state.clockFetchedAt = Date.now();
      state.skewMs = Date.parse(clock.serverNow) - Date.now();
      state.clock = clock;
    } catch (_) { /* The grid falls back to em dashes; the hint reports the outage. */ }
  }
  // Called by operator.js on every queue poll: it owns the snapshot, we own the table.
  function onQueue(snapshot) {
    state.queue = snapshot;
    if (!state.enabled) return;
    loadClock(false);
    renderGrid();
  }
  // A queue edit now publishes a new schedule version, so this tab's copy of the
  // document goes stale the moment one lands. Without a resync the removed item
  // survives here as a schedule row with no queue row behind it - exactly the
  // ghost row the write-through was meant to remove - and an inserted one shows
  // with no duration. Buffered edits win: reloading would silently discard them.
  async function resync() {
    if (!state.enabled || state.dirty || state.busy) return;
    try {
      await reloadCurrentDocument();
      render();
    } catch (_) {
      /* The queue snapshot is already rendered; a failed doc refresh must not
         blank the grid. The next successful resync repairs it. */
    }
  }
  window.rundownGrid = { onQueue, resync };

  /* ---------------------------------------------------------------- inspector */

  function programInspector(item) {
    const form = byId("event-inspector-form"); const technical = group("Program details");
    [["Program", item.displayName || item.key], ["Media path", item.relativePath || "—"], ["Queue position", String(item.position)], ["Duration", item.durationMs === null ? "Unknown — graphics cannot be item-relative" : `${seconds(item.durationMs)} sec`], ["Loop start", `${seconds(item.timelineStartMs)} sec`], ["Item key", item.key], ["Stable item ID", item.stableItemId], ["Media ID", item.mediaId]].forEach(([name, value]) => { const line = document.createElement("p"); const label = document.createElement("b"); label.textContent = `${name}: `; const text = document.createElement("span"); text.textContent = value; line.append(label, text); technical.append(line); });
    const actions = document.createElement("div"); actions.className = "inspector-actions"; actions.append(makeButton("+ Add graphic", () => add(item.key)), applyButton()); technical.append(actions);
    const note = document.createElement("p"); note.className = "inspector-note"; note.textContent = "Program order and membership are managed with the row actions and the insert strip."; technical.append(note); form.append(technical);
  }
  function resolvePendingNumbers() {
    for (const row of rows()) {
      const event = row.event;
      if (Object.hasOwn(event, "pendingOffset")) {
        event.trigger.offsetMs = millis(event.pendingOffset, false);
        delete event.pendingOffset;
      }
      if (Object.hasOwn(event, "pendingDuration")) {
        const persistent = (event.kind === "logo" || event.kind === "bug") && event.trigger.type === "wall-clock";
        event.durationMs = millis(event.pendingDuration, persistent);
        delete event.pendingDuration;
      }
      if (Object.hasOwn(event, "pendingFade")) {
        event.fadeMs = millis(event.pendingFade || "0", true) || 0;
        delete event.pendingFade;
      }
    }
  }
  function eventInspector(row) {
    const form = byId("event-inspector-form"); const event = row.event; const chosenAsset = asset(event.assetId);
    const eventGroup = group("Event"); const key = input(event.key); key.addEventListener("change", () => { event.key = key.value.trim(); state.selected = { type: "event", key: event.key }; markDirty(true); }); eventGroup.append(formField("Event key", key));
    const kind = optionSelect(lanes.map(value => [value, laneLabel(value)]), event.kind); kind.addEventListener("change", () => { event.kind = kind.value; const candidate = (window.__graphicsCatalog || []).find(entry => entry.kind === kind.value && entry.enabled); if (candidate) { event.assetId = candidate.id; event.mode = defaultMode(candidate); } markDirty(true); }); eventGroup.append(formField("Lane", kind));
    const assetOptions = (window.__graphicsCatalog || []).filter(entry => entry.kind === event.kind && entry.enabled).map(entry => [entry.id, entry.label || entry.id]); const assetSelect = optionSelect(assetOptions, event.assetId); assetSelect.addEventListener("change", () => { event.assetId = assetSelect.value; event.mode = defaultMode(asset(assetSelect.value)); markDirty(true); }); eventGroup.append(formField("Asset", assetSelect));
    if (chosenAsset?.thumbnail) { const preview = document.createElement("img"); preview.className = "inspector-preview"; preview.src = chosenAsset.thumbnail; preview.alt = chosenAsset.label || event.assetId; eventGroup.append(preview); } form.append(eventGroup);
    const timing = group("Timing"); const trigger = optionSelect([["item-start", "Program start"], ["wall-clock", "Time from start"]], event.trigger.type); trigger.addEventListener("change", () => moveTrigger(row, trigger.value)); timing.append(formField("Trigger", trigger));
    if (trigger.value === "item-start") { const programs = optionSelect((state.doc.items || []).map(item => [item.key, item.displayName || item.key]), row.item?.key); programs.addEventListener("change", () => moveItem(row, programs.value)); timing.append(formField("Program", programs)); const offset = input(event.pendingOffset ?? seconds(event.trigger.offsetMs || 0), "number"); offset.min = "0"; offset.step = "0.01"; offset.addEventListener("change", () => { event.pendingOffset = offset.value; markDirty(); }); timing.append(formField("Offset (sec)", offset, "0.01-second precision · goes on air when you leave the field")); }
    else {
      const anchor = anchorMs();
      const control = input(anchor === null || !event.trigger.startsAt ? "" : elapsedText(Date.parse(event.trigger.startsAt) - anchor));
      control.placeholder = "HH:MM:SS.ss";
      control.inputMode = "numeric";
      const summary = document.createElement("output");
      summary.className = "resolved-time";
      const show = (text, bad) => { summary.textContent = text; summary.classList.toggle("problem", Boolean(bad)); };
      // The elapsed value is what the operator works in; the wall clock it lands on
      // is shown beside it so nothing is hidden by the conversion.
      const describe = () => {
        const at = Date.parse(event.trigger.startsAt);
        if (!Number.isFinite(at)) { show("Not set.", true); return; }
        const shown = parts(new Date(at).toISOString());
        show(`${shown.date} ${shown.time} ${shown.abbreviation}`);
      };
      if (anchor === null) {
        // Elapsed time means nothing before there is a start to count from.
        control.disabled = true;
        show("This schedule is not on air yet, so there is no start to count from.", true);
      } else describe();
      control.addEventListener("change", () => {
        const offset = elapsedMs(control.value);
        if (offset === null) { show("Enter HH:MM:SS.ss counted from the start.", true); return; }
        const at = anchor + offset;
        if (at <= now()) { show(`Already passed - the schedule is at ${elapsedAt(now())} now.`, true); return; }
        if (at > now() + 14 * 86400000) { show("Beyond the 14-day scheduling limit.", true); return; }
        control.value = elapsedText(offset);
        event.trigger.startsAt = new Date(at).toISOString();
        markDirty();
        describe();
      });
      timing.append(formField("Time from start", control, anchor === null ? "Unavailable until the schedule is on air" : `HH:MM:SS.ss since this schedule went on air · now ${elapsedAt(now())}`));
      timing.append(formField("Resolved", summary, `Wall clock in ${zone()}.`));
      // The origin the field counts from, so the two numbers can be checked against
      // each other rather than taken on trust.
      const origin = document.createElement("output");
      origin.className = "resolved-time";
      origin.textContent = anchor === null ? "—" : (() => { const shown = parts(new Date(anchor).toISOString()); return `${shown.date} ${shown.time} ${shown.abbreviation}`; })();
      timing.append(formField("Schedule start", origin, "What Time from start counts from."));
    }
    const canPersist = event.kind === "logo" || event.kind === "bug"; const duration = input(event.pendingDuration ?? seconds(event.durationMs), "number"); duration.min = "0.01"; duration.step = "0.01"; duration.placeholder = canPersist && trigger.value === "wall-clock" ? "Persistent" : "Required"; duration.addEventListener("change", () => { event.pendingDuration = duration.value; markDirty(); }); timing.append(formField("Duration (sec)", duration, canPersist ? "Blank only for wall-clock Logo/Bug" : "Includes animation · goes on air when you leave the field"));
    const fade = input(event.pendingFade ?? seconds(event.fadeMs || 0), "number"); fade.min = "0"; fade.step = "0.01"; fade.addEventListener("change", () => { event.pendingFade = fade.value; markDirty(); }); timing.append(formField("Fade (sec)", fade, "Included in Duration · goes on air when you leave the field")); form.append(timing);
    const technical = group("Technical"); const modes = optionSelect((asset(event.assetId)?.supportedModes || []).map(value => [value, value]), event.mode); modes.addEventListener("change", () => { event.mode = modes.value; markDirty(); }); technical.append(formField("Animation", modes)); const info = document.createElement("p"); info.className = "inspector-note"; info.textContent = row.item?.durationMs ? `Clip-fit limit: ${seconds(row.item.durationMs)} sec. Remaining before restore: ${seconds(Math.max(0, row.item.durationMs - (event.trigger.offsetMs || 0)))} sec.` : "Wall-clock event; the 14-day scheduling limit applies."; technical.append(info); const actions = document.createElement("div"); actions.className = "inspector-actions"; actions.append(applyButton());
    // Duplicate copies a published definition and Delete takes one off air. A graphic
    // that has never been applied has neither, so Apply is its only action here; the
    // x on its row discards it.
    if (event.definitionId) actions.append(makeButton("Duplicate", () => duplicate(row)), makeButton("Delete event", () => removeEvent(row), "danger"));
    technical.append(actions); form.append(technical);
  }
  function renderInspector() { const form = byId("event-inspector-form"), empty = byId("inspector-empty"), heading = byId("inspector-heading"), row = selected(); form.replaceChildren(); if (!row || !row.item && row.type === "program") { form.hidden = true; empty.hidden = false; heading.textContent = "Properties"; empty.replaceChildren(document.createTextNode("Select a program or a graphics event.")); if (state.dirty) { const stranded = document.createElement("div"); stranded.className = "inspector-actions"; stranded.append(applyButton()); empty.append(stranded); } return; } empty.hidden = true; form.hidden = false; if (row.type === "program") { heading.textContent = "Program"; programInspector(row.item); } else { heading.textContent = "Graphics event"; eventInspector(row); } }
  function moveItem(row, itemKey) { const next = state.doc.items.find(item => item.key === itemKey); if (!next) return; row.item.graphics = row.item.graphics.filter(event => event !== row.event); row.event.trigger = { type: "item-start", offsetMs: 0 }; next.graphics.push(row.event); state.expanded.add(next.stableItemId); markDirty(true); }
  function moveTrigger(row, type) { state.problem = null; if (type === "wall-clock") { row.item.graphics = row.item.graphics.filter(event => event !== row.event); row.event.trigger = { type: "wall-clock", startsAt: new Date(Date.now() + 300000).toISOString() }; state.doc.scheduledGraphics.push(row.event); state.expanded.add("wall-clock"); } else { state.doc.scheduledGraphics = state.doc.scheduledGraphics.filter(event => event !== row.event); const first = state.doc.items[0]; row.event.trigger = { type: "item-start", offsetMs: 0 }; first.graphics.push(row.event); state.expanded.add(first.stableItemId); } markDirty(true); }
  function duplicate(row) { const copy = JSON.parse(JSON.stringify(row.event)); copy.key = `${copy.key}-copy`; delete copy.definitionId; if (row.item) row.item.graphics.push(copy); else state.doc.scheduledGraphics.push(copy); state.selected = { type: "event", key: copy.key }; markDirty(true); }
  // Deleting publishes itself, so this is the last chance to change your mind.
  // Deleting is the one authoring action that still publishes on its own, so an
  // operator can pull a graphic off air without first finishing an unrelated edit.
  // That publish carries the whole document, so it says so when others are staged.
  function removeEvent(row) {
    const staged = !row.event.definitionId;
    const message = staged
      ? `Discard ${row.event.key}? It was never applied, so nothing on air changes.`
      : state.dirty
        ? `Remove ${row.event.key}? This goes on air immediately, and applies your other staged changes with it.`
        : `Remove ${row.event.key}? This goes on air immediately.`;
    if (!confirm(message)) return;
    if (row.item) row.item.graphics = row.item.graphics.filter(event => event !== row.event);
    else state.doc.scheduledGraphics = state.doc.scheduledGraphics.filter(event => event !== row.event);
    state.selected = null; markDirty(true);
    if (!staged) apply();
  }
  function add(itemKey) {
    if (!state.doc?.items?.length) return;
    const selectedAsset = (window.__graphicsCatalog || []).find(entry => entry.kind === "lband" && entry.enabled) || (window.__graphicsCatalog || []).find(entry => entry.enabled);
    if (!selectedAsset) return;
    const margin = selectedAsset.kind === "lband" || selectedAsset.kind === "aston" ? 1200 : 0;
    const requested = itemKey ? state.doc.items.find(item => item.key === itemKey) : null;
    const target = requested && (requested.durationMs || 0) > margin + 250 ? requested : state.doc.items.find(item => (item.durationMs || 0) > margin + 250);
    if (!target) { log("No program is long enough for this graphic and its restoration margin.", "error"); return; }
    const maximum = target.durationMs - margin;
    const duration = Math.max(250, Math.min(selectedAsset.defaultDurationMs || 5000, maximum));
    const event = { key: `graphic-${Date.now()}`, assetId: selectedAsset.id, kind: selectedAsset.kind, mode: defaultMode(selectedAsset), durationMs: duration, fadeMs: selectedAsset.defaultAnimationMs || 0, trigger: { type: "item-start", offsetMs: 0 }, source: "rundown" };
    target.graphics.push(event);
    state.expanded.add(target.stableItemId);
    state.selected = { type: "event", key: event.key };
    markDirty(true);
  }

  /* -------------------------------------------------------------- write path */

  function setBusy(value) { state.busy = value; render(); }
  // One atomic call: validated, published, and mirrored into the channel file.
  // There is no saved-but-not-live state to leave behind.
  async function apply() { if (!state.dirty || state.busy) return; setBusy(true); try { resolvePendingNumbers(); const problem = localValidationError(); if (problem) throw new Error(problem); const result = await api("/rundowns/main/apply", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ document: state.doc, expectedVersionId: state.liveVersionId }) }); state.liveVersionId = result.version.id; state.doc = await api(`/rundowns/main/versions/${result.version.id}`); markPublished(); state.problem = null; await loadClock(true); (result.warnings || []).forEach(warning => log(warning, "info")); const file = result.channelFile || {}; if (file.state === "failed") log(`Applied and on air, but ${file.path} could not be rewritten (${file.error}). This change will be lost on the next restart.`, "error"); else log(file.state === "written" ? `Applied; playlist reconciliation is running, and ${file.path} was rewritten.` : "Applied; playlist reconciliation is running.", "ok"); } catch (error) { if (error.status === 409) { log(`Could not apply: ${error.message} Reloading what is on air.`, "error"); try { await reloadCurrentDocument(); render(); } catch (_) { /* Keep the original conflict visible. */ } return; } state.problem = error.message; log(`Could not apply schedule: ${error.message}`, "error"); } finally { setBusy(false); } }
  // The live version is always the editing base. It is also the recovery path:
  // a rejected publish reloads it so the tab cannot keep showing an edit that
  // never reached air.
  async function reloadCurrentDocument() { const detail = await api("/rundowns/main"); state.liveVersionId = detail.activeVersionId; state.doc = await api(`/rundowns/main/versions/${detail.activeVersionId}`); markPublished(); state.selected = null; }
  /* ---------------------------------------------------------- program import */

  function importStatus(message, className) { const element = byId("import-status"); element.className = `upload-status ${className || ""}`.trim(); element.textContent = message; }
  function importBody(dryRun) {
    const form = new FormData();
    form.append("file", state.importFile, state.importFile.name);
    const position = byId("import-position").value.trim();
    if (position !== "") form.append("position", position);
    if (state.liveVersionId) form.append("expectedVersionId", state.liveVersionId);
    if (dryRun) form.append("dryRun", "true");
    return form;
  }
  // Preview first: the server merges the file onto the live schedule, validates
  // the result and rolls back, so a bad file is caught before it reaches air
  // rather than after.
  async function previewImport() {
    const chosen = byId("import-file").files[0];
    byId("import-preview").hidden = true;
    byId("import-actions").hidden = true;
    if (!chosen) { state.importFile = null; return; }
    state.importFile = chosen;
    state.importBusy = true;
    importStatus(`Validating ${chosen.name} against the live schedule…`, "busy");
    try {
      const result = await api("/rundowns/main/programs/import", { method: "POST", body: importBody(true) });
      renderImportPreview(result);
      importStatus(`${chosen.name} validates. Review the result, then Import.`, "ok");
      byId("import-actions").hidden = false;
    } catch (error) {
      importStatus(error.message, "error");
    } finally { state.importBusy = false; }
  }
  function renderImportPreview(result) {
    const preview = byId("import-preview");
    preview.replaceChildren();
    preview.hidden = false;
    const existing = new Set((state.doc?.items || []).map(item => item.key));
    const heading = document.createElement("p");
    const items = result.version?.items || [];
    heading.className = "import-heading";
    heading.textContent = `Resulting schedule — ${items.length} program${items.length === 1 ? "" : "s"}, ${result.applied?.insertItems || 0} inserted`;
    preview.append(heading);
    const list = document.createElement("ol");
    items.forEach(item => {
      const entry = document.createElement("li");
      entry.textContent = item.displayName || item.key;
      if (!existing.has(item.key)) {
        entry.append(document.createTextNode(" "), tag("new", "new"));
        // The server renames only the newcomer on a key collision; the live item
        // keeps its key, because as-run history joins on it.
        if (/-\d+$/.test(item.key)) entry.append(document.createTextNode(` renamed to ${item.key}`));
      }
      list.append(entry);
    });
    preview.append(list);
    (result.warnings || []).forEach(warning => { const note = document.createElement("p"); note.className = "import-warning"; note.textContent = warning; preview.append(note); });
  }
  async function commitImport() {
    if (!state.importFile || state.importBusy) return;
    state.importBusy = true;
    importStatus(`Importing ${state.importFile.name}…`, "busy");
    try {
      const result = await api("/rundowns/main/programs/import", { method: "POST", body: importBody(false) });
      state.liveVersionId = result.version.id;
      state.doc = await api(`/rundowns/main/versions/${result.version.id}`);
      markPublished();
      state.dirty = false; state.editRevision = 0; state.selected = null;
      await loadClock(true);
      cancelImport();
      importStatus(`Imported ${result.applied?.insertItems || 0} program${result.applied?.insertItems === 1 ? "" : "s"}; reconciliation is running.`, "ok");
      (result.warnings || []).forEach(warning => log(warning, "info"));
      log("Imported a program list; the schedule is on air.", "ok");
      render();
    } catch (error) {
      importStatus(error.message, "error");
    } finally { state.importBusy = false; }
  }
  function cancelImport() {
    state.importFile = null;
    byId("import-file").value = "";
    byId("import-preview").hidden = true;
    byId("import-actions").hidden = true;
  }

  /* --------------------------------------------------------------------- run */

  function render() {
    if (!state.enabled) return;
    renderStatus();
    renderGrid();
    renderInspector();
   
  }
  async function init() {
    await loadCatalog();
    const status = await api("/rundowns/status");
    state.enabled = status.enabled;
    state.bootstrap = status;
    renderBootstrapBanner();
    if (!state.enabled) {
      // Hand the table back to operator.js, which can still list and reorder the
      // queue without a schedule behind it.
      delete window.rundownGrid;
      if (window.operatorQueue) window.operatorQueue.refresh();
      byId("rundown-status").textContent = "Scheduling disabled";
      byId("rundown-disabled").hidden = false;
      byId("rundown-disabled").textContent = "Program scheduling is disabled, so start times, durations and graphics are unavailable. Set ENABLE_RUNDOWN_SERVICE=true after rollout tests, then restart the control service.";
      return;
    }
    byId("rundown-actions").hidden = false;
    byId("rundown-inspector").hidden = false;
    byId("import-col").hidden = false;
    try { await reloadCurrentDocument(); }
    catch (error) {
      if (error.status !== 404) throw error;
      await api("/rundowns/main/seed", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ name: "Main rundown" }) });
      await reloadCurrentDocument();
      log("Published a first program schedule from the current playlist.", "info");
    }
    await loadClock(true);
    byId("import-file").onchange = previewImport;
    byId("import-commit").onclick = commitImport;
    byId("import-cancel").onclick = cancelImport;
    window.setInterval(refreshGridLive, 1000);
    render();
  }
  window.addEventListener("DOMContentLoaded", () => init().catch(error => {
    byId("rundown-status").textContent = "Error";
    byId("rundown-disabled").hidden = false;
    byId("rundown-disabled").textContent = error.message;
    delete window.rundownGrid;
    if (window.operatorQueue) window.operatorQueue.refresh();
  }));
}());
