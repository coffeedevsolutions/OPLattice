# Genre: the controlled vocabulary

Sixteen values. `Genre` in `CFG/<SERIAL>.cfg` must be **exactly one** of them,
spelled exactly as written here.

    Action          Hack & Slash    Platformer      Shooter
    Adventure       Horror          Puzzle          Simulation
    Fighting        Music           RPG             Sports
    Open World      Racing          Stealth         Strategy

## Why a fixed list

Free text was fine at 46 games and produced 29 distinct values, 17 of them used
once: `Sports / golf`, `Sports / fishing`, `Motorcycle racing`, `Park
management`. At 250 that is roughly a hundred values and a filter nobody can
scroll, which is the opposite of what a filter is for.

Single words, deliberately. They fit the 25-character cell with room to spare,
they sort readably, and they make tagging two hundred games mechanical instead
of a judgement call each time.

**One genre per game.** This is a filter, not a taxonomy. A second genre doubles
the filtering logic and buys very little -- the description already carries the
nuance, and it carries it better in prose than in a slash-separated label.

## What each one means

| Value | Covers | Examples here |
|---|---|---|
| `Action` | Third-person combat that is not one of the more specific buckets | Onimusha 1/2, Gundam |
| `Adventure` | Exploration and narrative over combat | Shadow of the Colossus |
| `Fighting` | Versus, one screen, rounds | SoulCalibur II, Naruto 3, One Piece |
| `Hack & Slash` | Crowds, combos, one hero against many | Dynasty Warriors 3/4/5 |
| `Horror` | Survival horror | *(reserved -- Resident Evil, Silent Hill, Fatal Frame)* |
| `Music` | Rhythm and instrument games | *(reserved -- Guitar Hero, DDR, Amplitude, Taiko)* |
| `Open World` | A city or region you drive and walk freely between missions | GTA Vice City |
| `Platformer` | Jumping and traversal as the core verb | Jak, Ratchet 1-3, both LEGO SW, Pac-Man 3, Scaler, Flushed Away |
| `Puzzle` | *(reserved)* | |
| `RPG` | Parties, levelling, stats -- action-RPG included | FFX, FFXII, both Kingdom Hearts, Suikoden IV |
| `Racing` | Vehicles against a clock or a field | GT3, NFS MW, Midnight Club 3, ATV 4, Cars, Sonic Riders, Suzuki TT |
| `Shooter` | Ranged combat as the core verb, first or third person | Battlefront 1/2, CoD, Bionicle, Ratchet Deadlocked |
| `Simulation` | Management and building | Thrillville |
| `Sports` | Real sports, extreme sports and fishing alike | SSX 3, Tony Hawk, NFL Street 2, NCAA, Minna no Golf, Rapala |
| `Stealth` | Avoidance over confrontation | MGS2, MGS3 |
| `Strategy` | *(reserved -- Kessen, Front Mission)* | |

## Calls that contradict the old free-text labels

- **Ratchet 1-3 are `Platformer`, Deadlocked is `Shooter`.** The old
  `3D platformer / shooter` label was applied to all four and hid a real
  difference; the description for Deadlocked says it "abandons platforming
  almost entirely", which is the whole point of the spin-off.
- **Both LEGO Star Wars are `Platformer`**, not `Shooter`, blasters
  notwithstanding. The verb is traversal and character-swapping.
- **Shadow of the Colossus is `Adventure`, alone.** It is not the same kind of
  thing as Onimusha, and Ico, Okami and their like will join it.
- **`Sports` absorbs SSX, Tony Hawk and Rapala.** Splitting extreme sports and
  fishing out gives six buckets holding one game each, which is how the old
  vocabulary got to 29 values.
- **`Stealth` is kept for two games** because Splinter Cell and Tenchu are
  coming, not because two justifies a bucket on its own.

## Adding to the list

Don't, unless a bucket would hold at least three or four games and those games
would genuinely be looked for together. Every extra value costs a row in the
filter and a decision at tagging time. `Open World` and `Hack & Slash` earned
their places by what is coming rather than by what is here -- one game and three
games respectively today.
