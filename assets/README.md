# assets/

The HUD renders with SDL2_ttf. It searches a list of common system font paths
(see `kFontCandidates` in `src/hud.cpp`), including this directory:

```
assets/DejaVuSansMono.ttf
```

On macOS the HUD normally finds `/System/Library/Fonts/Supplemental/Arial.ttf`
automatically, so you don't need to add anything. On a minimal Linux box without
system fonts, drop a monospace TTF here named `DejaVuSansMono.ttf` and the HUD
will use it. If no font is found the game still runs — only the text overlay is
disabled (paddles, ball, and score rects still draw).
