(function () {
  "use strict";

  const playlistId = "main";
  const state = {
    snapshot: null,
    media: [],
    catalog: [],
    cues: [],
    selectedMediaId: null,
    requestBusy: false,
    ssaiStartedAt: null,
    ssaiEventId: null,
    ssaiCueOutLatched: false,
    ssaiOnAir: null,
    slates: [],
    slateEventId: null,
    slateTimer: null,
    polling: false,
  };

  const byId = (id) => document.getElementById(id);

  function errorMessage(error) {
    if (typeof error === "string") return error;
    return error && error.message ? error.message : "Unknown error";
  }

  async function api(path, options) {
    const response = await fetch(path, options);
    if (response.status === 204) return null;
    const type = response.headers.get("content-type") || "";
    const body = type.includes("json") ? await response.json() : await response.text();
    if (!response.ok) {
      const detail = body && (body.detail || body.message);
      const message = Array.isArray(detail)
        ? detail.map((item) => item.msg || String(item)).join(", ")
        : detail || (typeof body === "string" ? body : response.statusText);
      const error = new Error(message || `HTTP ${response.status}`);
      error.status = response.status;
      throw error;
    }
    return body;
  }

  function log(message, level) {
    const line = document.createElement("div");
    const time = document.createElement("span");
    time.className = "log-time";
    time.textContent = `[${new Date().toLocaleTimeString()}] `;
    const text = document.createElement("span");
    text.className = `log-${level || "info"}`;
    text.textContent = message;
    line.append(time, text);
    const output = byId("activity-log");
    output.prepend(line);
    while (output.children.length > 80) output.lastElementChild.remove();
  }
  window.operatorLog = log;

  function setApiState(ok, text) {
    byId("api-dot").className = `dot ${ok ? "ok" : "bad"}`;
    byId("api-status").textContent = text;
  }

  function updateClocks() {
    const now = new Date();
    byId("clock").textContent = now.toLocaleTimeString([], {
      hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false,
    });
    byId("date").textContent = now.toLocaleDateString([], {
      weekday: "long", day: "2-digit", month: "long", year: "numeric",
    });
    byId("utc").textContent = `UTC ${now.toISOString().slice(0, 19).replace("T", " ")}`;
    if (state.ssaiStartedAt !== null) {
      byId("ssai-live-n").textContent = String(Math.floor((Date.now() - state.ssaiStartedAt) / 1000));
    }
    updateCueCountdowns();
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return "—";
    if (bytes < 1024) return `${bytes} B`;
    const units = ["KiB", "MiB", "GiB", "TiB"];
    let value = bytes / 1024;
    let unit = units[0];
    for (let i = 1; value >= 1024 && i < units.length; i += 1) {
      value /= 1024;
      unit = units[i];
    }
    return `${value.toFixed(value >= 10 ? 1 : 2)} ${unit}`;
  }

  // MM:SS.hh — the grid reads as a rundown, so durations are tabular, not "12.4 sec".
  function mmss(ms) {
    if (!Number.isFinite(ms)) return "—";
    const total = Math.max(0, Math.round(ms));
    const minutes = Math.floor(total / 60000);
    const seconds = Math.floor((total % 60000) / 1000);
    const hundredths = Math.floor((total % 1000) / 10);
    return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${String(hundredths).padStart(2, "0")}`;
  }

  // ffprobe metadata is the only duration available when the rundown service is
  // off, so the fallback table can still show one.
  function probedDurationMs(asset) {
    const value = Number(asset && asset.probe && asset.probe.format && asset.probe.format.duration);
    return Number.isFinite(value) && value > 0 ? Math.round(value * 1000) : null;
  }

  window.operatorFormat = {bytes: formatBytes, mmss, probedDurationMs};

  function isApplying() {
    return state.requestBusy || !state.snapshot ||
      state.snapshot.desiredRevision !== state.snapshot.appliedRevision;
  }

  function queueError(message) {
    const hint = byId("queue-hint");
    hint.className = "qhint error";
    hint.textContent = message;
  }

  function button(label, handler, className) {
    const value = document.createElement("button");
    value.type = "button";
    value.textContent = label;
    if (className) value.className = className;
    value.addEventListener("click", handler);
    return value;
  }

  function itemStateTag(item) {
    const value = document.createElement("span");
    value.className = `tag ${item.state === "on_air" ? "air" : item.state}`;
    value.textContent = item.state === "on_air" ? "on air" : item.state;
    return value;
  }

  // The engine-reconciliation line, owned here in both modes.
  function renderHint() {
    const hint = byId("queue-hint");
    if (!state.snapshot) {
      hint.className = "qhint";
      hint.textContent = "Playlist state unavailable";
      return;
    }
    const error = state.snapshot.applyError;
    if (error) {
      hint.className = "qhint error";
      hint.textContent = `Engine apply failed: ${error}`;
    } else if (isApplying()) {
      hint.className = "qhint";
      hint.textContent = `applying revision ${state.snapshot.desiredRevision} (engine ${state.snapshot.appliedRevision})…`;
    } else {
      hint.className = "qhint";
      hint.textContent = `running continuously · revision ${state.snapshot.appliedRevision} · ${state.snapshot.items.length} item${state.snapshot.items.length === 1 ? "" : "s"}`;
    }
  }

  // The rundown grid owns the table whenever the scheduling service is enabled,
  // because only it holds the schedule document that supplies time, duration and
  // graphics. This fallback keeps the panel usable when the service is off.
  function renderQueue() {
    renderHint();
    if (window.rundownGrid) {
      window.rundownGrid.onQueue(state.snapshot);
      return;
    }
    renderFallbackQueue();
  }

  function emptyRow(body, text) {
    const row = body.insertRow();
    const cell = row.insertCell();
    cell.colSpan = 10;
    cell.className = "empty";
    cell.textContent = text;
  }

  function renderFallbackQueue() {
    const body = byId("queue-body");
    body.replaceChildren();
    if (!state.snapshot) {
      emptyRow(body, "Playlist state unavailable");
      return;
    }
    if (!state.snapshot.items.length) {
      emptyRow(body, "No clips are configured");
      return;
    }

    const applying = isApplying();
    state.snapshot.items.forEach((item, index) => {
      const onAir = item.id === state.snapshot.currentItemId || item.state === "on_air";
      const row = body.insertRow();
      if (onAir) row.className = "onair";
      row.insertCell().textContent = String(index + 1);
      row.insertCell().textContent = "";
      // Without a schedule there is no start time and no loop position.
      const start = row.insertCell();
      start.className = "cell-start dim";
      start.textContent = "—";
      const loop = row.insertCell();
      loop.className = "dim";
      loop.textContent = "—";

      const clip = row.insertCell();
      const name = document.createElement("div");
      name.className = "clip";
      name.textContent = item.media.name;
      const details = document.createElement("div");
      details.className = "sid";
      details.textContent = `${formatBytes(item.media.sizeBytes)} · ${item.id}`;
      clip.append(name, details);

      row.insertCell().textContent = "Video";
      row.insertCell().textContent = mmss(probedDurationMs(item.media));
      const graphics = row.insertCell();
      graphics.className = "dim";
      graphics.textContent = "—";

      const status = row.insertCell();
      status.className = "cell-state";
      status.append(itemStateTag(item));
      if (item.error) {
        const problem = document.createElement("div");
        problem.className = "sid";
        problem.textContent = item.error;
        status.append(problem);
      }

      row.insertCell().append(...queueActions(item, index, applying, onAir));
    });
  }

  // Shared with the rundown grid so both tables offer the same queue controls.
  function queueActions(item, index, applying, onAir) {
    const controls = [];
    const here = button("here", () => insertAfter(index));
    here.title = "Insert the selected media after this item";
    here.disabled = applying || !state.selectedMediaId;
    controls.push(here);
    if (onAir) {
      const skip = button("Skip ▸", skipCurrent, "blue");
      skip.disabled = applying || state.snapshot.items.length < 2;
      controls.push(skip);
    } else {
      const up = button("▲", () => moveItem(item, index - 1));
      const down = button("▼", () => moveItem(item, index + 1));
      const remove = button("remove", () => removeItem(item), "remove");
      up.title = "Move up";
      down.title = "Move down";
      up.disabled = applying || index === 0;
      down.disabled = applying || index === state.snapshot.items.length - 1;
      remove.disabled = applying || state.snapshot.items.length <= 1;
      controls.push(up, down, remove);
    }
    return controls;
  }

  function renderMedia() {
    const container = byId("media-files");
    container.replaceChildren();
    if (!state.media.length) {
      const empty = document.createElement("div");
      empty.className = "empty";
      empty.textContent = "No validated media files";
      container.append(empty);
      return;
    }
    state.media.forEach((asset) => {
      const row = document.createElement("div");
      row.className = `file${asset.id === state.selectedMediaId ? " selected" : ""}`;
      row.tabIndex = 0;
      row.setAttribute("role", "button");
      row.setAttribute("aria-pressed", asset.id === state.selectedMediaId ? "true" : "false");
      const name = document.createElement("span");
      name.className = "name";
      name.textContent = asset.name;
      const size = document.createElement("span");
      size.className = "size";
      size.textContent = formatBytes(asset.sizeBytes);
      row.append(name, size);
      const select = () => {
        state.selectedMediaId = asset.id === state.selectedMediaId ? null : asset.id;
        renderMedia();
        renderQueue();
      };
      row.addEventListener("click", select);
      row.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          select();
        }
      });
      container.append(row);
    });
  }

  async function mutateQueue(label, call) {
    if (isApplying()) return;
    state.requestBusy = true;
    renderQueue();
    try {
      state.snapshot = await call();
      // Insert, remove and reorder are published as schedule edits when a rundown
      // owns this playlist, so the grid's document must be reloaded or it keeps
      // rendering the item that was just removed.
      if (window.rundownGrid && window.rundownGrid.resync) await window.rundownGrid.resync();
      log(label, "ok");
    } catch (error) {
      queueError(errorMessage(error));
      log(`${label} failed: ${errorMessage(error)}`, "error");
    } finally {
      state.requestBusy = false;
      renderQueue();
    }
  }

  function insertAfter(index) {
    if (!state.selectedMediaId || !state.snapshot) return;
    const mediaAsset = state.media.find((asset) => asset.id === state.selectedMediaId);
    return mutateQueue(`Inserted ${mediaAsset ? mediaAsset.name : "media"} after row ${index + 1}`, () => api(`/playlists/${playlistId}/items`, {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({
        mediaId: state.selectedMediaId,
        position: index + 1,
        expectedRevision: state.snapshot.desiredRevision,
      }),
    }));
  }

  function moveItem(item, position) {
    if (!state.snapshot || position < 0 || position >= state.snapshot.items.length) return;
    return mutateQueue(`Moved ${item.media.name} to row ${position + 1}`, () => api(`/playlists/${playlistId}/items/${encodeURIComponent(item.id)}`, {
      method: "PATCH",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({position, expectedRevision: state.snapshot.desiredRevision}),
    }));
  }

  function removeItem(item) {
    if (!state.snapshot) return;
    return mutateQueue(`Removed ${item.media.name}`, () => api(`/playlists/${playlistId}/items/${encodeURIComponent(item.id)}?expectedRevision=${state.snapshot.desiredRevision}`, {
      method: "DELETE",
    }));
  }

  function skipCurrent() {
    return mutateQueue("Skipped the on-air clip", () => api(`/playlists/${playlistId}/skip`, {method: "POST"}));
  }

  async function uploadFile(file) {
    if (!file) return;
    const status = byId("upload-status");
    status.className = "upload-status busy";
    status.textContent = `Validating and uploading ${file.name}…`;
    const form = new FormData();
    form.append("file", file, file.name);
    try {
      const asset = await api("/media", {method: "POST", body: form});
      state.media = await api("/media");
      state.selectedMediaId = asset.id;
      renderMedia();
      renderQueue();
      status.className = "upload-status ok";
      status.textContent = `${asset.name} validated (${formatBytes(asset.sizeBytes)}) and selected.`;
      log(`Uploaded and validated ${asset.name}`, "ok");
    } catch (error) {
      status.className = "upload-status error";
      status.textContent = errorMessage(error);
      log(`Upload failed: ${errorMessage(error)}`, "error");
    } finally {
      byId("upload-file").value = "";
    }
  }

  function assetsForKind(kind) {
    return state.catalog.filter((asset) => asset.kind === kind);
  }

  function selectedAsset(kind) {
    const select = byId(`graphic-select-${kind}`);
    const id = select ? select.value : null;
    return state.catalog.find((asset) => asset.id === id) || assetsForKind(kind)[0];
  }

  function defaultGraphicMode(asset) {
    if (asset && asset.defaultMode && asset.supportedModes.includes(asset.defaultMode)) return asset.defaultMode;
    if ((asset.kind === "logo" || asset.kind === "bug") && asset.supportedModes.includes("fade")) return "fade";
    if ((asset.kind === "lband" || asset.kind === "aston") && asset.supportedModes.includes("reveal")) return "reveal";
    return asset.supportedModes[0];
  }

  function cueForKind(kind) {
    return state.cues.find((cue) => cue.kind === kind) || null;
  }

  function sponsorCue() {
    return state.cues.find((cue) => cue.kind === "lband" || cue.kind === "aston") || null;
  }

  function makeGraphicTile(kind, title) {
    const tile = document.createElement("article");
    tile.className = "tile";
    const heading = document.createElement("h3");
    heading.textContent = title;
    const box = document.createElement("div");
    box.className = "graphic-box";
    box.id = `graphic-box-${kind}`;
    const select = document.createElement("select");
    select.id = `graphic-select-${kind}`;
    select.className = "sponsor-select";
    assetsForKind(kind).forEach((asset) => {
      const option = document.createElement("option");
      option.value = asset.id;
      option.textContent = asset.enabled === false ? `${asset.label} (disabled)` : asset.label;
      option.disabled = asset.enabled === false;
      select.append(option);
    });
    const preview = document.createElement("div");
    preview.className = "thumbnail";
    const image = document.createElement("img");
    image.id = `graphic-image-${kind}`;
    image.alt = `${title} preview`;
    preview.append(image);
    const status = document.createElement("div");
    status.id = `graphic-status-${kind}`;
    status.className = "graphic-status";
    status.textContent = "Ready";
    box.append(select, preview, status);

    const controls = document.createElement("div");
    controls.className = "graphic-buttons";
    const start = button("Start", () => startGraphic(kind), "blue");
    const stop = button("Stop", () => stopGraphic(kind));
    start.id = `graphic-start-${kind}`;
    stop.id = `graphic-stop-${kind}`;
    stop.disabled = true;
    controls.append(start, stop);

    const time = graphicField(kind, "Time", "time", "number", "0", "0");
    const duration = graphicField(kind, "Duration", "duration", "number", kind === "logo" || kind === "bug" ? "" : "15", "1");
    const anim = graphicField(kind, "Anim", "anim", "number", kind === "logo" || kind === "bug" ? "0" : "0.6", "0");
    if (kind === "logo" || kind === "bug") anim.style.display = "none";
    tile.append(heading, box, controls, time, duration, anim);

    select.addEventListener("change", () => updateGraphicPreview(kind));
    return tile;
  }

  function graphicField(kind, labelText, name, type, value, min) {
    const field = document.createElement("div");
    field.className = "field";
    const label = document.createElement("label");
    label.htmlFor = `graphic-${name}-${kind}`;
    label.textContent = labelText;
    const input = document.createElement("input");
    input.id = `graphic-${name}-${kind}`;
    input.type = type;
    input.value = value;
    input.min = min;
    input.step = "0.1";
    field.append(label, input);
    return field;
  }

  function updateGraphicPreview(kind) {
    const asset = selectedAsset(kind);
    if (!asset) return;
    byId(`graphic-image-${kind}`).src = asset.thumbnail;
    const duration = byId(`graphic-duration-${kind}`);
    const anim = byId(`graphic-anim-${kind}`);
    if (asset.defaultDurationMs && (kind === "lband" || kind === "aston")) {
      duration.value = String(asset.defaultDurationMs / 1000);
    }
    if (asset.defaultAnimationMs !== undefined && anim) {
      anim.value = String(asset.defaultAnimationMs / 1000);
    }
  }

  function buildGraphics() {
    const container = byId("graphics-tiles");
    container.replaceChildren();
    [["logo", "Logo"], ["lband", "L-band"], ["bug", "Bug"], ["aston", "Aston"]]
      .forEach(([kind, title]) => {
        container.append(makeGraphicTile(kind, title));
        updateGraphicPreview(kind);
      });
    renderGraphicsState();
  }

  function cueCountdown(cue) {
    const now = Date.now();
    if (cue.phase === "scheduled") return `starts in ${Math.max(0, Math.ceil((Date.parse(cue.startsAt) - now) / 1000))}s`;
    if (cue.phase === "restoring") return `restoring ${Math.max(0, Math.ceil((Date.parse(cue.restoreUntil) - now) / 1000))}s`;
    if (cue.endsAt) return `on air · ${Math.max(0, Math.ceil((Date.parse(cue.endsAt) - now) / 1000))}s left`;
    return "on air · hold until Stop";
  }

  function updateCueCountdowns() {
    state.cues.forEach((cue) => {
      const status = byId(`graphic-status-${cue.kind}`);
      if (status) status.textContent = cueCountdown(cue);
    });
  }

  function renderGraphicsState() {
    if (!byId("graphic-start-logo")) return;
    const locked = sponsorCue();
    ["logo", "lband", "bug", "aston"].forEach((kind) => {
      const cue = cueForKind(kind);
      const sponsor = kind === "lband" || kind === "aston";
      const box = byId(`graphic-box-${kind}`);
      const start = byId(`graphic-start-${kind}`);
      const stop = byId(`graphic-stop-${kind}`);
      const status = byId(`graphic-status-${kind}`);
      box.className = `graphic-box${cue ? ` ${cue.phase === "scheduled" ? "scheduled" : "onair"}` : ""}`;
      start.disabled = Boolean(cue) || (sponsor && Boolean(locked));
      stop.disabled = !cue || !cue.cancelable;
      byId(`graphic-select-${kind}`).disabled = Boolean(cue) || (sponsor && Boolean(locked));
      if (!cue) {
        status.className = "graphic-status";
        status.textContent = sponsor && locked ? `${locked.kind === "lband" ? "L-band" : "Aston"} owns squeeze` : "Ready";
      } else {
        status.className = `graphic-status ${cue.phase === "scheduled" ? "wait" : "live"}`;
        status.textContent = cueCountdown(cue);
      }
    });
  }

  async function startGraphic(kind) {
    const asset = selectedAsset(kind);
    if (!asset || asset.enabled === false) {
      log(`${kind} asset is disabled by the video-graphics safety gate`, "error");
      return;
    }
    let startSeconds = Math.max(0, Number(byId(`graphic-time-${kind}`).value) || 0);
    const durationText = byId(`graphic-duration-${kind}`).value.trim();
    let durationSeconds = Number(durationText);
    const hold = (kind === "logo" || kind === "bug") && durationText === "";
    if (!hold && (!Number.isFinite(durationSeconds) || durationSeconds <= 0)) {
      log(`${kind} duration must be greater than zero`, "error");
      return;
    }
    if (asset.mediaType === "video") startSeconds = Math.max(startSeconds, (asset.preloadLeadMs || 3000) / 1000);
    let animSeconds = Math.max(0, Number(byId(`graphic-anim-${kind}`).value) || 0);
    if (!hold) animSeconds = Math.min(animSeconds, durationSeconds / 2);
    const mode = defaultGraphicMode(asset);
    const request = {
      assetId: asset.id,
      mode,
      durationMs: hold ? null : Math.round(durationSeconds * 1000),
      startInMs: Math.round(startSeconds * 1000),
      fadeMs: Math.round(animSeconds * 1000),
    };
    byId(`graphic-start-${kind}`).disabled = true;
    try {
      const cue = await api("/graphics/cues", {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(request),
      });
      state.cues = state.cues.filter((current) => current.id !== cue.id);
      state.cues.push(cue);
      renderGraphicsState();
      log(`Started ${asset.label}${startSeconds ? ` in ${startSeconds}s` : ""}`, "ok");
    } catch (error) {
      renderGraphicsState();
      log(`${asset.label} failed: ${errorMessage(error)}`, "error");
    }
  }

  async function stopGraphic(kind) {
    const cue = cueForKind(kind);
    if (!cue) return;
    byId(`graphic-stop-${kind}`).disabled = true;
    try {
      await api(`/graphics/cues/${encodeURIComponent(cue.id)}`, {method: "DELETE"});
      state.cues = state.cues.filter((current) => current.id !== cue.id);
      renderGraphicsState();
      log(`Stopped ${kind}`, "ok");
    } catch (error) {
      renderGraphicsState();
      log(`Stop ${kind} failed: ${errorMessage(error)}`, "error");
    }
  }

  function eventTime() {
    return null;
  }

  // ANSI/SCTE 35 2023r1 Table 23, transcribed in full at
  // doc/codebase/38-scte35-segmentation-type-id-table.md. Only the concrete ids are listed:
  // 0x02 is Private and the five reserved ranges have no message name, so those stay
  // enterable by number and are labelled by segmentationLabel() instead.
  const SEGMENTATION_TYPES = [
    {id: 0x00, name: "Not Indicated"},
    {id: 0x01, name: "Content Identification"},
    {id: 0x10, name: "Program Start"},
    {id: 0x11, name: "Program End"},
    {id: 0x12, name: "Program Early Termination"},
    {id: 0x13, name: "Program Breakaway"},
    {id: 0x14, name: "Program Resumption"},
    {id: 0x15, name: "Program Runover Planned"},
    {id: 0x16, name: "Program Runover Unplanned"},
    {id: 0x17, name: "Program Overlap Start"},
    {id: 0x18, name: "Program Blackout Override"},
    {id: 0x19, name: "Program Join"},
    {id: 0x1A, name: "Program Immediate Resumption"},
    {id: 0x20, name: "Chapter Start"},
    {id: 0x21, name: "Chapter End"},
    {id: 0x22, name: "Break Start"},
    {id: 0x23, name: "Break End"},
    {id: 0x24, name: "Opening Credit Start (deprecated)"},
    {id: 0x25, name: "Opening Credit End (deprecated)"},
    {id: 0x26, name: "Closing Credit Start (deprecated)"},
    {id: 0x27, name: "Closing Credit End (deprecated)"},
    {id: 0x30, name: "Provider Advertisement Start"},
    {id: 0x31, name: "Provider Advertisement End"},
    {id: 0x32, name: "Distributor Advertisement Start"},
    {id: 0x33, name: "Distributor Advertisement End"},
    {id: 0x34, name: "Provider Placement Opportunity Start"},
    {id: 0x35, name: "Provider Placement Opportunity End"},
    {id: 0x36, name: "Distributor Placement Opportunity Start"},
    {id: 0x37, name: "Distributor Placement Opportunity End"},
    {id: 0x38, name: "Provider Overlay Placement Opportunity Start"},
    {id: 0x39, name: "Provider Overlay Placement Opportunity End"},
    {id: 0x3A, name: "Distributor Overlay Placement Opportunity Start"},
    {id: 0x3B, name: "Distributor Overlay Placement Opportunity End"},
    {id: 0x3C, name: "Provider Promo Start"},
    {id: 0x3D, name: "Provider Promo End"},
    {id: 0x3E, name: "Distributor Promo Start"},
    {id: 0x3F, name: "Distributor Promo End"},
    {id: 0x40, name: "Unscheduled Event Start"},
    {id: 0x41, name: "Unscheduled Event End"},
    {id: 0x42, name: "Alternate Content Opportunity Start"},
    {id: 0x43, name: "Alternate Content Opportunity End"},
    {id: 0x44, name: "Provider Ad Block Start"},
    {id: 0x45, name: "Provider Ad Block End"},
    {id: 0x46, name: "Distributor Ad Block Start"},
    {id: 0x47, name: "Distributor Ad Block End"},
    {id: 0x50, name: "Network Start"},
    {id: 0x51, name: "Network End"},
  ];

  // The same two sets the server holds as SEGMENTATION_TYPE_IDS_BREAK_END and
  // _BREAK_START (services/schedule.py) and the HLS serialiser holds as the switch in
  // include/utpl/hls/types.h. Spelled out rather than derived from the names above, because
  // "End" in a message name does not by itself mean the id closes an avail. Written here so
  // the page can refuse a combination before it costs a round trip; segmentation_type_id_test.py
  // asserts these three copies still agree.
  const SEGMENTATION_TYPE_IDS_BREAK_END = [0x23, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B];
  const SEGMENTATION_TYPE_IDS_BREAK_START = [0x22, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A];

  const DEFAULT_SEGMENTATION_TYPE_ID = 0x34;

  const hex2 = (id) => `0x${id.toString(16).toUpperCase().padStart(2, "0")}`;

  // What to call an id Table 23 gives no message name for. Every value 0-255 is either
  // listed above, Private, or in one of the reserved ranges, so this always returns a label.
  function segmentationLabel(id) {
    if (id === 0x02) return `Private (${hex2(id)})`;
    return `Reserved (${hex2(id)})`;
  }

  // Every row of Table 23, for the reference panel behind the (i) beside Segmentation
  // Message. Generated from doc/codebase/38-scte35-segmentation-type-id-table.md and
  // compared against it cell by cell by api/tests/segmentation_type_id_test.py, so this
  // stays a copy of the standard rather than a second opinion about it.
  const SEGMENTATION_TABLE_ROWS = [
    ["Not Indicated", "0x00 (00)", "0", "0", "Not used", "Not Used"],
    ["Content Identification", "0x01 (01)", "0", "0", "Not used", "Not Used"],
    ["Private", "0x02 (02)", "", "", "", ""],
    ["Reserved", "0x03-0x0F", "", "", "", ""],
    ["Program Start", "0x10 (16)", "1", "1", "Not used", "Not Used"],
    ["Program End", "0x11 (17)", "1", "1", "Not used", "Not Used"],
    ["Program Early Termination", "0x12 (18)", "1", "1", "Not used", "Not Used"],
    ["Program Breakaway", "0x13 (19)", "1", "1", "Not used", "Not Used"],
    ["Program Resumption", "0x14 (20)", "1", "1", "Not used", "Not Used"],
    ["Program Runover Planned", "0x15 (21)", "1", "1", "Not used", "Not Used"],
    ["Program Runover Unplanned", "0x16 (22)", "1", "1", "Not used", "Not Used"],
    ["Program Overlap Start", "0x17 (23)", "1", "1", "Not used", "Not Used"],
    ["Program Blackout Override", "0x18 (24)", "0", "0", "Not used", "Not Used"],
    ["Program Join", "0x19 (25)", "1", "1", "Not used", "Not used"],
    ["Program Immediate Resumption", "0x1A (26)", "0", "0", "Not used", "Not used"],
    ["Reserved", "0x1B-0x1F", "", "", "", ""],
    ["Chapter Start", "0x20 (32)", "Non-zero", "Non-zero", "Not used", "Not Used"],
    ["Chapter End", "0x21 (33)", "Non-zero", "Non-zero", "Not used", "Not Used"],
    ["Break Start", "0x22 (34)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not used"],
    ["Break End", "0x23 (35)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not used"],
    ["Opening Credit Start (deprecated)", "0x24 (36)", "1", "1", "Not used", "Not used"],
    ["Opening Credit End (deprecated)", "0x25 (37)", "1", "1", "Not used", "Not used"],
    ["Closing Credit Start (deprecated)", "0x26 (38)", "1", "1", "Not used", "Not used"],
    ["Closing Credit End (deprecated)", "0x27 (39)", "1", "1", "Not used", "Not used"],
    ["Reserved", "0x28-0x2F", "", "", "", ""],
    ["Provider Advertisement Start", "0x30 (48)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Provider Advertisement End", "0x31 (49)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Distributor Advertisement Start", "0x32 (50)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Distributor Advertisement End", "0x33 (51)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Provider Placement Opportunity Start", "0x34 (52)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Provider Placement Opportunity End", "0x35 (53)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Distributor Placement Opportunity Start", "0x36 (54)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Distributor Placement Opportunity End", "0x37 (55)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Provider Overlay Placement Opportunity Start", "0x38 (56)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Provider Overlay Placement Opportunity End", "0x39 (57)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Distributor Overlay Placement Opportunity Start", "0x3A (58)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Distributor Overlay Placement Opportunity End", "0x3B (59)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Provider Promo Start", "0x3C (60)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Provider Promo End", "0x3D (61)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not used"],
    ["Distributor Promo Start", "0x3E (62)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Distributor Promo End", "0x3F (63)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not used"],
    ["Unscheduled Event Start", "0x40 (64)", "0", "0", "Not used", "Not Used"],
    ["Unscheduled Event End", "0x41 (65)", "0", "0", "Not used", "Not Used"],
    ["Alternate Content Opportunity Start", "0x42 (66)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Alternate Content Opportunity End", "0x43 (67)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Provider Ad Block Start", "0x44 (68)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Provider Ad Block End", "0x45 (69)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Distributor Ad Block Start", "0x46 (70)", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero", "0 or Non-zero"],
    ["Distributor Ad Block End", "0x47 (71)", "0 or Non-zero", "0 or Non-zero", "Not used", "Not Used"],
    ["Reserved", "0x48-0x4F", "", "", "", ""],
    ["Network Start", "0x50 (80)", "0", "0", "Not used", "Not Used"],
    ["Network End", "0x51 (81)", "0", "0", "Not used", "Not Used"],
    ["Reserved", "0x52-0xFF", "", "", "", ""],
  ];

  // Table 23 has 47 named messages and most differ from their neighbours by a single word, so
  // the control filters rather than lists. Five is what fits without the popup covering the
  // Segmentation type box directly beneath it.
  const MAX_SEGMENTATION_OPTIONS = 5;

  const combo = {open: false, matches: [], active: -1};

  // What the message box should read for an id: its Table 23 name, or the range label for one
  // the standard leaves reserved.
  function segmentationBoxLabel(id) {
    const entry = SEGMENTATION_TYPES.find((candidate) => candidate.id === id);
    return entry ? entry.name : segmentationLabel(id);
  }

  // Substring match on the name or on either spelling of the id, so "0x30", "48" and
  // "advertisement" all reach the same row. Sorted alphabetically rather than by id: the
  // operator is reading words here, and the number box beside it is where numeric order would
  // matter.
  function segmentationMatches(query) {
    const needle = query.trim().toLowerCase();
    const all = [...SEGMENTATION_TYPES].sort((a, b) => a.name.localeCompare(b.name));
    if (needle === "") return all.slice(0, MAX_SEGMENTATION_OPTIONS);

    return all
      .filter((entry) =>
        entry.name.toLowerCase().includes(needle) ||
        hex2(entry.id).toLowerCase().includes(needle) ||
        String(entry.id) === needle)
      .slice(0, MAX_SEGMENTATION_OPTIONS);
  }

  function closeSegmentationList() {
    combo.open = false;
    combo.active = -1;
    combo.matches = [];
    byId("ssai-seg-options").hidden = true;
    byId("ssai-seg-message").setAttribute("aria-expanded", "false");
  }

  function renderSegmentationList(query) {
    const list = byId("ssai-seg-options");
    combo.matches = segmentationMatches(query);
    if (combo.active >= combo.matches.length) combo.active = -1;
    list.textContent = "";

    if (!combo.matches.length) {
      const empty = document.createElement("li");
      empty.className = "empty";
      empty.textContent = "No Table 23 message matches";
      list.appendChild(empty);
    }

    combo.matches.forEach((entry, index) => {
      const option = document.createElement("li");
      option.setAttribute("role", "option");
      option.dataset.id = String(entry.id);
      option.setAttribute("aria-selected", index === combo.active ? "true" : "false");
      option.textContent = entry.name;

      const hex = document.createElement("span");
      hex.className = "hex";
      hex.textContent = hex2(entry.id);
      option.appendChild(hex);

      // mousedown, not click: the input's blur fires first and would close the list, detaching
      // this node before any click could land on it.
      option.addEventListener("mousedown", (event) => {
        event.preventDefault();
        commitSegmentationType(entry.id);
      });

      list.appendChild(option);
    });

    combo.open = true;
    list.hidden = false;
    byId("ssai-seg-message").setAttribute("aria-expanded", "true");
  }

  // The one place that moves both halves of the pair at once.
  function commitSegmentationType(id) {
    byId("ssai-seg-type-id").value = String(id);
    byId("ssai-seg-message").value = segmentationBoxLabel(id);
    closeSegmentationList();
    renderSsaiControls();
  }

  // The id currently in the number box, or null when it is empty or outside a byte. null is
  // what blocks Cue Out; it is never sent.
  function currentSegmentationTypeId() {
    const raw = byId("ssai-seg-type-id").value.trim();
    if (raw === "") return null;

    const value = Number(raw);
    if (!Number.isInteger(value) || value < 0 || value > 255) return null;
    return value;
  }

  function segmentationName(id) {
    return segmentationBoxLabel(id);
  }

  // Number box -> message box. Any half-typed search text is replaced, so the two halves can
  // never disagree about which row is selected.
  function syncSegmentationMessageFromTypeId() {
    const id = currentSegmentationTypeId();
    if (id !== null) byId("ssai-seg-message").value = segmentationBoxLabel(id);
    closeSegmentationList();
    renderSsaiControls();
  }

  // Typing in the message box only filters - it commits nothing, so the number box stays
  // authoritative until an option is actually chosen. Leaving the box abandons the search text
  // and restores the label for the id still held there.
  function onSegmentationMessageInput() {
    renderSegmentationList(byId("ssai-seg-message").value);
  }

  // Focusing shows the list to browse, not the single row whose full name is already sitting
  // in the box. The text is selected so the first keystroke replaces it rather than appending
  // to a name the operator did not type.
  function onSegmentationMessageFocus() {
    renderSegmentationList("");
    byId("ssai-seg-message").select();
  }

  function restoreSegmentationMessageLabel() {
    const id = currentSegmentationTypeId();
    byId("ssai-seg-message").value = id === null ? "" : segmentationBoxLabel(id);
  }

  function setActiveSegmentationOption(index) {
    combo.active = index;
    [...byId("ssai-seg-options").children].forEach((node, position) => {
      node.setAttribute("aria-selected", position === index ? "true" : "false");
    });
  }

  function onSegmentationMessageKeyDown(event) {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      if (!combo.open) {
        renderSegmentationList(byId("ssai-seg-message").value);
      }
      if (!combo.matches.length) return;
      const step = event.key === "ArrowDown" ? 1 : -1;
      setActiveSegmentationOption(
        (combo.active + step + combo.matches.length) % combo.matches.length,
      );
    } else if (event.key === "Enter") {
      // Only swallow Enter when it is actually choosing something; otherwise leave it to the
      // page.
      const chosen = combo.active >= 0 ? combo.matches[combo.active]
        : combo.matches.length === 1 ? combo.matches[0] : null;
      if (combo.open && chosen) {
        event.preventDefault();
        commitSegmentationType(chosen.id);
      }
    } else if (event.key === "Escape") {
      closeSegmentationList();
      restoreSegmentationMessageLabel();
    }
  }

  // --- slates -----------------------------------------------------------------------
  //
  // A slate swaps the whole programme for a card, so it is a scheduled event rather than a
  // graphics cue. Fire posts one and keeps its id; Stop deletes that id. There is no stop
  // endpoint and none is needed: the engine polls the provider every 500 ms, finds the row
  // gone, and cancels the in-progress event, which runs the same handleStop as a natural end.

  function selectedSlate() {
    const id = byId("ssai-slate").value;
    return state.slates.find((slate) => slate.id === id) || null;
  }

  function renderSlateControls() {
    const slate = selectedSlate();
    const running = state.slateEventId !== null;
    const note = byId("ssai-slate-note");

    byId("slate-fire").disabled = running || !slate;
    byId("slate-stop").disabled = !running;
    byId("ssai-slate").disabled = running;
    byId("ssai-slate-duration").disabled = running;

    if (!state.slates.length) {
      note.textContent = "No slate media available - check the slate catalog.";
      note.hidden = false;
      note.classList.add("warn");
      return;
    }

    // The one place an operator can find out that this slate will silence the programme.
    // Slate media with no audio track of its own mutes channel audio for its duration.
    if (slate && !slate.hasAudio) {
      note.textContent = "This slate has no audio - programme audio is muted while it runs.";
      note.hidden = false;
      note.classList.remove("warn");
      return;
    }

    note.hidden = true;
    note.classList.remove("warn");
  }

  async function loadSlates() {
    try {
      state.slates = await api("/slates/catalog");
    } catch (error) {
      state.slates = [];
      log(`Slate catalog failed to load: ${errorMessage(error)}`, "error");
    }

    const select = byId("ssai-slate");
    select.textContent = "";

    for (const slate of state.slates) {
      const option = document.createElement("option");
      option.value = slate.id;
      option.textContent = slate.hasAudio ? slate.label : `${slate.label} (no audio)`;
      select.appendChild(option);
    }

    renderSlateControls();
  }

  function clearSlateState() {
    if (state.slateTimer !== null) {
      window.clearTimeout(state.slateTimer);
      state.slateTimer = null;
    }
    state.slateEventId = null;
    renderSlateControls();
  }

  async function fireSlate() {
    const slate = selectedSlate();
    if (!slate || state.slateEventId !== null) return;

    const seconds = Math.max(1, Number(byId("ssai-slate-duration").value) || 15);

    const event = {
      type: "slate", time: eventTime(), details: {slate: {
        url: slate.mediaUri, duration: Math.round(seconds * 1000),
      }},
    };

    byId("slate-fire").disabled = true;

    try {
      const created = await api("/scheduled_events", {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify([event]),
      });

      // The id is the only handle on a running slate; without it Stop has nothing to delete.
      state.slateEventId = created[0].id;
      renderSlateControls();

      // Nothing tells the page when the slate ends on its own, so release the controls on its
      // own duration. Guarded on the id so a Stop, or a second slate, cannot be undone by an
      // earlier timer firing late.
      const firedId = state.slateEventId;
      state.slateTimer = window.setTimeout(() => {
        if (state.slateEventId !== firedId) return;
        clearSlateState();
      }, seconds * 1000);

      log(
        `Slate fired (${slate.label}, ${seconds}s` +
        `${slate.hasAudio ? "" : ", programme audio muted"})`,
        "ok",
      );
    } catch (error) {
      renderSlateControls();
      log(`Fire slate failed: ${errorMessage(error)}`, "error");
    }
  }

  async function stopSlate() {
    const eventId = state.slateEventId;
    if (eventId === null) return;

    byId("slate-stop").disabled = true;

    try {
      await api(`/scheduled_events/${encodeURIComponent(eventId)}`, {method: "DELETE"});
      clearSlateState();
      log("Slate stopped", "ok");
    } catch (error) {
      renderSlateControls();
      log(`Stop slate failed: ${errorMessage(error)}`, "error");
    }
  }

  // --- the Table 23 reference panel -------------------------------------------------

  function buildScteTable() {
    const table = byId("scte-table");
    if (table.rows.length) return;

    const head = table.createTHead().insertRow();
    for (const column of ["Segmentation Message", "segmentation_type_id", "segment_num",
                          "segments_expected", "sub_segment_num", "sub_segments_expected"]) {
      const cell = document.createElement("th");
      cell.textContent = column;
      head.appendChild(cell);
    }

    const bodyRows = table.createTBody();
    for (const cells of SEGMENTATION_TABLE_ROWS) {
      const row = bodyRows.insertRow();
      // Reserved and Private rows carry no counts and name no message; mark them so they read
      // as ranges rather than as something an operator could pick.
      const named = SEGMENTATION_TYPES.some((entry) => cells[1].startsWith(`${hex2(entry.id)} (`));
      if (!named) row.className = "range";
      row.dataset.typeId = cells[1];
      cells.forEach((text, index) => {
        const cell = row.insertCell();
        cell.textContent = text;
        if (index === 1) cell.className = "id";
      });
    }
  }

  function openScteTable() {
    buildScteTable();

    // Point at the row the panel was opened from, so the reference answers "what is this one"
    // as well as "what else is there".
    const id = currentSegmentationTypeId();
    const wanted = id === null ? null : `${hex2(id)} (${String(id).padStart(2, "0")})`;
    let current = null;
    for (const row of byId("scte-table").tBodies[0].rows) {
      const match = wanted !== null && row.dataset.typeId === wanted;
      row.classList.toggle("current", match);
      if (match) current = row;
    }

    byId("scte-table-modal").hidden = false;
    if (current && current.scrollIntoView) current.scrollIntoView({block: "center"});
    byId("scte-table-close").focus();
  }

  function closeScteTable() {
    byId("scte-table-modal").hidden = true;
    byId("ssai-seg-info").focus();
  }

  // A time_signal break closes on its duration and a splice break with auto return closes
  // itself, so in both cases there is nothing for an operator to click. Cue In only exists
  // for a splice break explicitly opened with auto return off.
  function manualCueInAllowed() {
    return byId("ssai-type").value !== "timesignal" && !byId("ssai-auto-return").checked;
  }

  function renderSsaiControls() {
    const timeSignal = byId("ssai-type").value === "timesignal";

    // Auto return is a splice_insert field; it has no meaning for a time_signal.
    byId("ssai-auto-return-row").hidden = timeSignal;

    // The UPID field needs no gating: SCTE-35 puts descriptors on the splice_info_section
    // rather than on the command, so both cue-out types can carry one.

    const cueIn = byId("cue-in");
    cueIn.hidden = !manualCueInAllowed();
    if (cueIn.hidden) cueIn.disabled = true;

    // A splice_insert only carries a segmentation_descriptor when a UPID is supplied - the
    // scheduler emits one on that condition alone (event/scheduler/spliceInsert.cpp) - so
    // for Splice Signaling with the UPID blank the type would go nowhere. Say so and grey
    // the pair out rather than letting an operator set a value the wire never sees. A
    // time_signal is nothing but descriptors, so there the pair is always live.
    const upid = byId("ssai-upid").value.trim();
    const inert = !timeSignal && upid === "";

    byId("ssai-seg-message").disabled = inert;
    byId("ssai-seg-type-id").disabled = inert;

    const typeId = currentSegmentationTypeId();
    const note = byId("ssai-seg-note");
    let problem = null;
    let hint = null;

    if (inert) {
      hint = "Applies once a UPID is entered - a splice_insert carries no descriptor without one.";
    } else if (typeId === null) {
      problem = "Segmentation type must be a whole number from 0 to 255.";
    } else if (timeSignal && SEGMENTATION_TYPE_IDS_BREAK_END.includes(typeId)) {
      // The same rule the server applies in services/schedule.py; refusing it here saves a
      // round trip that could only ever come back 400.
      problem = `${hex2(typeId)} closes an avail, and a time_signal break closes on its `
        + "duration rather than on a manual cue-in. Use Splice Signaling with auto return "
        + "off for an operator-closed break.";
    }
    // The server also refuses a break-start time_signal with no duration. The UI cannot
    // produce one: cueOut() clamps the duration to at least a second.

    note.textContent = problem || hint || "";
    note.hidden = !(problem || hint);
    note.classList.toggle("warn", Boolean(problem));

    byId("cue-out").disabled = state.ssaiCueOutLatched || Boolean(problem);
  }

  async function cueOut() {
    const seconds = Math.max(1, Number(byId("ssai-duration").value) || 30);
    const eventId = Math.floor(Date.now() / 1000) >>> 0;
    const timeSignal = byId("ssai-type").value === "timesignal";
    const autoReturn = byId("ssai-auto-return").checked;

    // renderSsaiControls() disables Cue Out whenever this is null, so reaching here with no
    // id means the two got out of step; refuse rather than post a descriptor without one.
    const typeId = currentSegmentationTypeId();
    if (typeId === null) {
      log("Segmentation type must be a whole number from 0 to 255", "error");
      return;
    }

    const descriptor = {
      segmentation_cancel_indicator: "SEGMENTATION_EVENT_NOT_CANCELED",
      segmentation_duration: Math.round(seconds * 90000),
      segmentation_event_id: eventId,
      segmentation_type_id: typeId,
    };

    const upid = byId("ssai-upid").value.trim();
    if (upid) {
      // segmentation_upid is literal ASCII: its characters are the UPID bytes, so the
      // server rejects anything outside ASCII rather than re-encoding it. Catch that here
      // to save a round trip.
      if (!/^[\x20-\x7E]+$/.test(upid)) {
        log("UPID must be printable ASCII", "error");
        return;
      }

      descriptor.segmentation_upid = upid;
      // Type 15 (URI) is defined as a plain character string, so decoders print the value
      // whole. Type 12 (MPU) would split it into a 4-byte format identifier plus private
      // data, which reads as truncation even though the bytes are identical.
      descriptor.segmentation_upid_type = 15;
    }

    const event = timeSignal ? {
      type: "scte35_timesignal", time: eventTime(), details: {scte35_timesignal: {
        scte35_descriptors: [descriptor],
      }}}
    : {
      type: "scte35_spliceinsert", time: eventTime(), details: {scte35_spliceinsert: {
        duration: Math.round(seconds * 1000), splice_event_id: eventId,
        auto_return: autoReturn,
        ...(upid ? {
          segmentation_upid: upid,
          segmentation_upid_type: descriptor.segmentation_upid_type,
          segmentation_type_id: descriptor.segmentation_type_id,
        } : {}),
      }},
    };
    // An engine with ad insertion configured ends a break on a cue-in and never on the duration
    // carried by the cue-out (AdBreakSwitcher sets ignoreSignalledDuration). So a self-closing
    // break also sends its closing event up front, as an automation system would. The cue-out is
    // immediate and the engine places its splice point one preroll ahead, while a cue-in with an
    // explicit time fires at that time: the preroll is added back so the break lasts the entered
    // duration (without it a requested 12 s ran 7 s). 4 s is the engine's fixed preroll -
    // scte.prerollTime is in the schema but not parsed. A manual break (auto return off) keeps
    // waiting for Cue In; maxBreakDuration remains the backstop if a closing event is lost.
    const PREROLL_MS = 4000;
    const endsAt = new Date(Date.now() + PREROLL_MS + seconds * 1000).toISOString();
    let closing = null;
    if (timeSignal) {
      // Segmentation start types are even and their end type is the next value (0x34 -> 0x35).
      if (typeId % 2 === 0) {
        closing = {
          type: "scte35_timesignal", time: endsAt, details: {scte35_timesignal: {scte35_descriptors: [{
            segmentation_cancel_indicator: "SEGMENTATION_EVENT_NOT_CANCELED",
            segmentation_event_id: eventId,
            segmentation_type_id: typeId + 1,
            ...(upid ? {segmentation_upid: upid, segmentation_upid_type: descriptor.segmentation_upid_type} : {}),
          }]}}};
      }
    } else if (autoReturn) {
      closing = {
        type: "scte35_returntonetwork", time: endsAt, details: {scte35_returntonetwork: {
          splice_event_id: eventId,
        }}};
    }
    try {
      await api("/scheduled_events", {
        method: "POST", headers: {"Content-Type": "application/json"},
        body: JSON.stringify(closing ? [event, closing] : [event]),
      });
      state.ssaiStartedAt = Date.now();
      state.ssaiEventId = eventId;
      byId("ssai-live").classList.add("on");

      const manual = manualCueInAllowed();

      // Held in state, not on the button: renderSsaiControls() runs on every keystroke in
      // the segmentation fields and would otherwise re-enable Cue Out mid-break.
      state.ssaiCueOutLatched = manual;
      byId("cue-out").disabled = manual;
      byId("cue-in").disabled = !manual;

      // Nothing will click Cue In for a self-closing break, so release the controls on the
      // break's own duration instead. Without this the panel would stay latched forever.
      if (!manual) {
        window.setTimeout(() => {
          if (state.ssaiEventId !== eventId) return;
          clearSsaiState();
        }, seconds * 1000);
      }

      log(
        `SSAI Cue Out (${timeSignal ? "Time Signal" : "Splice"}, ${seconds}s` +
        `${timeSignal ? "" : `, auto return ${autoReturn ? "on" : "off"}`}` +
        `${upid ? `, upid ${upid}` : ""}` +
        `${timeSignal || upid ? `, ${segmentationName(typeId)} ${hex2(typeId)}` : ""})`,
        "ok",
      );
    } catch (error) {
      log(`Cue Out failed: ${errorMessage(error)}`, "error");
    }
  }

  function clearSsaiState() {
    state.ssaiStartedAt = null;
    state.ssaiEventId = null;
    byId("ssai-live-n").textContent = "0";
    state.ssaiCueOutLatched = false;
    byId("cue-out").disabled = false;
    byId("ssai-live").classList.remove("on");
    renderSsaiControls();
  }

  async function cueIn() {
    const eventId = state.ssaiEventId;
    if (eventId === null) return;

    // Only reachable for a splice break opened with auto return off; the button is hidden
    // otherwise. A time_signal has no manual cue-in at all - the API rejects one.
    if (!manualCueInAllowed()) return;

    const event = {
      type: "scte35_returntonetwork", time: eventTime(), details: {scte35_returntonetwork: {
        splice_event_id: eventId,
      }},
    };
    try {
      await api("/scheduled_events", {
        method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify([event]),
      });
      clearSsaiState();
      log("SSAI Cue In (Return to Network)", "ok");
    } catch (error) {
      log(`Cue In failed: ${errorMessage(error)}`, "error");
    }
  }

  // The rundown grid needs the queue as its spine (playout order, on-air item,
  // reconciliation state) and needs to reuse these mutations rather than
  // reimplement optimistic-revision handling. Exposed as one explicit object so
  // the dependency stays one-directional and greppable.
  window.operatorQueue = {
    get snapshot() { return state.snapshot; },
    get selectedMediaId() { return state.selectedMediaId; },
    isApplying,
    itemStateTag,
    queueActions,
    formatBytes,
    // Lets the rundown module hand the table back the moment it discovers the
    // scheduling service is off, instead of leaving "loading…" up until the
    // next poll.
    refresh: renderQueue,
  };

  async function refreshQueue() {
    const previous = state.snapshot;
    const next = await api(`/playlists/${playlistId}`);
    state.snapshot = next;
    if (previous && previous.desiredRevision !== previous.appliedRevision &&
        next.desiredRevision === next.appliedRevision) {
      log(`Engine applied playlist revision ${next.appliedRevision}`, "ok");
    }
    if (next.applyError && (!previous || previous.applyError !== next.applyError)) {
      log(`Playlist reconciliation failed: ${next.applyError}`, "error");
    }
    renderQueue();
  }

  async function refreshCues() {
    state.cues = await api("/graphics/cues");
    renderGraphicsState();
  }

  // Follows the engine rather than this panel's own last click. With ad insertion configured a
  // break also ends on maxBreakDuration, which the client that opened it cannot see happen, so
  // without this the panel would keep showing a break that finished. Only a break the engine
  // reported on air and then off air releases the panel; an engine without ad insertion always
  // reports off air and leaves the panel to its own timers.
  async function refreshSsai() {
    let onAir;

    try {
      onAir = (await api("/ssai/state")).onAir;
    } catch (error) {
      return; // the engine is unreachable; leave the controls as they are
    }

    const wasOnAir = state.ssaiOnAir;
    state.ssaiOnAir = onAir;

    if (wasOnAir === true && onAir === false && state.ssaiEventId !== null) {
      log("Ad break ended", "ok");
      clearSsaiState();
    }
  }

  async function poll() {
    if (state.polling) return;
    state.polling = true;
    try {
      await Promise.all([refreshQueue(), refreshCues(), refreshSsai()]);
      setApiState(true, "API connected");
    } catch (error) {
      setApiState(false, "API unavailable");
      if (!state.snapshot) queueError(errorMessage(error));
    } finally {
      state.polling = false;
    }
  }

  async function boot() {
    updateClocks();
    setInterval(updateClocks, 1000);
    byId("upload-file").addEventListener("change", (event) => uploadFile(event.target.files[0]));
    byId("cue-out").addEventListener("click", cueOut);
    byId("cue-in").addEventListener("click", cueIn);
    byId("ssai-type").addEventListener("change", renderSsaiControls);
    byId("ssai-auto-return").addEventListener("change", renderSsaiControls);
    byId("ssai-upid").addEventListener("input", renderSsaiControls);
    byId("ssai-seg-message").addEventListener("input", onSegmentationMessageInput);
    byId("ssai-seg-message").addEventListener("keydown", onSegmentationMessageKeyDown);
    byId("ssai-seg-message").addEventListener("focus", onSegmentationMessageFocus);
    byId("ssai-seg-message").addEventListener("blur", () => {
      closeSegmentationList();
      restoreSegmentationMessageLabel();
    });
    byId("ssai-seg-type-id").addEventListener("input", syncSegmentationMessageFromTypeId);

    byId("ssai-seg-info").addEventListener("click", openScteTable);
    byId("scte-table-close").addEventListener("click", closeScteTable);
    // Clicking the backdrop - anywhere outside the card - dismisses the panel. The target
    // check is what confines this to the backdrop, so a click that starts inside the card and
    // drags out does not close it.
    byId("scte-table-modal").addEventListener("click", (event) => {
      if (event.target === byId("scte-table-modal")) closeScteTable();
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && !byId("scte-table-modal").hidden) closeScteTable();
    });

    byId("ssai-slate").addEventListener("change", renderSlateControls);
    byId("slate-fire").addEventListener("click", fireSlate);
    byId("slate-stop").addEventListener("click", stopSlate);

    restoreSegmentationMessageLabel();
    renderSsaiControls();
    renderSlateControls();

    // Kept out of the Promise.all below: loadSlates handles its own failure, so a missing
    // slate catalog degrades the slate row alone instead of failing the whole page load.
    await loadSlates();

    try {
      const [mediaAssets, graphicsCatalog, playlist, activeCues] = await Promise.all([
        api("/media"), api("/graphics/catalog"), api(`/playlists/${playlistId}`), api("/graphics/cues"),
      ]);
      state.media = mediaAssets;
      state.catalog = graphicsCatalog.assets;
      state.snapshot = playlist;
      state.cues = activeCues;
      renderMedia();
      renderQueue();
      buildGraphics();
      setApiState(true, "API connected");
      log("Operator controls loaded from live service state", "ok");
    } catch (error) {
      setApiState(false, "API unavailable");
      queueError(errorMessage(error));
      log(`Startup failed: ${errorMessage(error)}`, "error");
    }
    setInterval(poll, 1000);
  }

  document.addEventListener("DOMContentLoaded", boot);
}());
