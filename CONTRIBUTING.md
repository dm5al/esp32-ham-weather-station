# Contributing

Bug reports, hardware ports and photographs of real panels are all welcome.

## Before opening an issue

Include the serial log. Nearly every problem in this project's history was
diagnosed from it, and several were diagnosed *only* from it — a stack overflow
names the task that died, which immediately rules out most of the code.

```
idf.py -p PORT monitor
```

A healthy boot ends with:

```
I (1127) board: board ready: 800x480 RGB565, GT911 touch, LVGL running
I (1853) selftest: ==== ALL PASS ====
```

For display problems, `lcd show` prints the timings in use and the resulting
refresh rate. Say which board you have — the two are not interchangeable.

## House rules for code

**Verify against the real thing.** Every network format in this project was
checked with `curl` before a parser was written, and the one time that step was
skipped it produced confident nonsense: a numeric field was assumed to be a DXCC
entity code when it was actually the spot's age in seconds, and the display
cheerfully reported a Spanish station as being in Liberia. Check the data.

**Say what is measured and what is inferred.** The device shows WSPR receptions
because they happened, not because a model predicts them. Where something is
approximate — a country resolved from a callsign prefix, a mode guessed from a
comment — the code should say so where it is derived.

**Never invent precision.** DX spots carry no coordinates, so the device does
not plot them and does not compute a distance from a country centroid. "Another
continent" is honest; "1,340 km" would not be.

**Comment the why, not the what.** The interesting comments in this codebase
explain a hold time that has to be 50 ms, or why a lookup table is indexed by
radius squared. Nobody needs a comment saying a loop iterates.

**Match the surrounding style.** Four spaces, braces on the same line, 96
columns, `snake_case`. Doxygen `@brief` on non-obvious functions.

## Testing

There is no host-side test build — only Xtensa cross-compilers are available —
so the firmware tests itself at boot. If you add anything with arithmetic in it,
add a case to `main/lib/selftest.c`; that is what caught the missing atmospheric
refraction in the sunrise calculation, which was wrong by six minutes and
symmetric, so it looked plausible.

## Regenerating data

`main/lib/dxcc.c`, `main/lib/landmask.c` and the fonts are generated and
committed. Do not hand-edit them; re-run the scripts in `tools/` and commit the
result. See [docs/BUILD.md](docs/BUILD.md).

## The privacy rule

The operator's callsign must never reach any network service. This is not
negotiable and it constrains what can be added — it is why DX cluster telnet and
the Reverse Beacon Network cannot be used at all, despite being the obvious
sources.

Any new data source must:

1. need no account, key or identifier of any kind
2. be permitted for anonymous automated use — check `robots.txt`; DXHeat was
   rejected because it disallows the exact endpoint its data lives on
3. degrade to a visible "unavailable" rather than showing stale data as current

## Licence

Contributions are accepted under the project's licence, PolyForm Noncommercial
1.0.0. Attribution to the original authors is retained in all cases.
