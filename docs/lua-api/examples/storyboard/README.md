# Sunshine storyboard

Save a new four-key project in a copy of this complete directory. In Compose's
**Lua note generator** inspector choose `project.lua`, **Run**, inspect the chart/storyboard
diff, then **Apply**. Open the player preview and seek through 0–4000 ms.
Select the generated Sunshine difficulty. Save and reopen the `.wormedit` project
to retain the scene and recipe.

OSBX images resolve relative to the saved project directory without requiring
prior image imports. Preview decoding does not change the project resource catalog.

`project.lua` includes a small chart and the exportable sky, sun/rays and worm
scene. The worm's offset rotating parent produces a baked orbit on osu! export.
`banner-project.lua` also includes the sheared WORMEDIT banner: preview supports
it, but osu! export must refuse the whole output set and name `banner/op/2`.
Choose one entry point, rather than applying both recipes to the same project.

All artwork is original geometric vector artwork for this example. Editable SVG
sources and rendered PNGs live together in `assets/`; Lua references the PNGs.
The test `lua.OSBX example survives generator native preview and osu export`
exercises this directory directly, including both negative paths.
