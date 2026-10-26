# SpinePreview app icon

Concept: an articulated S-shaped rig with three joints and a play symbol on a blue macOS tile. No text or version badge, so both runtime applications share the same icon.

- `source.png`: original AI-generated image, with alpha transparency (1254×1254).
- `AppIcon.png`: normalized 1024×1024 PNG.
- `AppIcon.icns`: macOS icon with 16, 32, 128, 256 and 512-point sizes, each at 1× and 2× resolution.

Generated with the built-in image_gen tool. PNG resampling and ICNS packaging use macOS `sips` and `iconutil`. Normal application builds consume the checked-in ICNS directly. Regenerate locally with `bash scripts/build-icon.sh`; no network or image-generation API is needed for this step.

Original generation prompt:

> Use case: logo-brand. Asset type: production macOS application icon for SpinePreview, a lightweight skeletal-animation preview tool. Generate ONE finished square app icon, not a presentation sheet or mockup. Subject: a bold original abstract skeletal rig made of three connected tapered bone segments and round articulation nodes, arranged as a flowing S-like gesture, with a small clear right-facing play triangle integrated into the negative space. Make the skeletal animation idea immediately legible, not a medical spine illustration. Style: refined contemporary macOS desktop icon, precise simple sculpted shapes, subtly dimensional matte ceramic and frosted-glass finish, restrained highlights, generous clear silhouette, readable at 32px. Composition: straight-on orthographic centered rounded-square tile, large central symbol, clean balanced optical spacing, tile approximately 88 percent of square canvas with proper transparent margin outside the rounded corners and a very restrained contact shadow. Palette: dark navy and cool blue related to the project's dark-blue interface, bright high-contrast central bone symbol. No letters, no words, no numbers, no version badge, no macOS device mockup, no wallpaper, no multiple options, no watermark. Do not imitate the official Spine logo or any existing app icon. Produce a 1024 by 1024 PNG with real alpha transparency outside the icon tile.
