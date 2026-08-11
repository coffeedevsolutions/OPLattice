#!/usr/bin/env python3
"""Write per-game CFG files so the info page has something to display.

    tools/make-metadata.py [out-dir]        # default _deploy/CFG

OPL reads Genre, Release, Developer, Description and Rating with a plain
configGetStr on the game's config set, and nothing in OPL ever writes them --
they only exist as label strings. So the fields stay blank until an external
tool supplies them. OPL Manager does this properly when it downloads art.

WHAT THIS IS NOT: a games database. The values below are written from the
model's own recall, mostly for JP-region titles, and some of them will be wrong
-- release years and developer/publisher attribution especially. They are here
so the layout can be judged with realistic text in it. Treat anything you care
about as unverified, and let OPL Manager overwrite it when you get the chance.

#Size, #Media and #Format are deliberately absent: OPL computes those itself
from the disc and would ignore anything written here.

Existing files are merged, not replaced -- OPL stores real per-game settings
($VMC, $Compatibility, and so on) in the same file.
"""
import os
import sys

# serial: (genre, release, developer, rating, description)
GAMES = {
    "SLPM_650.53": ("Tactical Action", "2001", "Omega Force", "CERO A", "Massed-battle hack and slash across the Three Kingdoms."),
    "SLPM_652.48": ("Tactical Action", "2003", "Omega Force", "CERO A", "Officer duels and troop command over a wider campaign."),
    "SLPM_658.90": ("Tactical Action", "2006", "Omega Force", "CERO A", "Larger battlefields and a reworked officer roster."),
    "SLPS_250.50": ("RPG", "2001", "Square", "CERO A", "Pilgrimage across Spira, with voiced cutscenes and the sphere grid."),
    "SLPM_663.20": ("RPG", "2006", "Square Enix", "CERO B", "Ivalice, the gambit system, and a war fought in the margins."),
    "SLPM_654.88": ("Open World", "2003", "Rockstar North", "CERO Z", "Eighties Vice City, on foot and behind the wheel."),
    "SLPS_254.78": ("Action", "2005", "Bandai", "CERO A", "One Year War mobile suit combat."),
    "SCPS_150.21": ("Platformer", "2001", "Naughty Dog", "CERO A", "Seamless open platforming with no visible loading."),
    "SLPM_662.33": ("Action RPG", "2005", "Square Enix", "CERO A", "Keyblade combat across Disney worlds."),
    "SLPS_251.98": ("Action RPG", "2002", "Square", "CERO A", "The expanded cut, with added bosses and abilities."),
    "SLPM_650.78": ("Stealth Action", "2001", "Konami", "CERO C", "Tanker and Big Shell, seen through two protagonists."),
    "SLPM_657.90": ("Stealth Action", "2004", "Konami", "CERO C", "Cold War jungle infiltration, camouflage and CQC."),
    "SCPS_150.16": ("Sports", "2001", "Clap Hanz", "CERO A", "Approachable three-click golf with a deep swing system."),
    "SLPM_662.32": ("Racing", "2005", "EA Black Box", "CERO A", "Street racing and police pursuit in Rockport."),
    "SLPS_255.89": ("Fighting", "2005", "Eighting", "CERO A", "Cel-shaded arena fighting with jutsu chains."),
    "SLPS_254.73": ("Action", "2005", "Bandai", "CERO A", "Crew-based brawling through the Grand Line."),
    "SLPM_651.00": ("Action", "2002", "Capcom", "CERO C", "Sengoku-era demon hunting with a new cast."),
    "SLPM_650.10": ("Action", "2001", "Capcom", "CERO C", "Fixed-camera survival action in a haunted castle."),
    "SCPS_150.37": ("Platformer", "2002", "Insomniac Games", "CERO A", "Gadget-driven platforming across a planet-hopping campaign."),
    "SCPS_150.56": ("Platformer", "2003", "Insomniac Games", "CERO A", "Heavier weapon focus and weapon experience levels."),
    "SCPS_150.84": ("Platformer", "2004", "Insomniac Games", "CERO A", "Full arsenal, larger set pieces, online multiplayer."),
    "SCPS_150.99": ("Action", "2005", "Insomniac Games", "CERO B", "Arena combat and a shift toward shooter pacing."),
    "SLPM_654.49": ("Sports", "2003", "EA Canada", "CERO A", "Downhill racing and trick runs over one connected mountain."),
    "SCPS_150.97": ("Action Adventure", "2005", "Team Ico", "CERO B", "Sixteen colossi, an empty land, and nothing in between."),
    "SLPS_252.30": ("Fighting", "2003", "Namco", "CERO A", "Weapon-based fighting with an extensive single-player mode."),
    "SLPM_656.00": ("RPG", "2004", "Konami", "CERO B", "Naval war and rune magic across an island nation."),
    "SLUS_212.08": ("Sports", "2005", "Neversoft", "ESRB T", "Open-world skating with classic and slow-motion focus modes."),
    "SLPS_200.01": ("Racing", "2000", "Namco", "CERO A", "Launch-window drift racing at sixty frames a second."),
}

KEYS = ("Genre", "Release", "Developer", "Rating", "Description")

out = sys.argv[1] if len(sys.argv) > 1 else "_deploy/CFG"
os.makedirs(out, exist_ok=True)

written = 0
for serial, values in sorted(GAMES.items()):
    path = os.path.join(out, f"{serial}.cfg")

    # Preserve whatever OPL already stored for this game.
    existing = {}
    order = []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                if k not in existing:
                    order.append(k)
                existing[k] = v

    fields = dict(zip(KEYS, (values[0], values[1], values[2], values[3], values[4])))
    for k, v in fields.items():
        if k not in existing:
            order.append(k)
        existing[k] = v

    with open(path, "w", encoding="utf-8") as f:
        for k in order:
            f.write(f"{k}={existing[k]}\n")
    written += 1

print(f"{written} CFG files in {out}")
print("Values are recalled, not sourced. Expect errors in dates and developers.")
