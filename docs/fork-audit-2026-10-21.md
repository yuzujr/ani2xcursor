# Public fork audit — 2026-10-21

Compared all seven branch heads in the five public forks listed by GitHub against
upstream `main` at `f88701e49a65c67224ef03aa0c3a9ee3f8c37967`.
Also searched upstream pull requests, including closed requests, for the authors
of the unsubmitted changes. Private forks, deleted branches, and unpushed local
changes are outside this audit.

| Fork | Changes absent from upstream | PR status |
| --- | --- | --- |
| [portasynthinca3](https://github.com/portasynthinca3/ani2xcursor) | One commit on `use-color-count-field`: use the BMP color count | Submitted as [#6](https://github.com/yuzujr/ani2xcursor/pull/6) |
| [ntrrgc](https://github.com/ntrrgc/ani2xcursor) | None; both branches are ancestors of upstream | Delay fix already merged as [#4](https://github.com/yuzujr/ani2xcursor/pull/4) |
| [C0j23T](https://github.com/C0j23T/ani2xcursor) | One commit: nearest neighbor resizing to prevent pixel art blurring | No upstream PR found |
| [ArtixBTW](https://github.com/ArtixBTW/ani2xcursor) | Includes C0j23T's same commit, plus a second commit using nearest neighbor for enlargement and bilinear interpolation with an intermediate image for reduction | No upstream PR found |
| [yigekuyou](https://github.com/yigekuyou/ani2xcursor) | None; its branch is an ancestor of upstream | No unsubmitted code found |

## Unsubmitted need: preserve pixel art when resizing

[C0j23T's commit](https://github.com/C0j23T/ani2xcursor/commit/396d1573359562ee7df24b8308c782bd6847e6da)
explicitly describes preventing pixel art blurring. It replaces bilinear
interpolation with nearest neighbor for every resize. This preserves source
colors and sharp pixel boundaries when enlarging, but reduction can discard thin
features and produce jagged edges.

[ArtixBTW's follow-up](https://github.com/ArtixBTW/ani2xcursor/commit/608783f1658cc6442ada2dd0eaeca7cd41fb1b94)
uses nearest neighbor when enlarging and bilinear interpolation when reducing.
Reduction first resizes to twice the target size and then to the target size.
For a 64px source and a 30px target, this means 64 → 60 → 30: the intermediate
operation is itself a reduction. This is not a general area resampling filter.
It can also round hotspots twice: an x hotspot of 3 becomes 3 at 60px and then
2 at 30px, whereas a direct 64 → 30 conversion rounds it to 1.

These are two forks sharing one original change, rather than two independent
implementations. Their commit messages provide evidence of dissatisfaction with
resizing quality; they do not establish the authors' broader views or reasons for
not submitting a PR.

Recommended follow-up: an explicit resampling option, preserving the existing
default and offering nearest neighbor for pixel art. Evaluate reduction quality
and hotspot accuracy separately before adopting the second fork's approach.
No fork resizing changes were incorporated during this maintenance pass.

## Validation of the current maintenance changes

- Added regression coverage for reduced 1/4/8-bit palettes, the zero color-count
  fallback, transparency masks, a minimal 32-bit cursor, and invalid color counts.
- Added CLI coverage for the standard size preset and repeated size options.
- Downloaded the ten ANI files attached to issue #5 into a temporary directory.
  Independently decoded their indexed BMP payloads and compared the resulting
  Xcursor pixels, dimensions, hotspots, and delays: all 150 size/frame combinations
  matched, with no truncation warnings.
- Verified that `--size standard` creates all seven requested sizes for a theme
  configured through an INF file. Existing manifest precedence is documented.
- Validated the Chinese gettext catalog and built and ran the local test suite.

The sample checks validate conversion output. They do not reproduce a KDE/Wayland
desktop session or establish how each compositor selects and displays sizes.
