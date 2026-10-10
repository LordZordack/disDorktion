# Generated experiment examples

The five JSON files use the complete version 1 [experiment schema](../../docs/AUDITION_HARNESS.md): impulse, sine, two-tone, logarithmic sweep, and seeded white noise. Each uses reference gain at unity, 48 kHz stereo, 256-frame processing, 48,000 source frames, and 48,000 render frames. They are settings examples; `buildIdentity: "example"` makes that provenance explicit and observations make no measured/listening claim.

Render from the repository root after building Release:

```powershell
New-Item -ItemType Directory -Force -Path '.\renders'
& '.\build\windows-vs2026\Release\disdorktion_render.exe' `
  --experiment '.\examples\experiments\sweep.json' `
  --output-dir '.\renders\sweep-001'
```

Choose a new output directory on every run. The renderer preserves the example record in `result.json` and adds its actual `rendererBuildIdentity`. To restore these in the GUI, match 48 kHz, stereo, and a 256-frame device block first. Source frames and render frames are independent; a longer generated render contains silence after the source ends. For file experiments, save a successfully loaded WAV/AIFF in the app to obtain the absolute path, actual hash, and original format.
