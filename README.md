<p align="center">
  <img src="assets/micro-max-4.8-uci-core-banner.png"
       alt="micro-Max 4.8 UCI (Core)"
       width="900">
</p>

# micro-Max 4.8 UCI (Core)

## Overview

**micro-Max 4.8 UCI (Core) v1.0** adds the Universal Chess Interface (UCI) to the classic micro-Max 4.8 chess engine through a separate UCI integration source file, while the included `umax4_8.c` remains unchanged, byte for byte.

Written in C by H.G. Muller, micro-Max is most notable for its strength relative to its minimal code size. Version 4.8 has only 1,953 characters of compact source code.

Core is the preservation edition of the 64Logic micro-Max UCI project, built to let micro-Max 4.8 operate as originally designed. A separate edition with additional UCI features, including time management and Forsyth-Edwards Notation (FEN) support, is in development and uses the same `umax4_8.c` without modification.

Core lets you play against micro-Max 4.8 or use it in engine matches with chess software that supports UCI.

Core v1.0 is frozen for new features. Future changes are limited to correctness, portability, and documentation updates that preserve its scope.

## Downloads

Prebuilt engine binaries that have completed native validation are published on [GitHub Releases](https://github.com/64logic/micro-max-4.8-uci-core/releases).

**macOS:** These binaries are currently unsigned and may be blocked by Gatekeeper on first use; they can be allowed in System Settings > Privacy & Security.

## Use

Use the micro-Max 4.8 UCI (Core) engine executable in a chess graphical user interface (GUI), tournament manager, or other software that supports UCI, such as HIARCS Chess Explorer, Cute Chess, or Shredder. Core has no configurable UCI options by design.

Core accepts UCI position commands based on the standard starting position, such as:

```text
position startpos
position startpos moves ...
```

## UCI Identity

```text
id name micro-Max 4.8 UCI (Core) v1.0
id author UCI integration by 64Logic; micro-Max 4.8 by H.G. Muller
info string micro-Max 4.8: No FEN Input / No Clock Awareness / No Underpromotion
```

## Supported

- Chess from the standard starting position
- Pawn promotion to a queen
- UCI position setup and search

## Unsupported

- Position setup from FEN
- Time management using UCI clock values
- Pawn underpromotion
- UCI search limits using `nodes`, `movetime`, or `depth`
- Mate search requests using `mate`
- Limiting search to selected moves with `searchmoves`
- Interrupting the synchronous micro-Max search with `stop`
- Immediate processing of `isready` while a search is running
- True UCI pondering and continuous analysis

For compatibility, Core recognizes `go ponder` and `go infinite`. Each runs one ordinary synchronous micro-Max 4.8 search and then holds the completed result. A ponder result is released by `ponderhit` or `stop`; an infinite result is released by `stop`. These commands do not make the search interruptible or continuous.

Search time can vary, and some searches can take much longer than usual because of the original search behavior in micro-Max 4.8.

## Position Handling

Core uses the move history from the standard starting position. When a new position command builds on the position Core is already tracking, Core processes only the additional moves. If earlier moves change because of a takeback or a different variation, Core resets micro-Max 4.8 and rebuilds the position by replaying the full move history.

micro-Max 4.8 stores information about the game in the same hash table used during search and advances its internal state when a search selects a move. Core tracks that state as UCI position commands arrive. In a long session, accumulated search and game state can occasionally cause micro-Max 4.8 to reject an otherwise supported move even when the supplied move history is valid.

If micro-Max 4.8 rejects an otherwise supported move, Core resets micro-Max 4.8 once and replays the full move history. If the move is still rejected, Core reports that micro-Max 4.8 is unsynchronized with the move history rather than assuming or changing any moves. This preserves micro-Max 4.8’s original game behavior without altering the intended game state.

## Build

If you use a prebuilt engine binary, no build tools are required.

Core builds from two C source files:

| File | Description |
| --- | --- |
| `src/micro-max-4.8-uci-core-v1.0.c` | UCI integration by 64Logic |
| `third-party/umax4_8.c` | micro-Max 4.8 by H.G. Muller |

micro-Max 4.8 relies on plain `char` values being signed, because some of its internal values can be negative. On platforms where plain `char` would otherwise be unsigned, this could change engine behavior. The `Makefile` therefore compiles both C source files with signed `char` semantics using `-fsigned-char`.

`third-party/umax4_8.c` may produce expected warnings from modern compilers and should not be modified.

### Build Commands

| Platform | Build Details |
| --- | --- |
| macOS | **Requirements:**<br>Apple’s Command Line Tools for Xcode |
|  | **Commands:**<br>`make`<br>`make release` |
| Linux | **Requirements:**<br>C compiler and GNU Make |
|  | **Commands:**<br>`make`<br>`make release` |
| Windows x86_64 | **Requirements:**<br>MSYS2 UCRT64 environment with GCC and `mingw32-make` |
|  | **Commands:**<br>`mingw32-make`<br>`mingw32-make release` |
| Windows ARM64 | **Requirements:**<br>LLVM Clang for Windows ARM64, MSVC ARM64 libraries, Windows SDK, GNU Make, and a MinGW-compatible POSIX shell/tools environment |
|  | **Commands:**<br>`make`<br>`make release` |
| FreeBSD | **Requirements:**<br>C compiler and GNU Make |
|  | **Commands:**<br>`gmake`<br>`gmake release` |

SHA-256 verification requires `shasum` or `sha256sum`.

The `Makefile` automatically detects your operating system and processor architecture and names the engine executable for your platform. Add `V=1` to display compiler commands while building.

A normal build verifies the preserved `third-party/umax4_8.c` and applies the required compiler flags. A release build also performs a clean rebuild, verifies both C source files, uses the project’s release settings, and prints the SHA-256 hash of the completed engine executable.

## Platforms

The `Makefile` supports release builds for these platforms and architectures:

| Platform | Architectures |
| --- | --- |
| macOS | `x86_64`, `arm64` |
| Linux | `x86_64`, `arm64` |
| Windows | `x86_64`, `arm64` |
| FreeBSD | `x86_64` |

On macOS, release builds support macOS 10.13 or later on `x86_64` and macOS 11.0 or later on `arm64`.

Prebuilt binaries are published only after native validation for each platform and architecture. Each GitHub release lists the available builds.

Prebuilt x86_64 binaries use additional compiler optimizations and have different SHA-256 hash values from the standard `make release` binaries. Prebuilt ARM64 binaries use the standard `make release` settings.

## Verification

`make verify` confirms the SHA-256 hash of the preserved `third-party/umax4_8.c`, while `make verify-release` confirms both C source files.

[BUILD_INPUT_SHA256SUMS.txt](BUILD_INPUT_SHA256SUMS.txt) lists the SHA-256 hashes of the `Makefile` and the two C source files used for release builds.

Each [GitHub release](https://github.com/64logic/micro-max-4.8-uci-core/releases) includes a `SHA256SUMS.txt` file with the SHA-256 hashes of the release files.

If verification of `third-party/umax4_8.c` fails after a fresh Git clone, do not modify the file. First confirm that `.gitattributes` is present and that Git checked out `umax4_8.c` with LF line endings. Converting the file to CRLF changes its SHA-256 hash even though the source text appears unchanged. The file must match the canonical source exactly, byte for byte.

## Source and Licensing

The UCI integration, build files, and associated documentation files by 64Logic are licensed under the [MIT License](LICENSE).

The included `third-party/umax4_8.c` is the original micro-Max 4.8 source file by H.G. Muller. It remains unchanged, byte for byte, and is not relicensed under the MIT License.

See [PROVENANCE.md](PROVENANCE.md) for the source identity, attribution, redistribution basis, and SHA-256 hash.
