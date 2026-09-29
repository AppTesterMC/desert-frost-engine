# Battles, troop marches, espionage, worms and the final attack: implementation spec

Status: written 2026-09-25 from binary evidence, for the ScummVM `dune` engine. It covers what the speedrun in `route.md` needs next and is ordered the same way.

## 0. Sources, conventions and caveats

**Sources (all local):**
- `/tmp/seg000.txt`: a capstone disassembly of CD `DNCDPRG.EXE` v3.7, SHA-1 `c55e9e35…afcb`. Addresses are `seg000:XXXX`. Near calls print as `0xffffXXXX`; read them as `XXXX`.
- madmoose's chani annotations, (github.com/madmoose/dune-chani, `cryo-dune-3.7-cd-dncdprg.chani`). They supply the routine names, the `Troop` and `Location` layouts and the ds variable names. Some chani comments are wrong; where the code disagrees, this spec says so.
- Jump and callback tables were decoded straight from the CD's `DNCDPRG.EXE` (same hash): seg000 is the file at `+0x200`, and seg001 starts at `+0x200+0xf4b0`.
- OpenRakis `tools/cd/DuneEdit2/…/Parsers/JobFinder.cs` gives the occupation names. Lionel Debroux's `odrade` (`odrade.go`, `troop.go`) gives the equipment bits and status bits and warns that army skill above 207 breaks battles, which agrees with §3.3. dune-rust and swift-dune only have the night-attack particle effect, which our `attack.cpp` already ports. The `dune` repository has no gameplay logic.
- Engine baseline, read-only: a copy of the engine taken on 2026-09-25 (`engines/dune`).

**Version caveat.** The route was played on the **floppy** release, but every address here comes from the **CD 3.7** executable. Data offsets below `ds:1158` are the same in both (`World::ds()`). Before relying on a formula for exact reproduction, check it against the floppy code.

**Marking.** A claim marked **[inferred]** is a reading of intent or data that the code does not show directly. Everything else was read from the instructions.

### 0.1 Record layouts (raw data-segment offsets)

The engine keeps the data segment in `GameState::vars`, so the raw offsets below can be used directly through `troopByte()` and `locationByte()`.

**Troop** (27 bytes each, at `ds:08aa + 27*(id-1)`, 68 records; the loop stops at `ds:0fbb`):

| off | field | meaning in this spec |
|---|---|---|
| +00 | id | 1-based |
| +01 | next | next troop id in the location chain |
| +02 | position | slot 1..30 at the location (Harkonnen bank starts at 9) |
| +03 | occupation | low nibble = job; 0x10 stopped; 0x20 captured/defeated; 0x40 moving; 0x80 not hired |
| +04 | location (u16) | ds offset of its Location; **while moving = the destination** |
| +06/+08 | gps lng/lat (s16) | current position while moving |
| +0a | time (u16) | `game_time` when the occupation (re)started |
| +0c | depC (u16) | espionage: Harkonnen troops seen; attacking: Harkonnen killed; training: countdown |
| +0e | depE (u16) | espionage: strength per Harkonnen troop; attacking: Fremen killed |
| +10 | bits10 (u16) | 0x80 **Harkonnen troop**; 0x10 hidden from the map (Harkonnen); 0x40 espionage report done; 0x400 has fought at a fortress; 0x20 converting a fortress; 0x800 trained with Gurney |
| +12 | speech (u16) | 0x10/0x20 refusing to work; 0x1000 turned a fortress into a sietch |
| +14 | day rallied | |
| +15 | motivation | 0..100 |
| +16/+17/+18 | spice, army, ecology skill | cap 0x5f |
| +19 | equipment | bit7 harvester, 6 ornithopter, 5 krys knives, 4 laser guns, 3 weirding modules, **2 atomics**, 1 bulbs |
| +1a | population | men / 10 |

Occupation low nibble (table `cs:6c26`, decoded from the EXE; names from OpenRakis JobFinder): 0 spice mining (`6fe5`), 1 prospecting (`70cc`), 2 waiting, 3 spice troop searching for equipment, **4 military training (`71ef`)**, **5 espionage (`72b0`)**, **6 attacking (`739e`)**, 7 army troop searching for equipment, 8 irrigation, 9 wind trap, 0x0a bulbs. Harkonnen troops use 0x8c–0x8f; raiders are 0x8d. The byte 0xa0 marks a freed or slaved Fremen troop that is not hired; 0x22 marks a captured Fremen troop ("apologizes for being captured").

**Location** (28 bytes each, at `ds:0100 + 28*i`, terminated by first byte 0xff):

| off | field | meaning |
|---|---|---|
| +00/+01 | first/last name | first = region (1 Arrakeen, 2 Carthag, …) |
| +02/+04 | map x/y | |
| +08 | appearance | < 0x20 sietch, 0x20 Atreides palace, 0x21–27 village, **0x28–2f fortress**, **0x30 Harkonnen palace** |
| +09 | troop | head of the troop chain |
| +0a | status | 0x80 hidden; 0x40 prospected; 0x20 wind trap; 0x10 inventory seen; **0x08 held by the Atreides (conquered, not yet converted)**; 0x04 saboteurs; **0x02 battle (only set by Harkonnen raids)**; 0x01 vegetation |
| +0b | phase/radius/day | multi-use: discover phase; **the day a conquered fortress becomes a sietch**; the paint radius for `644e` (engine `kDiscRadius`) |
| +14..+1a | equipment counts | harvesters, ornithopters, krys knives, laser guns, weirding modules, **atomics (+19)**, bulbs. These counts **include the items that troops there are holding**. |

Location 0 (`ds:0100`) is the Atreides palace (Carthag). **Location 1 (`ds:011c`) is the Harkonnen palace (Arrakeen, appearance 0x30).** The chani comment at `205d` wrongly calls `0x11c` the Atreides palace. Locations 2, 3 and 4 (`ds:0138`, `0154`, `0170`) are Arrakeen fortresses at the start; they are the three places next to the palace that the final attack uses.

### 0.2 Time and random numbers

- `game_time` is at `ds:0002`. It has 16 periods per day: `day = time >> 4` and `slot = time & 15` (`1ac5`, `1ae0`). A period lasts about 60 s of real time (`World::kPeriodMillis`). Each period runs `run_events_for_current_time_period` (`1b23`):
  1. the new-day hook (`1c46`);
  2. **if `ds:c2 < 7`**: the troop-occupation walk (`6c6f`), the new-day location walk (`63f0`) and the time-of-day action (table `cs:1db3`: slot 3 shipments `20a4`, **slot 4 Harkonnen raids `1f64`**, slot 8 reminder `1dda`, slot 15 Harkonnen growth `1d10`);
  3. re-staging of the current location for CONDIT (`331e`);
  4. **`night_attack_period_step` (`1bec`)**.
- `rand` (`e3cc`) is an LCG: `s = s*0xcbd1 + 1` (low 16 bits) at `ds:d826`. It returns `al` = bits 8–15 of the new state and `ah` = bits 16–23 of the product. `rand_masked(bx)` (`e3b7`) is `s' = s*0xe56d + 1` at `ds:d824`, returning `(al,ah) & bx` from the same byte selection. Both are seeded from the BIOS tick count `0:046c` at startup (`00c3`). The rolling word `ds:0000` is reset to `rand()` on every game-loop pass (`d84b`) and on some screen changes (`dda0`); the code tests it with `rol`/`test`.
- **Consequence:** the random state lives outside the saved block (the saves cover `ds:0..0x1260`), so reloading a save rerolls every battle. The route depends on this ("saves are loaded again and again until the kill count rises"). Our `World::rollRandom()` keeps an LFSR in `ds:0`, which *is* saved, so a reload would replay the same battle. **Engine action:** add `World::rand()` and `World::randMasked()` as the two LCGs, with state that is not saved and is seeded from `g_system->getMillis()`. Also refresh `ds:0 = rand()` every frame in `GameScreen::update()`, and use these in all the code below.

### 0.3 Iterating troops at a location

- `call_callback_on_all_troops_in_location` (`6603`) walks the chain from `loc+09` through `troop+01`. It calls the callback with **carry set when the troop is hired** (occupation < 0x80).
- `call_callback_on_hired_troops_in_location` (`661d`) does the same walk but calls only hired troops.
- `location_do_accumulation…` (`5098`, callback `5082`) is `countHostiles`, which we already have. It returns **cx = live Harkonnen troops** (bits10 & 0x80, occupation not & 0x20) and **dx = live Fremen troops with occupation exactly 6**.
- `location_is_Atreides` (`5d36`, our `friendlyPlace`) returns true for appearance < 0x28, or for status & 0x08.

---

## 1. Espionage: revealing forts (route items 19 and 21)

### 1.1 Menu

`menu_callback_choice_map_troop_dialogue_change_troop_occupation` (`69b3`) picks the menu from `(occ & 0xf) >> 2`:

- **Army troop (class 1)** gets menu `ds:2182`: GO & SEARCH FOR EQUIPMENT (`7734`), **ESPIONAGE (`6a45`)**, SPECIALIZE IN SPICE, SPECIALIZE IN ECOLOGY, Cancel.
  - ESPIONAGE is **greyed unless `ds:e2 < 0x1e`**, where `ds:e2` is the distance from the staged location to the nearest *hidden* place with appearance ≥ 0x28, computed by `5274`.
  - GO & SEARCH is greyed while `game_phase < 0x10`.
  - Our `drawTroop()` currently offers "Military Training / Espionage", which is wrong. Rebuild it from these records.
- **Army troop already on espionage (nibble 5)** gets menu `ds:219a`: **ATTACK (`6a2f`)** and Cancel.

### 1.2 Choosing ESPIONAGE (`6a45`)

1. Set occupation to `(occ & 0x0c) | 1`, which is 5, through `troop_apply_occupation_choice` (`6a89`). The troop says a line with action 0x0a, and a refusal event restores the old occupation.
2. Set `ds:46d8 = 1` (purpose unknown). Clear bits10 0x40 (the report-done flag).
3. `331e` stages **the troop's own location**, which recomputes `ds:e4` = **the nearest hidden fortress or palace**, measured with $d=\max(\lvert\Delta x\rvert \gg 8,\ \lvert\Delta y\rvert)$ (`5274`).
4. The troop acknowledges the move to `ds:e4` (`82da`). If it does not refuse, the contact closes (`8770`) and the troop is ordered to march to `ds:e4` (`troop_issue_move_order` `84a6`, §2).

So espionage sends the troop **automatically to the nearest undiscovered fortress within 30 cells**. The player does not pick the fort.

### 1.3 Arrival reveals the fort (`8357`)

When the marching troop arrives at a hostile place (not friendly and not battle-flagged), it settles there. **If its job is 5 and the place is hidden (status & 0x80), the arrival clears 0x80 and marks the map dirty (`5d44`).** This is "Espionage reveals Fort Bledan-Harg".

If the place was already revealed, a job-5 arrival instead falls through to `83fd`: every hired troop there switches to job 6, so an attack starts (§3.1).

### 1.4 Each period at the fort (`72b0`, job 5)

Let $s$ = army skill (+17) and $e$ = `game_time − troop+0a` (periods on the job).

1. **Count.** If bits10 & 0x40 is clear and $e > \lfloor (80-s)/2 \rfloor \gg 1$ (that is, about $e > (80-s)/4$):
   - If `depC == 0`, call `68d2`. For every troop at the fort with bits10 & 0x10 (a hidden Harkonnen troop), it clears 0x10, respawns the map icon and counts it. Store the count in `depC`. **A count of 0 sets 0x40 immediately** ("nothing here").
2. **Report.** If $e > \lfloor (80-s)/2 \rfloor$: call `33be` (the forces, §3.3), then set `depE = ds:94 / depC` (Harkonnen strength per troop) and bits10 |= 0x40.
3. **Exposure, every period, even after the report.** If the troop is **not** the chain head (`troop.id != loc+09`), $e > \lfloor s/2 \rfloor$, and `(rand() & 0x3f) >= s`, then the troop is **captured** (`668f`):
   - occupation |= 0x20;
   - equipment = 0, **without** unregistering it, so the items stay in the fort's stock;
   - `+0a = game_time`;
   - charisma −4 through `6fb0`, which also lowers the motivation of every troop.

   For $s<64$ that is $P=(64-s)/64$ per period. For $s\ge 64$ it never happens. A fort's own Harkonnen troops are the chain head **[inferred from the initial data, where every fort has a troop]**, so exposure applies in practice.

The troop dialogue reads `ds:44` (depC) and `ds:46` (depE) through `troop_prepare_troop_data_for_condit` (`31f6`).

### 1.5 Other ways forts are found

- Flying over or arriving at a fort (`location_mark_discovered` `425b`; we already have `markDiscovered`).
- **Harkonnen captain.** The captain is a defeated Harkonnen troop (occupation & 0x20) at a fortress. `316e` classifies it into room 3 (base room 2, +1 for "captured at a fortress"). In its dialogue (`932e`), if `depC == 0` and `ds:e2 < 0x1e`, then `depC = ds:e4` and that fort is staged for CONDIT. The engine's speaker-12 event, `revealPointedPlace(0x11ce)`, then reveals it.
  - Threatening the captain (`9590`) takes 0x29 from his motivation each time. When `ds:ed` goes negative he is overpowered: occupation |= 0x10 and room-person 12's flag |= 0x10.

### 1.6 Engine mapping

- `World::espionageTick(uint id, uint loc)` implements `72b0`. Call it from `World::runPeriod()` for `case 5`.
- `World::revealHarkonnenTroops(uint loc)` implements `68d2` and returns the count.
- `World::nearestHiddenHarkonnen(uint fromLoc, uint &dist)` implements the `ds:e2/e4` part of `5274`. Check whether `stageLocationForConditions()` already writes `ds:e2..e6`; if it does, reuse it.
- `GameScreen::drawTroop()` needs the real army menu, the espionage grey rule and the ATTACK menu for job 5. The `kRowSetOccupation` handler for espionage then calls `World::issueMoveOrder(id, nearestHidden)`.

---

## 2. Troop move orders and travel time

### 2.1 Orders

- **MOVE TROOP** (`menu_map_troop_dialog` `ds:2118`, handler `8064`, pick `81ec`/`8256`): the player picks a visible map marker. For ordinary troops there is no range or type check. The prospector troop, `ds:08e0`, instead queues three sietch or Atreides destinations.
- **Done** (`8214`): the troop speaks the acknowledgement line (`82da`: action 0x0b, or 0x10 if the destination is where it already stands). If a dialogue event did not refuse, call `troop_issue_move_order(si, di)` (`84a6`).

Our engine greys MOVE TROOP today. Enable it: open `MapScreen` in pick mode and call `World::issueMoveOrder`.

### 2.2 `troop_issue_move_order` (`84a6`)

1. **Already moving** (occupation & 0x40): set `+04 = dest` (retarget). A fetch mission (low bits 11b) drops its low bits.
2. Otherwise:
   - Call `6ebf`: any hired troop still at the old place that was sulking (speech & 0x10) gets its job re-applied.
   - Unlink the troop from its old chain (`858c`).
   - **If the troop was job 6 at a battle-flagged place** and it was the last job-6 defender there (`5098`: dx−1 ≤ 0), the battle there is lost (`74b6`, §3.8).
   - **If the destination is a sietch or village** (appearance < 0x28):
     - if it is battle-flagged, **return**. The troop is left unlinked and not moving. This looks like an original bug; reproduce it or guard it and log it.
     - otherwise motivation −3 (`6f93`, floor 0; below 5 the troop sets motivation 4, stops and sulks, speech |= 0x20).
   - Swap `+04` to the destination. Occupation |= 0x40, position = 0. Unregister the troop's equipment from the old place (`7f75`), refresh its icon.
   - Unless bits10 & 0x10 (a hidden Harkonnen), **take 7 travel sub-steps immediately** (`jmp 8313` with cx = 7).

### 2.3 Each period while moving (`troop_travel_step` `8308`, from `6c6f`)

The walk skips the troop that is currently selected on the map (`ds:1954`). Otherwise there are **4 sub-steps per period, or 8 if the troop carries an ornithopter (equipment bit 6)**.

Each sub-step (`8604`):
- Take the gaps to the destination: longitude in cells, $\lvert\Delta\text{lng}\rvert / u(\text{lat})$ with $u$ the per-row longitude units (`ds:4880`, built from TABLAT; our `rowCells`), and latitude in rows.
- **If the dominant gap is < 7, the step is zero, which means arrival.**
- Otherwise move one unit on the dominant axis. The minor axis moves one unit only when a `rol ds:0` carry is set (about 50 %). Once the dominant gap is 1 the sub-cell remainder is snapped.

**Travel time.** Let $D$ be the dominant distance in cells. Arrival is detected on the sub-step after the gap drops to 6, so the troop needs $D-5$ sub-steps; 7 of them happen at once when the order is given. That gives

$$
P_{\text{periods}} \approx \max\left(0,\ \left\lceil \frac{D-12}{k} \right\rceil\right),\qquad k=\begin{cases}4 & \text{on foot}\\ 8 & \text{with an ornithopter}\end{cases}
$$

One period is 1/16 day. The estimate ignores the random moves on the minor axis and the change of $u$ with latitude **[approximation, derived]**.

### 2.4 Arrival (`troop_arrive_at_destination` `8357`)

1. Snap gps to the destination's map x/y.
2. **Destination friendly and not battle-flagged:** a job 5 or 6 becomes job 4 (`6ac5`, bits 0–1 cleared), then settle. A fetch mission (low bits 11b) takes the turn-around at `841f`.
3. **Otherwise**, settle (`83bc`):
   - link into the chain (`851f`: Fremen take the first free slot from 1, Harkonnen from 9) and clear 0x40;
   - `85cc`: a Fremen troop that landed on slot > 8 evicts the first captured Harkonnen troop there, which leaves play;
   - register the troop's equipment into the place (`7f5f`) and re-apply its job (`6ad4`, which restarts the job clocks).

   Then:
   - job 5 → reveal (§1.3);
   - **the troop is now the chain head (no one else there) → the battle is won at once (`7429`, §3.7)**;
   - otherwise → `83fd`: **every hired troop there becomes job 6** (a prospector, job 1, just stops). This is how a march to a known fortress starts a battle (route item 31, "set to attack").

### 2.5 Engine mapping

Add a new file `engines/dune/troops.cpp` (list it in `module.mk`) with these `World` methods:

- `issueMoveOrder(id, loc)` implements `84a6`.
- `travelSubstep(id, int &dlng, int &dlat)` implements `8604`.
- `troopTravelStep(id)` implements `8308`.
- `troopArrive(id)` implements `8357`.
- `linkTroop(id, loc)` implements `851f` and `85cc`.
- `unlinkTroop(id)` implements `858c`.
- `registerEquipment(id, loc, int sign)` implements `7f5f` and `7f75`.

`runPeriod()` currently skips every troop with occupation & 0x40. Make it call `troopTravelStep` for them instead, matching `6c6f`: skip when speech & 0x430, handle a population below 20 with the merge `6d19`, skip occupation & 0xa0, send & 0x40 to travel, and otherwise use the job table. After the job callback, call `troop_usually_decrease_skills_every_4_days` (`6d7b`). `MapScreen` then needs moving troop icons (`troop_icon_pick_script_moving`, `6827`).

---

## 3. Fort battles

### 3.1 When a battle exists

A place is "in battle" (`location_has_battle` `627e`) when either:
- status & 0x02 is set (only Harkonnen raids set it, §5), or
- it is not friendly and at least one live Fremen troop there has job exactly 6.

Ways to start a fort battle:
- **ATTACK** from espionage (`6a2f`: occupation becomes `(occ & 0x0c) | 2`, which is 6);
- **marching** any hired troop to a revealed fort (§2.4);
- a job-5 arrival at an already revealed fort.

The fort's Harkonnen troops are job ≥ 0x80, so the walk skips them. **The whole fight runs inside the job-6 callbacks of the Fremen troops, one roll per attacking troop per period.** Each job-6 tick also sets `ds:11bc |= 1`, which suppresses the next Harkonnen raid.

### 3.2 Per-period resolution (`callback_troop_location_for_troop_occupation_attacking` `739e` → `73d9`)

This runs for every hired, non-moving Fremen troop with job 6. If the place is `ds:011c`, go to §6.4 instead.

1. `33be` computes the forces $H$ = `ds:94`, $F$ = `ds:96` and the balance $B$ = `ds:9c` (§3.3).
2. **If $H=0$: battle won** (`7429`, §3.7).
3. Roll $r$ = `rand()` & 0xff.
4. **If $r \ge B$ (the Harkonnens strike this troop, `751d`):**
   - $x = \lfloor H / n \rfloor$, where $n$ = `ds:60`, the Fremen troop count **of the location last staged for CONDIT** by `331e`, normally Paul's current place. If $n=0$ there is no division. This is a quirk: a battle far from Paul uses Paul's location's count **[consequence read from code; reproduce it]**.
   - Loss $L = \text{loss}(x, \text{this troop})$ (§3.3). `depE += L` (Fremen killed) and population −= $L$.
   - **If the population reaches 0:**
     - population = `(rand_masked(0x7f) & 0x7f) + 30`, so 30–157 units, meaning 300–1570 prisoners;
     - the troop is captured (`668f`: occupation |= 0x20, equipment 0 and left in the fort's stock, charisma −4);
     - then if `5098` finds no job-6 Fremen left, **the battle is lost** (`74b6`, §3.8).
5. **Else (this troop strikes, `73ef`):**
   - $c$ = live Harkonnen troops (`5098` cx) and $S$ = this troop's strength (§3.3).
   - $x=\min(255,\ \lfloor S/c\rfloor+1)$.
   - For **each live Harkonnen troop** at the place (`7552`): $L=\text{loss}(x,\text{that troop})$; population −= $L$; this troop's `depC += \sum L` (Harkonnens killed).
   - A Harkonnen troop that reaches 0:
     - occupation |= 0x20 and bits10 |= 0x10, so it becomes a defeated captive and a possible captain;
     - its icon is removed;
     - **with probability 1/4** (`rand_masked(3) == 0`) its atomics bit is cleared and its *other* items are unregistered from the fort. Net effect: its non-atomic gear is destroyed and its atomics stay in the fort's stock **[read from code; the chani comment "strip atomics" is misleading]**.
   - **If no live Harkonnen troop remains: battle won** (`7429`).

### 3.3 Formulas

**Motivation modifier $m$** (`6efd`, already ported as `motivationModifier`). Start from $m$ = motivation, add 20 if `ds:fa` is set, then:

| case | value of $m$ |
|---|---|
| job 6 **and** the troop is at Paul's current place (`ds:114e`) | $\min(m+30,100)$ |
| job 6 elsewhere | $m$, **not capped** (up to 120) |
| ecology jobs (8, 9) | 100 |
| any other job | $\min(m,100)$ |

When $0x64 \le$ `game_phase` $< 0x68$, the result becomes $\max(m-40, 10)$.

**Strength $S$ of a troop** (`342d`). With $a$ = army skill, $p$ = population (men/10) and equipment flags $k$ (krys knives, bit 5), $l$ (laser guns, 4), $w$ (weirding modules, 3), $t$ (atomics, 2):

$$
q=\min(255,\ 2m+a),\qquad b=\left\lfloor \frac{q\,p}{16}\right\rfloor,\qquad M=1+2k+4l+8w+16t
$$

$$
S=\min\left(255,\ \left\lfloor \frac{b\,M}{256}\right\rfloor\right),\qquad S\leftarrow 1\ \text{if}\ S=0\ \text{and}\ p\ge 1
$$

Harvesters, ornithopters and bulbs do not count.

**Forces** (`33be` via `3406`): over all troops at the place that are **not** occupation & 0x20, add $S$ to `ds:94` ($H$) if bits10 & 0x80 is set, otherwise to `ds:96` ($F$). The Fremen sum counts *every* Fremen troop there, not only the job-6 ones. The Fremen also OR their bits10 and speech words into `ds:5c`/`ds:5e`.

**Balance $B$** (`33d9`, byte `ds:9c`):

$$
B=\begin{cases}\min\left(252,\ \left\lfloor \dfrac{128F}{H}\right\rfloor\right) & F\ge H\\[2ex] 256-\min\left(252,\ \left\lfloor \dfrac{128H}{F}\right\rfloor\right) & F<H\end{cases}
$$

$B = 252$ when the smaller side is 0 or the ratio overflows, so $H=0$ gives 252 and $F=0$ gives 4. The Fremen win a roll with probability $B/256$. $B$ is above 0x80 when the Fremen are stronger, as the chani comment on `ds:9c` says.

**Loss inflicted on a troop with army skill $a$** (`758d`):

$$
\text{loss}(x,\cdot)=\min\left(p,\ \begin{cases}255 & x\,(255-2a)\ge 65536\\ \left\lfloor x\,(255-2a)/256 \right\rfloor & \text{otherwise}\end{cases}\right)
$$

The term $255-2a$ is computed in a byte, so it wraps when $a>127$. That matches odrade's warning about army skill above 207.

### 3.4 Paul at the battle (the "night attack" screen)

**`location_related_to_dying_if_arriving_at_fortress` (`503c`)** runs on every arrival (`arrive_at_location` `402e`) and on every period while `night_attack_stage` is non-zero (`1bec`):

1. Clear `ds:fd` and `ds:2b`.
2. If the place is battle-flagged, **or** it is hostile and at least one job-6 Fremen troop is there:
   - `ds:2b = 1`;
   - `ds:fd = gauge | 1` (`6144`);
   - pick the battle backdrop sprite into `ds:11dd`: 0x2f for a sietch, 0x30 for the Harkonnen palace, 0x33+(appearance−0x28) for a fortress.
3. Else if the place is hostile with live Harkonnens and no attackers: `ds:46d9 = 4`, **Paul is shot** (COMMAND 0xbe, "You know what?…"). Our `arrivalIsFatal()` already does this but ignores the battle case.

**Period step (`1bec`).** While `ds:2b` is set, run `503c` again. If a game over is pending (`ds:46d9` ≠ 0), **force it to 6** (COMMAND 0xc0; text not extracted, **[inferred: "Paul killed in battle"]**). When `ds:2b` has dropped to 0 (the battle is over), clear the attack effect (`b21`) and redraw the room.

**Menus** (`build_room_command_records` `2efb`). In room 1 of a place while `ds:2b` ≠ 0, the verbs are SEE DUNE MAP, **MASSIVE ATTACK (`7317`)**, **FIGHT FOR A WHOLE DAY (`0fc5`)** and **CALL A WORM (`42d1`, greyed while `game_phase < 0x4f`)**. There is no TAKE AN ORNITHOPTER. The scene draws the night-attack effect, `NightAttack` in `attack.cpp`.

**Effects of Paul's presence:**
- job-6 troops at Paul's place get +30 motivation in $m$, capped at 100 (§3.3);
- losing a battle at Paul's place kills him: `74b6` sets `ds:46d9 = 6` when the place is `ds:114e`.

**Dialogue gauge `ds:fd`** (`60f8`; "nearly routed", "very close" and "severe losses" in the Stilgar and Gurney lines). Let $P_F$ be the population of the uncaptured job-6 Fremen, $K_F$ = Σ depE, $P_H$ the Harkonnen population and $K_H$ = Σ depC. Then

$$
f=\frac{256\,P_F}{P_F+K_F},\qquad h=\frac{256\,P_H}{P_H+K_H},\qquad \text{fd}=\left(128+\frac{128\,(f-h)}{\max(f,h)}\right)\ \text{OR}\ 1
$$

using integer division. The map's "Battle:" panel draws sprite $0x8e + \lfloor(\text{gauge}+15)/32\rfloor$.

### 3.5 MASSIVE ATTACK (`7317`)

1. `ds:473a = 1`. Snapshot $H_0$, $F_0$ into `ds:98`/`ds:9a`.
2. **Roll once:** if `rand()` ≥ $B$ the Harkonnens strike (callback `7516`); otherwise the Fremen strike (callback `7419`).
3. Repeat **up to 16 times**, stopping early when $H=0$ or $F=0$:
   - recompute the forces (`33be`);
   - apply the callback to every troop at the place:
     - `7516`: each job-6 Fremen troop takes a Harkonnen strike (`751d`);
     - `7419`: each job-6 Fremen troop strikes (`73ef`); if $H=0$ the battle is won.
4. Store `ds:98 = H_0 − H`, the Harkonnen strength lost, and `ds:9a = F_0 − F`, the Fremen strength lost (read by CONDIT).
5. Play 20 explosion effects: `rand_masked(0x201)` picks sound 0x0b or 0x11 and palette/sprite `0x28 + (ah & 2)` through `ddb0`.
6. `ds:473a = 0`. Jump to `1b8d`, which re-runs `1bec`.

**No game time passes.** The battle is settled by a single roll, repeated up to 16 times. If the Harkonnens win that roll with Paul present, the Fremen are usually wiped out and Paul dies (code 6). This is Stilgar's "hazardous but quick".

### 3.6 FIGHT FOR A WHOLE DAY (`0fc5`)

Run up to 16 times `run_events_for_n_time_periods(1)` (`0fd9`). Each call advances `game_time` by 1 and runs the full period, including every job-6 roll and `1bec`. **Stop as soon as `ds:2b == 0`**, which happens when the battle is over or Paul dies. Then repaint the room (`0fa7`).

### 3.7 Battle won (`7429` → `7443` for fortresses)

`7429`:
- If the place is not Paul's current place, queue vision message 7, "We won the battle Muad'Dib, here in …! Yaoouuuh!" (`71b2`). It needs Paul-events bit 0.
- Sietch (appearance < 0x28, a won raid): clear status 0x02, then run the common tail with `75af`.
- Otherwise go to `7443`.

`location_battle_won_for_fortress` (`7443`):
1. `loc+0b = 5`. Paint an Atreides disc of radius 5 (`644e`).
2. **`loc+0b = day + 2`**: the conversion day.
3. Charisma +4 (`6f78`). If charisma crosses a multiple of 4, every active troop gains $\Delta\lfloor c/4\rfloor$ motivation. Then **every active troop +1 motivation** (`6f56`, cap 100). Internal charisma is capped at 200, and the displayed value appears to be half of it **[inferred: phase 0x50's +40 shows as +20 on the route]**.
4. Status |= 0x08 (held by the Atreides). `ds:115a = rol(0x8000, first_name)` (purpose unknown).
5. **`75af` on hired troops:**
   - the prospector clears its captured bit;
   - a captured troop (0x20) gets occupation 0x22, "apologizes for being captured", awaiting orders;
   - every other troop: bits10 |= 0x400, and at a fortress also |= 0x20 (converting); **motivation +4, army skill +3 (cap 0x5f), job 4 (military training)**.
6. **`75ea` on unhired troops with bits10 & 0x80 (the fort's Harkonnens, defeated or not):**
   - unlink, clear 0x80 (they become Fremen), relink;
   - **if the new slot is ≥ 8, remove from play** (`66b1`);
   - otherwise they become **freed Fremen**: occupation 0xa0 (not hired, so recruitable), population 100–227 (`(rand_masked(0x0f7f) & 0x7f) + 0x64`), motivation 20–35, spice and army skill 10–41 each, **equipment 0 without unregistering**, then made visible (`68e0`).

   This is "freed prisoners … compelled to work for the Harkonnen" and the slaves joining. The engine's `fortressTaken` caps this at 8 freed troops; the real rule is **slot < 8**, and the rest leave play.
7. **Common tail `7479`** (also used for sietch wins):
   - `d = 1` if `(ds:0 & 3) == 0`, else 0;
   - repeat `762a` over the unhired Harkonnen troops until one pass handles no more than `d`:
     - while the count is below `d`, a troop becomes a **captive**: occupation 0xac, hidden, population 0, equipment 0;
     - after that each troop is removed from play.

   At a fortress, step 6 has already converted all of them, so this matters for raids: a 25 % chance to keep one captive raider.
8. `accumulate_harkonnen_spice_production` (`1cda`) recounts the hostile places in `dl`. **If ≤ 1 (only the Harkonnen palace is left):**
   - `ds:c2 = 1` (final attack stage 1, shipments stop);
   - clear bit 1 of room-person flags `ds:0ff7` and `ds:1007` (the records at `0fe8` Jessica and `0ff8` Thufir; **[inferred: this releases them for the finale]**);
   - `765e`: $T$ = total atomics over all 70 places; if $T\ge 10$, add $T-10$ to this place. The check looks inverted; the initial total is exactly 10, so this is normally a no-op **[read from code]**.
9. Mark the map dirty.

**Captured equipment.** A place's equipment row counts both free items and the items its troops hold (`7f27` subtracts the held items to get the free stock, `ds:46fe`). The Harkonnen troops' items were registered at the fort. Step 6 zeroes their equipment bytes **without unregistering**, so **the fort's whole row becomes free stock**, available to MODIFY EQUIPMENT (`7cbb`; we have `takeEquipment`/`giveEquipment`). The initial rows are fort 2 `[2,2,3,3,3,3,0]`, fort 3 `[2,3,3,3,3,3,0]`, fort 4 `[1,2,0,1,1,1,0]`, and so on. The 10 atomics sit in forts 2, 3, 4, 5, 54 and 55. The route's atomics, weirding modules and laser guns come from these rows.

**Captain and prisoners** (route items 34 and 37). While the battle is on, defeated Harkonnen troops (0x20 with 0x80) stand in room 3 as the Harkonnen captain (§1.5). After the win the freed troops (0xa0) are the Fremen you can talk to at the fort.

### 3.8 Battle lost (`74b6`)

1. Clear status 0x02.
2. **If the place is Paul's current place: `ds:46d9 = 6`** (game over) and return.
3. A sietch (appearance < 0x28) **becomes a fortress**: appearance = (appearance & 7) + 0x28, `ds:27` −1, and the named characters parked there move to its room 3.
4. `7506` on every troop there: Harkonnens are hidden again (bits10 |= 0x10); Fremen are captured (occupation |= 0x20).
5. Paint a Harkonnen disc of radius 5 (`6447`). Clear status bits 0 and 3. Mark the map dirty.

### 3.9 Fortress becomes a sietch (`6e20`, from `71ef` at a new day)

On the first period of a new day (`ds:46de`), for each military-training troop at a place with status & 0x08:

- with $\delta = (\text{day} - \text{loc+0b}) \bmod 256$, skip if $\delta\in\{254,255\}$;
- otherwise clear 0x08, set **appearance = appearance & 7 (a sietch)**, `ds:27` +1, re-home the characters there (`6dbb`, room 1 or 2; troops with bits10 0x20 get speech 0x1000, "turned this ugly fortress into a nice sietch"), and `loc+0b = 5`.

In practice this happens **two days after the win**. Until then the place is an Atreides-held fortress: friendly, and the Stilgar/Chani "beaten off" lines apply (status bit 3).

### 3.10 Military training (`71ef`, job 4), which feeds army skill

This runs `6e20` first and clears bits10 0x200. If the place has saboteurs (status & 0x04), run the countdown in `depE` (`725f`) and return. Otherwise decrement `depC`; when it goes below 0:

- $\bar a$ = the mean army skill of the job-4 troops at the place. If Gurney's record `ds:101a` is parked at this place, $\bar a = 160$ and bits10 |= 0x800;
- $K$ = 200 with weirding modules or atomics, 250 with laser guns, 300 with krys knives, otherwise 400;
- then

$$
\text{depC}=\left\lfloor\frac{K}{2\max(0,\ \bar a-a)+\max(m,30)}\right\rfloor,\qquad a\leftarrow\min(a+1,\ 95)
$$

A rank change (high nibble) sets bits10 bits 0–1 = 2 for the troop's line. Every 4 days (`6d7b`, when `game_time & 0x3f == 0`), each skill of the troop's *other* classes that is marked as showing (speech bits 13–15) loses 1, down to a floor of 0.

### 3.11 Engine mapping

New file `engines/dune/battle.cpp` with these `World` methods:

| method | routine | notes |
|---|---|---|
| `troopStrength(id)` | `342d` | |
| `battleForces(loc, uint16 &h, uint16 &f)` | `33be` | also writes `ds:94/96/9c/5c/5e` for CONDIT |
| `battleBalance(h, f)` | `33d9` | |
| `battleLoss(x, id)` | `758d` | |
| `attackTick(id, loc)` | `73d9` | calls `harkonnenStrike` (`751d`) and `fremenStrike` (`73ef`/`7552`) |
| `battleWon(loc)` | `7429`/`7443` | the helpers for `75af`, `75ea` and `762a`; **fold the existing `fortressTaken()` into this** and keep the vegetation path calling it |
| `battleLost(loc)` | `74b6` | |
| `battleGauge(loc)` | `60f8` | |
| `militaryTraining(id, loc)` | `71ef` | includes `6e20` |
| `massiveAttack(loc)` | `7317` | |
| `troopCaptured(id)` | `668f` | |
| `removeFromPlay(id)` | `66b1` | |

Other changes:
- `runPeriod()` handles jobs 4, 5 and 6 and gates the troop walk on `ds:c2 < 7`. Also make charisma changes go through a helper that adds the motivation spill (`6f78`/`6fb0`); `addCharisma` currently does not.
- `GameScreen`:
  - replace `arrivalIsFatal()` with `nightAttackCheck(place)` (`503c`), called on arrival **and** after every `passTime` period while `ds:2b` ≠ 0;
  - map `ds:46d9` codes 4 and 6 to COMMAND 0xbe and 0xc0 endings;
  - in `addRoomRows()`, when `ds:2b` is set in room 1, show SEE DUNE MAP / `kRowMassiveAttack` / `kRowFightDay` / `kRowWorm`;
  - drive `NightAttack` (with `massive = true` during a massive attack) as the room backdrop;
  - `kRowFightDay` calls `passTime(1)` up to 16 times and stops when `ds:2b == 0`;
  - `kRowMassiveAttack` calls `World::massiveAttack(current)`, then `nightAttackCheck`.
- `MapScreen`: draw the "Battle:" gauge in the location popup (`60ac`).

---

## 4. Worm riding (route items 29, 30 and 33 onward)

### 4.1 Availability

- **CALL A WORM** (record `ds:2214`, handler `42d1`) appears in the desert and in room 1 of every place (room-code low byte 0x80, room 1), including the battle menu. It is **greyed while `game_phase < 0x4f`**, and phase 0x4c is Leto's death (`2f2f`/`2fba`).
- **GO THERE RIDING A WORM** (menu `ds:20e6`, handler `50ea`) appears in the map's location popup **when `ds:0a` bit 6 is set** (worm ridden) and the map is not in ornithopter mode (`5ff9`). In ornithopter mode the popup offers GO THERE FLYING AN ORNI (`ds:20da`) instead.

### 4.2 Calling (`42d1` → `4285`)

1. Close the dialogue.
2. Worm setup, once per run of the program: copy the worm video descriptor into `ds:aa66`. Then `ds:487e = 1` (travel vehicle: VER), `ds:473e = 0` (not the orni cockpit), `ds:11c9 = 8` (worm mode), and load `VER.HSQ` (resource 0x39).
3. Open the map with the Cancel menu (`4305`) to **select a destination**.

### 4.3 Departure (`map_confirm_travel_and_close` `4703` → `4795`)

- The mode flags become `8 | (8 >> 2)`, so the travel mode is `flags & 3 = 2`.
- A pending night attack is cancelled (`ds:2b = 0`, `b21`): **calling a worm is the way out of a battle.**
- The departure transition for a non-orni mode (`47a8`):
  - **`set_game_phase(0x50)`**. `121f` only raises the phase, so this fires once. Callback `117b`: **`ds:0a |= 0x40`, charisma +0x28 (+40 internal, with the motivation spill), move Jessica (`101b`)**. The phase-0x50 dialogue then gives Stilgar's "you have now perfected the riding of the worm" **[inferred: CONDIT data, not read]**;
  - then play **VER.HNM** (resource 0x0e) with **SN8.VOC**. The worm-suit music is `WORMSUIT.HSQ` (`ad50`).
- **No ornithopter is taken** from the pad (`4785` decrements `loc+15` only in mode 1).

### 4.4 The ride

The pump is the same as for the ornithopter (`travel_pump` `4f0c`):
- one step every 0x300 PIT ticks, which is `World::kFlightStepMillis` = 3834 ms, one map cell per step (`5206`);
- **one game period every 16 steps** (`4b3b`);
- no speed difference between vehicles was found **[read from 4b3b/5206; no vehicle term]**.

The **hostile-zone check `4182` only runs for mode 1 (ornithopter)**. Riding a worm into Harkonnen land never raises the warning and **never ends in "shot down" (code 2)**. This is the mechanical reason for Thufir's "find another way to travel".

In-flight verbs: BACK TO STARTING POINT (`50a5`), TOWARDS NEAREST PLACE (`50c4`), CHANGE DESTINATION (`497a`), and SKIP TO DESTINATION (`4ffb`, at most 0xc8 steps).

### 4.5 Arrival

`travel_finish_at_destination` (`4fc3`) takes these steps:
1. It re-seeds `ds:c5`.
2. It parks an orni only in mode 1.
3. It enters through `4002` → `arrive_at_location` (`401f`), which sets `ds:114e`/`ds:1150`.
4. **`503c`** runs the battle screen or death check (§3.4), then the place is marked discovered, then room 1.

### 4.6 Engine mapping

- `kRowWorm` (now a stub) opens `MapScreen` in select-destination mode with `_vehicle = kWorm`.
- Add `GameScreen::rideWormTo(loc)` and `rideWormToward(lng, lat)`, sharing `flyTo`/`flyToward` through a `Vehicle` parameter. Skip `askHostileZone()`, `animateOrni()` and `setOrnithopters()`, play `VER.HNM` and `SN8.VOC` on departure, and call `setGamePhase(0x50)` before the transition. The existing `phaseCallback(0x50)` should set `ds:0a` bit 6 and add charisma +40; check that it does.
- The map popup offers `kRowWormTravel` ("GO THERE RIDING A WORM") when `ds:0a & 0x40` and not in orni mode.
- Arrival calls `nightAttackCheck()`, not `arrivalIsFatal()`.

---

## 5. Harkonnen counter-attacks (raids and night attacks)

### 5.1 Scheduler (`actions_time_in_day_4` `1f64`, period slot 4 of each day)

All of these must hold:
- `game_phase ≥ 0x3c`, or `game_time − ds:1154 ≥ 0x70` (seven days after a phase callback stamps `ds:1154`);
- the day is even (`game_time & 0x10 == 0`);
- `ds:11bc` is 0. It is cleared whenever it is checked, and **any job-6 tick sets it**, so an ongoing attack of ours suppresses the next raid;
- `rol ds:0` sets the carry (about 50 %).

### 5.2 Target (`harkonnen_pick_attack_target` `2017`)

Scan the locations while `first_name < 8` (regions 1–7). A candidate needs all of these:
- a sietch (appearance < 0x20) with status & 0x82 clear and **map y below the best so far** (starting at 100), so the lowest y wins;
- no ill troop (`1e24`);
- after staging, `ds:60 ≠ ds:63`: it has Fremen troops that are not all prospectors **[inferred meaning of ds:63]**;
- a source within 30 cells: the nearest hidden Harkonnen place (`ds:e2`/`e4`), otherwise the nearest *revealed* place with appearance ≥ 0x28 (`ds:dc`/`de`);
- the source is not location 1 (the Harkonnen palace), has at least one live Harkonnen troop and has no job-6 Fremen.

### 5.3 Raid

1. `ds:c4 += 1`.
2. Move **up to 2 Harkonnen troops** from the head of the source's chain: occupation 0x8d, `issueMoveOrder` to the target, clear the hidden bit, **arrive at once** (`8357`).
3. Target status |= **0x02**.
4. `83fd`: the hired troops there switch to job 6 (defending); prospectors stop.
5. The characters parked there move to its room 1.
6. Queue vision message 0x0c, "The Harkonnens are attacking …!", or 0x0d if the prospector troop `ds:08e0` is there.
7. **If Paul is at the target:** force room 1, `ds:2b = 1`, compute the gauge. The night battle has started around him.

Defence then runs exactly as in §3.2: the defenders are job 6 and the raiders are the Harkonnens.
- **Win:** clear 0x02, `75af` (defenders to military training, +4 motivation, +3 army skill), and a 25 % chance to keep one raider as captive 0xac; the rest leave play.
- **Loss:** §3.8. **The sietch becomes a Harkonnen fortress** that must be retaken, `ds:27` −1.

Harkonnen troops also grow at slot 15: with a `rol ds:0` carry, every Harkonnen troop with a population of 1..199 gains 1 (`1d10`). This is already ported.

### 5.4 Engine mapping

Add `World::harkonnenRaid()` (`1f64`) and `World::pickRaidTarget(uint &src)` (`2017`) in `battle.cpp`. Call them from `runPeriod()` at `timeSlot() == 4`, after the troop walk and only while `ds:c2 < 7`. Return the raided index so `GameScreen` can start the night battle when it is Paul's place.

---

## 6. Final attack and the Emperor ending

### 6.1 The stage counter `ds:c2`

Every write to `ds:c2` in the code:

| stage | set by | where | notes |
|---|---|---|---|
| 0→1 | the last fortress falls (hostile places ≤ 1) | `749d` | shipments stop (`20ae`, `1df0`). Stilgar: "no more Harkonnen fortresses… use the help of Thufir" **[inferred: CONDIT on c2]** |
| +1 each | dialogue event 0x0e, first time the line is said | `a1ed` | already in `dialogue.cpp`. Route items 46, 47 and 51: Stilgar suggests Thufir, Thufir joins, the war council ("10000 men… atomics") **[which lines carry event 0x0e is inferred]** |
| 4→5 | **any line of Thufir (speaker 2) while `c2 == 4`**, if the 10 000-men test passes | `9f40` → `1243` | route item 52, "We have enough men around the palace" |
| 5→6 | **Stilgar event 9** | `2d2c` | the troops march on the palace (§6.3) |
| 6→7 | **the palace falls** | `73a9` | at c2 ≥ 7 **all period troop and time-of-day events stop** (`1b5e`) |

The CONDIT data gates the Stilgar, Thufir, Gurney and Chani dialogue on these values. Our dialogue engine already reads `ds:c2`, so it only needs correct writes.

### 6.2 The 10 000-men test (`1243`)

$$
\sum_{\substack{\text{hired troops at locations }2,3,4\\ \text{job}=4\ \wedge\ \text{atomics}}} p \;\ge\; 1000 \quad (\text{that is, } \ge 10\,000 \text{ men})
$$

Here $p$ is population in units of 10 men, so the threshold is 10 000 men. The test uses the hired-troop iterator `661d` over the records at `ds:0138`, `0154` and `0170`, the three Arrakeen fortresses by the palace. They must already be taken; they become sietches two days later (§3.9). The job must be exactly 4 (military training) and equipment bit 2 (atomics) must be set.

The engine lacks this. Add `World::finalAttackReady()` and call it from `GameScreen::startConversation()`, or wherever the speaker is set, when `speaker == 2 && ds:c2 == 4`.

### 6.3 Stilgar launches the attack (`2d2c`, event 9 of speaker 5)

1. `ds:c2 += 1`.
2. Collect the hired troops **at locations 2, 3 and 4 only** with job 4 and atomics (`2d62`).
3. For each: `issueMoveOrder(troop, 1)` (the Harkonnen palace), plus one extra `troopTravelStep`.

The loop's end test, `si < sp+0x0e`, caps the list at about 6 troops **[read from code; the exact cap depends on the stack layout]**.

`World::finalAttackTroops()` currently scans **all** troops and moves none. Restrict it to locations 2, 3 and 4 and to hired troops, and call `issueMoveOrder`.

### 6.4 The palace falls (`73a9`)

The first period in which **any** job-6 troop at location 1 ticks does all of this at once. The troops become job 6 on arrival through `83fd`, because the palace is hostile and its Harkonnens hold the chain head.

1. `ds:c2 += 1`.
2. **All hired troops at the palace → job 4** (`7399`).
3. **Remove every unhired Harkonnen troop at the palace from play** (`6e02`/`764d`, looped until none are left).
4. **Repaint the whole map:** every cell whose stage bits are 0x30 becomes 0x20, so the Harkonnen land turns Atreides (loop over `0xc5f9` bytes of the map buffer). This is why the route's globe shows "Harkonnen men = 00".
5. Queue vision message 0x0a, "The shield is down, the Harkonnen troops here have surrendered!".

There is **no combat roll at the palace**. The palace needs no battle: the atomics only matter through the 10 000-men gate (§6.2) and the dialogue.

Nothing in `73a9`, `8357` or `84a6` checks `ds:c2`. **[Untested: a troop sent to the palace with MOVE TROOP might take it early. Check this in the original before relying on it; the engine should reproduce the code literally.]**

### 6.5 Visiting and the ending

After the fall, the palace has no Harkonnens and no job-6 troops. So `503c` neither starts a battle nor kills Paul (`507a`: no Harkonnens, so no code 4), and Paul can enter.

`callback_transition_04057` (`4072`) reacts to **location_and_room = 0x3002 (Harkonnen palace, room 2)**: `game_phase_set_to_c8_game_ending` (`16fc`) sets phase 0xc8 and runs the scripted scene `cs:128f`, the throne room with the Emperor, the Baron and Feyd. Our `showRoom()` already does this.

### 6.6 Engine mapping

Add `World::palaceFalls()` (`73a9`) in `battle.cpp`, dispatched from `attackTick()` when `loc == 1`. Fix `finalAttackTroops()` and add `finalAttackReady()` as above. Also check `ds:c2 >= 7` at the top of `runPeriod()`: once the palace has fallen, only the clock, the new-day hook and the CONDIT staging still run.

---

## 7. Summary of engine work, in speedrun order

1. RNG: two LCGs that are not saved, plus refreshing `ds:0` (§0.2). This is a prerequisite for rerolling battles on reload.
2. `troops.cpp`: move orders, travel, arrival, chain link and unlink, and equipment registration (§2). Enable MOVE TROOP.
3. Espionage: the army menu, espionage tick, reveal and the ATTACK verb (§1).
4. `battle.cpp`:
   - strength, forces, balance and loss;
   - the job-6 tick;
   - won and lost, fort→sietch conversion and military training (§3.3–3.10);
   - `nightAttackCheck`, the battle verbs, MASSIVE ATTACK and FIGHT FOR A WHOLE DAY (§3.4–3.6).
5. Worm: CALL A WORM, GO THERE RIDING A WORM, phase 0x50, no hostile zone (§4).
6. Raids (§5).
7. Final attack: `finalAttackReady`, a fixed `finalAttackTroops` with the march, `palaceFalls`, and the c2 ≥ 7 gate (§6).

## 8. Open questions and inferred items

- Which dialogue lines carry event 0x0e, and which c2 values the CONDIT records test. This needs a dump of DIALOGUE/CONDIT; the engine only needs to execute the events.
- The text of COMMAND 0xc0 (the battle-death ending) was not extracted.
- The meaning of `ds:63` in the raid target test, of `ds:115a` in `7443`, and of `ds:46d8` in `6a45`.
- Whether a direct MOVE TROOP to the palace before stage 5 works in the original (§6.4).
- The floppy build's code was not compared (§0).
- `ds:60` in `751d` belongs to the location last staged for CONDIT, not necessarily the battle location (§3.2). Reproduce it literally.
- The unlinked-troop return in `84a6` when the destination is a sietch under attack (§2.2).

## Credits

Routine and variable names and most field meanings come from madmoose's (Thomas Fach-Pedersen) dune-chani disassembly annotations. The occupation list comes from the OpenRakis DuneEdit2 `JobFinder`. The equipment and status bits and the army-skill warning come from Lionel Debroux's odrade. The night-attack particle simulation we already use comes from madmoose's dune-rust and codingstyle's swift-dune. The route and timings come from the speedrun transcription in `route.md`. The formulas, tables and control flow above were read from the CD 3.7 disassembly and executable in this session.
