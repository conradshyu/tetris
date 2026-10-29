# Block Drop (Native Windows Tetris)

A complete single-player falling-block puzzle game written in C++ with the
Win32 API. It uses GDI for drawing, a memory device context for flicker-free
double-buffered rendering, and WinMM for synthesized sound effects. No
third-party libraries or downloaded runtime files are required.

## Requirements

- Windows 10 or later.
- Visual Studio 2022 or later with the **Desktop development with C++**
  workload, including the MSVC C++ build tools and a Windows 10/11 SDK.
- About 10 MB of disk space for the source and build output.

The supplied project currently selects Visual Studio 18's `v145` platform
toolset. If your Visual Studio installation uses a different toolset, open the
project's **General > Platform Toolset** property and select an installed
version.

## Build with Visual Studio

1. Extract `Tetris.zip` to a folder you can write to.
2. Open `Tetris.sln` in Visual Studio.
3. If prompted, retarget the solution to an installed Windows SDK.
4. Choose **Debug** or **Release** and **x64** (recommended) from the toolbar.
   The solution also includes Win32 configurations.
5. Select **Build > Build Solution** (or press **Ctrl+Shift+B**).
6. Run with **Debug > Start Without Debugging** (or **Ctrl+F5**).

The executable is written to `x64\Debug\Tetris.exe`, `x64\Release\Tetris.exe`,
`Debug\Tetris.exe`, or `Release\Tetris.exe`, depending on the selected
configuration and platform.

## Build from a Visual Studio Developer Command Prompt

From the folder containing `Tetris.sln`, build the 64-bit Release version with:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  .\Tetris.sln /m /p:Configuration=Release /p:Platform=x64
```

If Visual Studio is installed in another location, use that installation's
MSBuild path. Alternatively, open **Developer PowerShell for Visual Studio**
and run the same `MSBuild` command by name.

## Play

| Key | Action |
| --- | --- |
| Left / Right arrows | Move the active piece |
| Up arrow | Rotate clockwise |
| Down arrow (hold) | Soft drop; earn one point for each row descended |
| Space | Hard drop; earn two points for each row descended |
| P | Pause or resume |
| R | Start a new game |
| A-Z, Backspace, Enter | Enter initials and save the score after game over |

Pieces use a shuffled seven-piece bag, so each group of seven contains one of
each tetromino. Complete rows to clear them and earn points: 100, 300, 500, or
800 points for one through four rows, multiplied by the current level.
Soft- and hard-drop points are added as you descend. The level increases for
each 500 points, and gravity gradually accelerates to a minimum 80 ms per row.
The next piece, current score, cleared lines, level, and the five best scores
are shown beside the board.

## Sound and rendering

The game generates short PCM sound effects in memory at startup and plays them
through the Windows Multimedia (`winmm`) API; there are no external audio
assets. Each frame is drawn to a compatible memory bitmap and copied to the
window in one `BitBlt`, avoiding incremental on-screen drawing and reducing
flicker.

## Score storage

When a game ends, enter up to three initials and press **Enter** to record the
result. If no initials are entered, `AAA` is used. The game keeps the top ten
scores locally in:

```text
%LOCALAPPDATA%\Tetris\highscores.csv
```

This is typically
`C:\Users\<your-name>\AppData\Local\Tetris\highscores.csv`. The CSV contains a
score, initials, and local date per row. It is created automatically on the
first saved game and is not included in the source archive.

## Project files

- `Tetris.sln` — Visual Studio solution.
- `Tetris.vcxproj` — Native C++ Windows application project (Win32 and x64).
- `Tetris.cpp` — Game, Win32 window, rendering, input, sound, and score storage.
- `README.md` — Build and play instructions.

The application depends only on Windows system libraries (including `winmm`).
There are no third-party packages to download. Build outputs, intermediate
files, and player score data are not part of `Tetris.zip`.

## Troubleshooting

- **Missing v145 toolset:** install the matching Visual Studio C++ toolset, or
  change **Project Properties > Configuration Properties > General > Platform
  Toolset** to a version installed on your computer.
- **Missing Windows SDK:** install a Windows 10/11 SDK in Visual Studio
  Installer, then retarget the solution when prompted.
- **No sound:** check the Windows output device and volume. The game does not
  require sound hardware to start.
- **Score not saved:** ensure `%LOCALAPPDATA%` is available and the current
  Windows account can write to its `Tetris` subfolder.
