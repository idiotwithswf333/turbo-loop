# Turbo Loop

A small 3D arcade racer: 3 laps, 3 AI rivals, drifting, a minimap and best-lap tracking.
Everything is generated in code, so there are no asset files.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Accelerate | W / Up | RT or A |
| Brake / reverse | S / Down | LT or B |
| Steer | A D / Left Right | Left stick |
| Handbrake (drift) | Space | X |
| Restart race | R | Start |
| Pause | P | |
| Fullscreen | F11 | |
| Quit | Esc | |

## Getting the .exe from GitHub

1. Push this folder to a GitHub repository.
2. Open the **Actions** tab. The *Build* workflow runs on every push (or press **Run workflow**).
3. When it finishes, download **TurboLoop-windows-x64** from the run's *Artifacts*.
4. Tag a commit (`git tag v1.0 && git push --tags`) and the zip is also attached to a Release.

## Building locally

Needs CMake 3.20+ and a C++17 compiler. raylib is downloaded automatically.

    cmake -S . -B build
    cmake --build build --config Release

Run `turboloop --sim` for a windowless test: the AI drives all four cars through a full race and prints the results.
