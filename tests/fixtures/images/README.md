# Image fixtures

Hand-generated, self-authored PNG and JPEG files used by the renderer and the
shell tests. They contain no third-party data and are small enough to review
pixel by pixel. Each file is checked in as a fixture so tests need no encoder,
no network and no installed image library.

## PNG fixtures

The generator is committed next to the fixtures as `generate.py` (Python
standard library only); running it rewrites the PNGs.

| File | Format | Pixel content |
| --- | --- | --- |
| `flag_16x8.png` | 8-bit RGB, non-interlaced | Columns 0-7 red `#ff0000`, columns 8-15 blue `#0000ff`; all rows identical |
| `alpha_8x8.png` | 8-bit RGBA, non-interlaced | Columns 0-3 red `#ff0000` with alpha 128, columns 4-7 opaque green `#00ff00` |
| `gray_4x4.png` | 8-bit grayscale, non-interlaced | Every row is `0, 64, 128, 255` (black to white) |
| `palette_4x2.png` | 8-bit palette (3 entries), non-interlaced | Row 0 indices `0,1,2,0`; row 1 indices `2,1,0,2`; palette red, green, blue |

Tests address pixels with the documented values instead of comparing whole files,
so a fixture may be regenerated with the same content if the format ever needs
fixing. The generator reproduces the committed files byte for byte here; on another
machine the exact PNG bytes may differ with the Python/zlib version, so compare the
documented pixel content instead. Keep the dimensions and pixel values above in
sync with the tests and the generator.

`python3 tests/fixtures/images/generate.py` regenerates the PNGs.

## JPEG fixtures

These fixtures exercise `render::DecodeJpeg` and the content-based
`render::DecodeImage`. The generator is committed next to them as
`generate_jpeg.cpp` and uses the approved libjpeg (libjpeg-turbo) dependency; it
is a development tool and not part of the CMake build. Each quadrant below covers
`x = 0-7, y = 0-3` (top-left), `x = 8-15, y = 0-3` (top-right), `x = 0-7,
y = 4-7` (bottom-left) and `x = 8-15, y = 4-7` (bottom-right).

| File | Dimensions | Encoding | Expected decoded pixels (per channel, within tolerance) |
| --- | --- | --- | --- |
| `jpeg_rgb_16x8.jpg` | 16x8 | baseline, quality 100, 1x1 sampling | quadrants red `(255,0,0)`, green `(0,255,0)`, blue `(0,0,255)`, white `(255,255,255)`; alpha 255 |
| `jpeg_gray_16x8.jpg` | 16x8 | grayscale baseline, quality 100 | quadrants gray `0`, `64`, `128`, `255` in all three RGB channels; alpha 255 |
| `jpeg_progressive_16x8.jpg` | 16x8 | progressive, quality 100, 1x1 sampling | same quadrants as `jpeg_rgb_16x8.jpg`; alpha 255 |
| `jpeg_cmyk_16x8.jpg` | 16x8 | Adobe CMYK baseline, quality 100, 1x1 sampling | after CMYK to RGB conversion: red, green, blue, black; alpha 255 |
| `jpeg_too_wide_9000x1.jpg` | 9000x1 | baseline, quality 100, 1x1 sampling | never decoded: rejected by the 8192 px per-side bound before allocation |
| `jpeg_truncated_16x8.jpg` | 16x8 | baseline header + the first half of the entropy-coded scan of `jpeg_rgb_16x8.jpg` | never decoded: rejected as truncated data |

Tolerances used by `tests/render/jpeg_test.cpp`: per-channel `+-4` for the RGB
and progressive fixtures, `+-2` for the grayscale fixture, and `+-8` for the
CMYK fixture (`+-` is relative to the documented value above). All files decode
to straight-alpha ARGB with alpha 255; JPEG has no alpha channel.

Notes that the tests and the decoder rely on:

- The CMYK fixture stores Adobe-style inverted CMYK samples (`255` means no ink);
  libjpeg-turbo's libjpeg API cannot convert CMYK/YCCK to RGB itself, so
  `DecodeJpeg` decodes those streams to `JCS_CMYK` and converts each sample with
  the conventional `sample * black / 255` formula (the inverse of the standard
  RGB to CMYK conversion), which is why the fixture shows red, green, blue and
  black.
- libjpeg reports truncation only as a warning: after a premature end of input it
  fills the missing samples with gray rather than failing. `DecodeJpeg` promotes
  the truncation warnings (`Premature end of JPEG file` and `Corrupt JPEG data:
  premature end of data segment`) to decode errors so a partial image is never
  returned as complete, while benign warnings (for example extraneous bytes
  before a marker) are tolerated silently.
- EXIF orientation and other metadata are ignored; pixels are decoded as stored.
- JPEG is lossy and the exact bytes depend on the libjpeg version, and the
  decoded channels may round slightly differently on another machine. The tests
  therefore compare decoded pixels within the documented tolerances and never
  compare whole files. Keep this table, the tolerances and
  `tests/render/jpeg_test.cpp` in sync with the generator.

Compile and run the generator from the repository root (`.tmp/` is gitignored
scratch space):

```sh
mkdir -p .tmp
g++ -std=c++20 -O2 -Wall -Wextra tests/fixtures/images/generate_jpeg.cpp -o .tmp/generate_jpeg -ljpeg
.tmp/generate_jpeg tests/fixtures/images
```

The generator writes every JPEG fixture listed above, including the truncated
copy, which it derives from the just-written `jpeg_rgb_16x8.jpg`.
