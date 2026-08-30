# Gen2 git workflow (oralable_nrf)

**Policy:** Multi-board single repo — **do not fork**.  
**Tracking:** [cursor_oralable/docs/GEN1_GEN2_TRACKING.md](../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md)

---

## Branches

| Branch | Role |
|--------|------|
| `known-good-battery-ble` | **Gen1 production** (pilot 1.0.x). Protect `pcb00003` builds. |
| `feature/gen2-nrf54l15` | **Gen2 bring-up** until G2-P3. Board stub + NCS port. |
| `main` | Mirror / PR target as agreed; do not abandon Gen1 protection. |

### Create / update Gen2 branch

```bash
cd oralable_nrf
git fetch origin
git checkout known-good-battery-ble
git checkout -B feature/gen2-nrf54l15
# work on boards/byteexplain/pcb00003_gen2/ …
git push -u origin feature/gen2-nrf54l15
```

### Merge rule

- Before **G2-P3**: keep Gen2 on `feature/gen2-nrf54l15`.
- At **G2-P3** pass: PR into Gen1 production branch that **builds both**:
  - `west build -b pcb00003 …`
  - `west build -b pcb00003_gen2 …`
- Never merge a Gen2-only change that breaks Gen1.

### Tags

```bash
# Gen1 release
git tag -a v1.0.70 -m "Gen1: STAT blink ship …"
# Gen2 phase gate
git tag -a v2.0.0-gen2-g2p0 -m "Gen2 G2-P0: blink+SWD on REV11"
```

### Artifacts

See [artifacts/README.md](../artifacts/README.md).

```
oralable_<ver>_pcb00003_merged.hex           # Gen1
oralable_<ver>_pcb00003_gen2_merged.hex      # Gen2
```

### VERSION strings

| Board | File | Example GATT `006` |
|-------|------|--------------------|
| Gen1 | `app/VERSION` | `1.0.70` |
| Gen2 | `boards/.../pcb00003_gen2/VERSION.gen2` | `2.0.0-gen2-nrfconnect` |

iOS firmware gate: Gen1 min `1.0.63` / recommend `1.0.84` (app **4.3.3** build **5**); add Gen2 `2.0.x` when G2-P2 ships.

### Status helper

```bash
./scripts/gen2_status.sh
```
