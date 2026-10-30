/* Drives the real operator page in Chromium against a RUNNING stack.

   This is the companion to drive.mjs, not a replacement: jsdom has no layout, no painting and
   no hit testing, so an entire class of defect is invisible to it. The first run of this file
   found one - `.modal{display:flex}` overrode the `hidden` attribute, leaving an invisible
   full-page backdrop at z-index 1000 that swallowed every click on the operator page. Every
   jsdom assertion still passed, because `el.hidden` was true.

   Assert here only what needs a real browser: geometry, stacking, what is actually painted at
   a point, sticky positioning, computed style. Behaviour that a DOM alone can settle belongs
   in drive.mjs, which needs no running server. */
import { chromium } from "playwright";

const URL = process.env.OPERATOR_URL || "http://127.0.0.1:8080/operator";
const OUT = process.env.SHOT_DIR || "/w/shots";
const results = [];
const check = (label, cond, detail) => {
  results.push(`${cond ? "  ok  " : "  FAIL"} ${label}${cond ? "" : ` — ${detail}`}`);
  if (!cond) process.exitCode = 1;
};

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

const consoleErrors = [];
page.on("pageerror", (e) => consoleErrors.push(String(e)));
page.on("console", (m) => { if (m.type() === "error") consoleErrors.push(m.text()); });

await page.goto(URL, { waitUntil: "networkidle" });
await page.waitForTimeout(600);

const ssai = page.locator("section.panel", { has: page.locator("#ssai-type") });
await ssai.screenshot({ path: `${OUT}/01-ssai-initial.png` });

check("the page loaded without a script error", consoleErrors.length === 0, consoleErrors.join(" | "));
check("the message box shows the committed name",
  (await page.inputValue("#ssai-seg-message")) === "Provider Placement Opportunity Start",
  await page.inputValue("#ssai-seg-message"));
check("the pair is disabled with no UPID",
  await page.isDisabled("#ssai-seg-message") && await page.isDisabled("#ssai-seg-type-id"));

// The (i) must sit on the label line, not wrap below it.
const labelBox = await page.locator("#ssai-seg-message-label").boundingBox();
const infoBox = await page.locator("#ssai-seg-info").boundingBox();
check("the (i) sits on the label's line",
  Math.abs((labelBox.y + labelBox.height / 2) - (infoBox.y + infoBox.height / 2)) < 6,
  `label mid ${labelBox.y + labelBox.height / 2}, info mid ${infoBox.y + infoBox.height / 2}`);
check("the (i) is a visible size", infoBox.width >= 12 && infoBox.width <= 20, JSON.stringify(infoBox));

// A glyph must fit inside the control drawn around it. `font: 700 10px/13px inherit` was
// invalid CSS - a CSS-wide keyword cannot stand in for the family inside the shorthand - so
// the declaration was dropped silently and the "i" rendered at the inherited 15px inside a
// 15px circle, hanging out of the bottom. Asserting the computed value is what catches a
// declaration the parser threw away; asserting the ink fits is what catches it looking wrong
// for any other reason.
const fits = (selector) => page.evaluate((sel) => {
  const el = document.querySelector(sel);
  const cs = getComputedStyle(el);
  const box = el.getBoundingClientRect();
  const range = document.createRange();
  range.selectNodeContents(el);
  const ink = range.getBoundingClientRect();
  return {
    fontSize: parseFloat(cs.fontSize),
    lineHeight: cs.lineHeight,
    boxH: box.height, boxW: box.width,
    inkTop: ink.top - box.top, inkBottom: box.bottom - ink.bottom,
  };
}, selector);

const infoInk = await fits("#ssai-seg-info");
check("the (i) glyph is sized for its circle rather than inheriting the body size",
  infoInk.fontSize === 10, `computed font-size ${infoInk.fontSize}px`);
check("and sits inside the circle, not hanging out of it",
  infoInk.inkTop >= 0 && infoInk.inkBottom >= 0,
  `${infoInk.inkTop.toFixed(1)}px above, ${infoInk.inkBottom.toFixed(1)}px below`);
check("and is optically centred",
  Math.abs(infoInk.inkTop - infoInk.inkBottom) <= 1.5,
  `${infoInk.inkTop.toFixed(1)} vs ${infoInk.inkBottom.toFixed(1)}`);

// A hidden overlay must be out of the hit-test layer entirely. `display:flex` on .modal beats
// the UA stylesheet's [hidden]{display:none}, which left an invisible full-page backdrop at
// z-index 1000 swallowing every click on the page. jsdom cannot see this: it has no layout and
// no hit testing, so el.hidden read true while the page was unusable.
const topLeftOwner = await page.evaluate(() => {
  const hit = document.elementFromPoint(20, 300);
  return hit ? (hit.id || hit.className || hit.tagName) : "none";
});
check("a hidden modal does not intercept clicks on the page",
  !String(topLeftOwner).includes("modal"), `element at (20,300) is ${topLeftOwner}`);
check("and the browser agrees it is not displayed",
  (await page.evaluate(() =>
    getComputedStyle(document.getElementById("scte-table-modal")).display)) === "none");

await page.fill("#ssai-upid", "PROMO-1234");
await page.waitForTimeout(150);

// --- the combobox, open ---
await page.click("#ssai-seg-message");
await page.waitForTimeout(250);
await ssai.screenshot({ path: `${OUT}/02-combo-open.png` });

const optionCount = await page.locator("#ssai-seg-options > li").count();
check("focusing shows five options", optionCount === 5, String(optionCount));

const names = await page.locator("#ssai-seg-options > li").allTextContents();
check("alphabetical", JSON.stringify(names) === JSON.stringify([...names].sort()), names.join(" | "));

// The popup must float ABOVE the Segmentation type field, not push it down or hide behind it.
const listBox = await page.locator("#ssai-seg-options").boundingBox();
const typeBox = await page.locator("#ssai-seg-type-id").boundingBox();
check("the popup overlaps the field below it rather than displacing it",
  listBox.y + listBox.height > typeBox.y,
  `list ends ${listBox.y + listBox.height}, type box starts ${typeBox.y}`);
const painted = await page.evaluate(() => {
  const list = document.getElementById("ssai-seg-options");
  const r = list.getBoundingClientRect();
  const hit = document.elementFromPoint(r.left + r.width / 2, r.top + 10);
  return list.contains(hit);
});
check("and is the element actually painted on top", painted);

await page.fill("#ssai-seg-message", "promo");
await page.waitForTimeout(250);
await ssai.screenshot({ path: `${OUT}/03-combo-filtered.png` });
const filtered = await page.locator("#ssai-seg-options > li").allTextContents();
check("typing 'promo' narrows to the four promo messages",
  filtered.length === 4 && filtered.every((n) => /Promo/.test(n)), filtered.join(" | "));

// First alphabetically among the promo messages is Distributor Promo *End*, 0x3F = 63 -
// "End" sorts before "Start". Not 0x3E.
await page.locator("#ssai-seg-options > li").first().click();
await page.waitForTimeout(200);
check("clicking an option commits its id",
  (await page.inputValue("#ssai-seg-type-id")) === "63",
  await page.inputValue("#ssai-seg-type-id"));
await ssai.screenshot({ path: `${OUT}/04-committed.png` });

// --- the reference panel ---
await page.click("#ssai-seg-info");
await page.waitForTimeout(350);
await page.screenshot({ path: `${OUT}/05-table-modal.png` });

check("the panel is visible", await page.isVisible("#scte-table-modal"));

const closeInk = await fits("#scte-table-close");
check("the close button is square",
  Math.abs(closeInk.boxW - closeInk.boxH) < 0.5, `${closeInk.boxW} x ${closeInk.boxH}`);
check("and its glyph is vertically centred",
  Math.abs(closeInk.inkTop - closeInk.inkBottom) <= 1.5,
  `${closeInk.inkTop.toFixed(1)} vs ${closeInk.inkBottom.toFixed(1)}`);
const rows = await page.locator("#scte-table tbody tr").count();
check("it lists all 53 Table 23 rows", rows === 53, String(rows));

// "on top of everything": nothing may paint over the card.
const onTop = await page.evaluate(() => {
  const card = document.querySelector(".modal-card");
  const r = card.getBoundingClientRect();
  const hit = document.elementFromPoint(r.left + r.width / 2, r.top + 30);
  return card.contains(hit);
});
check("nothing paints over the panel", onTop);

const highlighted = await page.locator("#scte-table tbody tr.current").allTextContents();
check("the current row is highlighted",
  highlighted.length === 1 && /Distributor Promo End/.test(highlighted[0]),
  highlighted.join(" | "));

const inView = await page.evaluate(() => {
  const row = document.querySelector("#scte-table tbody tr.current");
  const body = document.querySelector(".modal-body");
  const r = row.getBoundingClientRect(), b = body.getBoundingClientRect();
  return r.top >= b.top && r.bottom <= b.bottom;
});
check("and was scrolled into view", inView);

// Header must stay readable while the body scrolls under it.
await page.locator(".modal-body").evaluate((el) => { el.scrollTop = 600; });
await page.waitForTimeout(200);
await page.screenshot({ path: `${OUT}/06-table-scrolled.png` });
const headerStuck = await page.evaluate(() => {
  const th = document.querySelector("#scte-table thead th");
  const body = document.querySelector(".modal-body");
  return th.getBoundingClientRect().top >= body.getBoundingClientRect().top - 1;
});
check("the column headers stay pinned while scrolling", headerStuck);

// Dismissal by clicking outside: top-left corner of the viewport is backdrop.
await page.mouse.click(12, 12);
await page.waitForTimeout(300);
check("clicking outside dismisses it", await page.isHidden("#scte-table-modal"));

await page.click("#ssai-seg-info");
await page.waitForTimeout(250);
await page.keyboard.press("Escape");
await page.waitForTimeout(250);
check("Escape dismisses it too", await page.isHidden("#scte-table-modal"));

// --- the warning path, rendered ---
await page.selectOption("#ssai-type", "timesignal");
await page.fill("#ssai-seg-type-id", "35");
await page.waitForTimeout(250);
await ssai.screenshot({ path: `${OUT}/07-warning.png` });
check("a break-end id on a time_signal disables Cue Out", await page.isDisabled("#cue-out"));

// --- the slate row ---
// Real layout only: whether the picker and its note sit inside the panel, and whether the
// buttons are reachable. What they post is drive.mjs's job.
await page.selectOption("#ssai-slate", { index: 1 });
await page.waitForTimeout(200);

const panelBox = await page.locator("section.panel", { has: page.locator("#ssai-type") }).boundingBox();
const noteBox = await page.locator("#ssai-slate-note").boundingBox();
check("the slate note is inside the SSAI panel",
  noteBox.x >= panelBox.x - 1 && noteBox.x + noteBox.width <= panelBox.x + panelBox.width + 1,
  `note ${noteBox.x}..${noteBox.x + noteBox.width}, panel ${panelBox.x}..${panelBox.x + panelBox.width}`);
check("and does not overflow the panel's bottom",
  noteBox.y + noteBox.height <= panelBox.y + panelBox.height + 1,
  `note ends ${noteBox.y + noteBox.height}, panel ends ${panelBox.y + panelBox.height}`);

const fireBox = await page.locator("#slate-fire").boundingBox();
const stopBox = await page.locator("#slate-stop").boundingBox();
check("Fire and Stop share a row rather than stacking",
  Math.abs(fireBox.y - stopBox.y) < 2, `${fireBox.y} vs ${stopBox.y}`);
check("and neither is clipped away",
  fireBox.width > 40 && stopBox.width > 40, `${fireBox.width} / ${stopBox.width}`);

const fireHit = await page.evaluate(() => {
  const b = document.getElementById("slate-fire");
  const r = b.getBoundingClientRect();
  return b.contains(document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2));
});
check("Fire is the element actually painted at its own centre", fireHit);

await page.locator("section.panel", { has: page.locator("#ssai-type") })
          .screenshot({ path: `${OUT}/08-slate-row.png` });

check("no script errors across the whole run", consoleErrors.length === 0, consoleErrors.join(" | "));

console.log(results.join("\n"));
console.log(`\n${results.length} checks, ${results.filter((r) => r.includes("FAIL")).length} failed`);
await browser.close();
