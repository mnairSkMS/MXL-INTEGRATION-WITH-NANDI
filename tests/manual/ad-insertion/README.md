# Ad-insertion media tooling

Two scripts that build and check media for the ad-insertion feature. **There is no separate runtime
here any more.** The channel, its playlists and its ad breaks all run in the Compose stack described
in [the ad insertion page](../../../doc/codebase/34-scte-ad-insertion.md); these are the pieces of
the old standalone harness that had no equivalent there.

## The runtime is the Compose stack

```bash
docker compose -f docker-compose.playlist.yml up -d --build
```

Everything is decided by configuration:

| what | where |
| --- | --- |
| Which media is the programme | `config/channel.yml` |
| Which media is the break reel | `config/ads.yml` |
| Which channel files exist at all | `PLAYLIST_BOOTSTRAP_FILES` in `docker-compose.playlist.yml` |
| Whether breaks happen | `transcoder.adInsertion.enable` in `config/migrated-media-playlist.json` |
| What ends a break | a cue-in, or `maxBreakDuration` |

Drop `ads.yml` from `PLAYLIST_BOOTSTRAP_FILES` and set `adInsertion.enable` to `false`, and the same
runtime is an ordinary single-playlist channel. Nothing else changes.

Cues come from the operator UI, from any writer of the `events` table, or from the input itself when
the programme is a stream that carries SCTE-35.

## prepare.sh - synthetic media

Builds labelled commercials and a programme with real SCTE-35 cues inside the transport stream. The
unified runtime plays the operator's own media and needs none of this; use it when you want media
that makes the switching legible, or a cue-carrying stream rather than operator-sent cues.

```bash
docker run --rm -v $PWD:/demo:ro -v /var/tmp/utpl-ad-demo:/demo-media \
    -e MEDIA_DIR=/demo-media utpl/transcoder-test:latest bash /demo/prepare.sh
```

Each spot carries a burnt-in `SPOT n` label, a running counter and a solid yellow corner tag. The
counter is what makes the resume defect visible: an interrupted spot shows exactly where it was cut.
`LABELS=0` builds the reel clean, at the cost of both that and `verify.sh`'s ability to tell the two
sources apart.

`SPOT_COUNT=1 SPOT_DURATION=330` is the default - one reel longer than any break, so the ad playlist
never reaches the end of an item. That matters: a break that spans an item rollover renders black,
which is an open defect and not a switching one.

To put the results on air, copy them into `media/queue` and name them in a channel YAML.

## verify.sh - what was actually on air

Samples a recording once a second, reads the corner tag to classify each second as programme, spot
or black, and prints the strip alongside the switch decisions from the log.

```bash
ffmpeg -i 'udp://@:5555?fifo_size=1000000&overrun_nonfatal=1' -c copy -t 300 \
       /var/tmp/utpl-ad-demo/out/output.ts
docker logs skandha-transcoder > /var/tmp/utpl-ad-demo/out/transcoder.log 2>&1
./verify.sh
```

It only classifies media built by `prepare.sh` with labels on. Against the operator's own media
every second reads as programme, and only the log excerpts are useful.

## Open defects this tooling reproduces

Both are recorded in [27](../../../doc/codebase/27-known-gaps-contradictions-and-remediation.md):

- An interrupted spot does not resume where it was cut, because the freeze does not stop playback.
  Measured: frozen at 43.92 s, resumed 30 s later at 73.80 s.
- A playlist renders black for several seconds at every item rollover, on air and off. With three
  clips of 10-16 s as the programme, a 117 s recording was black at 11 s, 49 s, 62 s and repeatedly
  from 99 s. Short items reproduce it in about 30 seconds.
