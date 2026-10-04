# BMS lane-profile fixtures

These synthetic files exercise the complete built-in BMS-family lane catalog. Each file places
one visible `WAV01` object on every semantic lane of the explicitly named profile. Import tests
must select that profile, materialize exactly one tap per lane, retain the profile in provenance,
and publish no parse errors.

The free-zone/foot-pedal pairs deliberately use the same raw channel in separate fixtures: the
selected profile, not the channel number alone, owns that semantic role. Likewise, the PMS
BME-style fixtures deliberately overlap BME channel families while retaining button semantics.

| Fixture | Explicit profile ID | Expected lanes |
| --- | --- | ---: |
| `bms-5k-standard.bms` | `bms-5k-outside-scratch` | 6 |
| `bms-5k-free-zone.bms` | `bms-5k-outside-scratch-free-zone` | 7 |
| `bms-5k-foot-pedal.bms` | `bms-5k-outside-scratch-foot-pedal` | 7 |
| `bme-7k.bme` | `bme-7k-outside-scratch` | 8 |
| `bme-7k-foot-pedal.bme` | `bme-7k-outside-scratch-foot-pedal` | 9 |
| `bms-10k-standard.bms` | `bms-10k-left-outside-right-outside` | 12 |
| `bms-10k-free-zone.bms` | `bms-10k-left-outside-right-outside-free-zone` | 14 |
| `bms-10k-foot-pedal.bms` | `bms-10k-left-outside-right-outside-foot-pedal` | 14 |
| `bms-10k-scratchless.bms` | `bms-10k-scratchless` | 10 |
| `bme-14k.bme` | `bme-14k-left-outside-right-outside` | 16 |
| `bme-14k-foot-pedal.bme` | `bme-14k-left-outside-right-outside-foot-pedal` | 18 |
| `pms-5button.pms` | `pms-standard-5button` | 5 |
| `pms-9button.pms` | `pms-standard-9button` | 9 |
| `pms-bme-9button.pms` | `pms-bme-style-9button` | 9 |
| `pms-bme-18button.pms` | `pms-bme-style-18button` | 18 |
| `double-scratch-fpp.bms` | `bms-double-scratch-fpp` | 9 |
| `octave-foot-pedal.bms` | `bms-octave-foot-pedal` | 16 |
| `project2dx-7k.bms` | `project2dx-7k` | 8 |

These fixtures prove explicit semantic import through Wormedit's supported profile catalog. Dense
standard BMS DP5, BME 7K/14K and PMS 5/9-button maps also pin conservative automatic selection when one
complete observed channel family, extension, and `#PLAYER` evidence separates the profile from
compatible supersets. PMS 18-button additionally pins `.pms` plus `#PLAYER 3`, and the octave case
pins the named `#OCT/FP` header. The parser exposes the full ranked detection result in every case.
Raw-identical free-zone/foot-pedal maps, BME 7K-foot-pedal versus BME-style PMS 9-button, and unnamed
special double-scratch/Project2DX maps remain lossless and unmaterialized until a profile is chosen.
