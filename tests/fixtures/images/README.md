# PNG image fixtures

Hand-generated, self-authored PNG files used by the renderer and the shell tests.
They contain no third-party data and are small enough to review pixel by pixel.
Each file was produced once by a throwaway script in `.tmp/` (never committed) and
is checked in as a fixture so tests need no encoder, no network and no installed
image library.

| File | Format | Pixel content |
| --- | --- | --- |
| `flag_16x8.png` | 8-bit RGB, non-interlaced | Columns 0-7 red `#ff0000`, columns 8-15 blue `#0000ff`; all rows identical |
| `alpha_8x8.png` | 8-bit RGBA, non-interlaced | Columns 0-3 red `#ff0000` with alpha 128, columns 4-7 opaque green `#00ff00` |
| `gray_4x4.png` | 8-bit grayscale, non-interlaced | Every row is `0, 64, 128, 255` (black to white) |
| `palette_4x2.png` | 8-bit palette (3 entries), non-interlaced | Row 0 indices `0,1,2,0`; row 1 indices `2,1,0,2`; palette red, green, blue |

Tests address pixels with the documented values instead of comparing whole files,
so a fixture may be regenerated with the same content if the format ever needs
fixing. Keep the dimensions and pixel values above in sync with the tests.
