/**
 * Whether two input URIs would open the same socket or file.
 *
 * This decides when probing is unsafe. `GET /inputs/probe` opens the source itself, so probing
 * a single-subscriber source the engine is already holding - an SRT caller target, or a UDP
 * unicast port - can steal or duplicate the live subscription and take the channel off air.
 * Probing some *other* URI while a channel runs is safe, and is the normal reason to be on
 * this screen at all, so the comparison has to be about the endpoint rather than about the
 * engine merely being up.
 *
 * Query strings are ignored deliberately: `srt://host:9000?mode=caller&latency=120` and
 * `srt://host:9000?mode=caller` are the same socket, and treating them as different would let
 * an operator bypass the guard by editing a parameter that changes nothing about which
 * endpoint is opened.
 */
export function sourceIdentity(uri: string | undefined | null): string | null {
  if (!uri) return null;

  const trimmed = uri.trim();
  if (!trimmed) return null;

  // Not using URL(): the engine's schemes include compound forms such as `pls+file:` and
  // `hls+file:` that it does not parse usefully, and a bare `ndi://NAME` has no port.
  const match = /^([a-z0-9+.-]+):\/\/([^/?#]*)([^?#]*)/i.exec(trimmed);
  if (!match) {
    // A scheme-less or opaque URI (e.g. `pls+file:/media/x.m3u8`). Compare it whole, minus
    // any query, rather than declaring it unidentifiable - an unidentifiable source would
    // silently lose the guard.
    return trimmed.split('?')[0].toLowerCase();
  }

  const [, scheme, authority, path] = match;
  return `${scheme.toLowerCase()}://${authority.toLowerCase()}${(path || '').replace(/\/+$/, '')}`;
}

/** True when `uri` names the same endpoint as any URI in `held`. */
export function isHeldBy(
  uri: string | undefined | null,
  held: Array<string | undefined | null>
): boolean {
  const identity = sourceIdentity(uri);
  if (!identity) return false;
  return held.some((h) => sourceIdentity(h) === identity);
}
