---
type: Paper
title: "Virtual Analog Buchla 259 Wavefolder"
description: "Circuit-model and aliasing research context for DisDorktion's independently designed triangle folder."
timestamp: 2026-10-11T00:00:00Z
authors: ["Fabián Esqueda", "Henri Pöntynen", "Vesa Välimäki", "Julian D. Parker"]
published: 2017-09-05
tags: ["audio-dsp", "wave-folding", "virtual-analog", "aliasing"]
resource: "https://dafx.de/paper-archive/2017/papers/DAFx17_paper_82.pdf"
---

# Virtual Analog Buchla 259 Wavefolder

Esqueda, Pöntynen, Välimäki, and Parker. Proceedings of DAFx-17, Edinburgh, 5–9 September 2017, pp. 192–199. [Full paper](https://dafx.de/paper-archive/2017/papers/DAFx17_paper_82.pdf).

## Findings and limitations

The paper derives memoryless branch mappings from a Buchla circuit with five parallel folding cells and a direct path. Their combination forms a piecewise linear transfer followed by filtering. It evaluates agreement with SPICE and harmonic behavior for sinusoidal input.

Fold corners introduce slope discontinuities and aliasing. The proposed correction uses polyBLAMP with oversampling; crossing positions and slopes are derived for known sinusoidal inputs. That assumption needs further work for arbitrary recorded audio or changing controls.

## Relevance to DisDorktion

The [wave-folding method](../methods/wave_folding.md) is an independently specified periodic triangle function. This source supports studying fold corners and aliasing, rather than claiming Buchla emulation or adopting its measured improvements. Q1 must evaluate suitable treatment for general plugin inputs. Paper simulations do not establish this project's processing cost, threshold range, native-rate sound acceptance, or oversampling adequacy.

Related: [project](../projects/disdorktion.md), [wave-folder validation](../notes/wave_folder_validation.md).

## Citation

```bibtex
@inproceedings{esqueda2017buchla,
  title = {Virtual Analog Buchla 259 Wavefolder},
  author = {Esqueda, Fabián and Pöntynen, Henri and Välimäki, Vesa and Parker, Julian D.},
  booktitle = {Proceedings of the 20th International Conference on Digital Audio Effects (DAFx-17)},
  pages = {192--199},
  year = {2017},
  address = {Edinburgh, UK}
}
```
