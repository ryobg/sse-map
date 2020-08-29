# sse-maptrack
Track on overlayed map, a mod for Skyrim: https://www.nexusmods.com/skyrimspecialedition/mods/26226

This project includes resources based on artists work available on https://game-icons.net. See
`assets\Data\SKSE\Plugins\sse-maptrack\icons-license.txt`

## Mystery notes

```
for f in *.svg; do convert -density 1024 -resize 62x62 -background none $f ${f::-3}png; done

montage *.png -geometry 64x64\>+0+0 -tile 32x -background none all.png

for f in ./vanilla/*.png; do compare -metric AE $f ./?/`basename $f` null: && echo " ./?/`basename $f`"; done > out.txt 2>&1

cat out.txt | grep ^0 | cut -d' ' -f2 | xargs rm
```
