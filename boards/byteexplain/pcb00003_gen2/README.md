# PCB00003 Gen2 (REV11 / BOM REV9)

**Hardware gen:** Gen2 · **Module:** Kaga **ES4L15BA1** → Nordic **nRF54L15** · **Battery:** LP260820 30 mAh

| Item | Value |
|------|--------|
| Board target | `pcb00003_gen2` |
| Bring-up branch | `feature/gen2-nrf54l15` |
| Gen1 board (do not break) | `pcb00003` / nRF52832 |
| Status | **Scaffold only** — DTS pinmux **not locked** |

**Docs:**  
- [GEN1_GEN2_TRACKING.md](../../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md) (timeline + checklist)  
- [PCB00003_GEN2_REV11_HARDWARE.md](../../../cursor_oralable/docs/PCB00003_GEN2_REV11_HARDWARE.md)  
- [HARDWARE_ROADMAP_nRF54L15.md](../../docs/HARDWARE_ROADMAP_nRF54L15.md)  
- [GEN2_GIT_WORKFLOW.md](../../docs/GEN2_GIT_WORKFLOW.md)

## Build (when NCS + pinmux ready)

```bash
# From oralable_nrf workspace with nRF54L15 support
west build -b pcb00003_gen2 -d build_pcb00003_gen2 app --sysbuild
nrfjprog --program build_pcb00003_gen2/merged.hex --sectorerase --verify --reset
```

Artifact naming: `artifacts/oralable_2.0.0_pcb00003_gen2_merged.hex`

## Pinmux

Nets are confirmed (`SDA`, `SCL`, `INT_ACC`, `INT_OPT`, `SENS_EN`, `CHRSTS`, `BATEN`, `BATVOL`).  
**GPIO numbers are TBD** until Altium netlist or first-board continuity — see hardware doc §5. Do not treat `pcb00003_gen2.dts` placeholders as production truth.

## Phase gates

See tracking board **G2-P0 … G2-P6**. This stub unlocks **G2-P0** scaffolding only.
