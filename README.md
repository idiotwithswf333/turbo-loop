# Turbo Loop

A small 3D arcade racer: 3 laps, 3 AI rivals, drifting, a minimap and best-lap tracking, and five tracks.
Everything is generated in code except one embedded image (`src/face_png.h`), which is the only texture on Face Land.

## Tracks

| # | Name | What it is |
|---|---|---|
| 1 | Sunny Circuit | The classic: green hills, trees, mountains |
| 2 | Palm Bay | Beach: sand, sea, palm trees, boardwalk barriers |
| 3 | Neon Strip | Night city, Las Vegas style: glowing towers, neon kerbs |
| 4 | Prototype Park | Deliberately unfinished: grey boxes, wireframe and missing road, missing-texture checkers |
| 5 | Face Land | Every surface (road, walls, ground, sky, trees, mountains, cars) is the same face image |

Pick one in the menu (arrow keys, or press 1-5). `turboloop --track 3` starts straight on a track.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Accelerate | W / Up | RT or A |
| Brake / reverse | S / Down | LT or B |
| Steer | A D / Left Right | Left stick |
| Handbrake (drift) | Space | X |
| Restart race | R | Start |
| Track menu | M or Esc | B (after the race) |
| Pause | P | |
| Fullscreen | F11 | |
| Quit | Esc in the menu | |

## Getting the .exe from GitHub

1. Push this folder to a GitHub repository.
2. Open the **Actions** tab. The *Build* workflow runs on every push (or press **Run workflow**).
3. When it finishes, download **TurboLoop-windows-x64** from the run's *Artifacts*.
4. Tag a commit (`git tag v1.0 && git push --tags`) and the zip is also attached to a Release.

## Building locally

Needs CMake 3.20+ and a C++17 compiler. raylib is downloaded automatically.

    cmake -S . -B build
    cmake --build build --config Release

Run `turboloop --sim` for a windowless test: the AI drives all four cars through a full race on every track and prints the results.

To swap the Face Land texture, regenerate `src/face_png.h` from any PNG (it is just the file's bytes as a C array).
