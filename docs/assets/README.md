# Screenshots

The PNG files in this directory are documentation only: they illustrate the committed fixture pages
for the top-level README. They are not test assertions and their exact appearance depends on the
fonts installed on the rendering machine.

They were captured headlessly and converted with ImageMagick (`convert -strip` removes metadata):

```sh
./build/toybrowser --size 900x620 --screenshot nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html"
./build/toybrowser --size 900x620 --screenshot cjk.ppm \
    "file://$PWD/tests/fixtures/cjk_site/index.html"
convert -strip nav.ppm docs/assets/screenshot.png
convert -strip cjk.ppm docs/assets/chinese.png
rm nav.ppm cjk.ppm
```

Regenerate them when the fixture pages or the default rendering change noticeably; keep the images
small and keep the README captions accurate.
