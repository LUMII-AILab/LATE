# LATE: Open Source Toolkit for Private Speech Transcription

The LATE toolkit consists of three components:

1. **Front-end** - a single-page web application built with vanilla JavaScript and several third-party libraries (Silero VAD, FFmpeg, ProseMirror, WaveSurfer).
2. **Back-end** - a statically compiled and linked binary that includes a web server, the SQLite database engine, the whisper.cpp engine, and optionally, the CUDA library.
3. **Speech models** - a [Whisper](https://github.com/openai/whisper)-based ASR (automatic speech recognition) model in the GGML format, compatible with the [whisper.cpp](https://github.com/ggml-org/whisper.cpp) engine, and a VAD (voice activity detection) model.

## Running LATE locally

Currently, a precompiled LATE binary is available for macOS on Apple Silicon (M1, M2, M3, or M4).

A binary release for Linux/x86_64 computers is in progress.

First, download the latest binary release from the [releases page](https://github.com/LUMII-AILab/LATE/releases), then unzip it (e.g. `late-darwin-universal.zip`).

In a [Terminal application](https://en.wikipedia.org/wiki/Command-line_interface), change directory to where you unzipped LATE. Then download the ASR and VAD models by executing this command line:
```
./download_models.sh
```

Finally, run the LATE front-end and back-end by executing this command line:
```
./late
```

## Acknowledgements

This work was funded by the EU Recovery and Resilience Facility's project [Language Technology Initiative](https://www.vti.lu.lv/en/) (2.3.1.1.i.0/1/22/I/CFLA/002) in synergy with the State Research Programme's project [Research on Modern Latvian Language and Development of Language Technology](https://www.digitalhumanities.lv/projects/vpp-late/) (VPP-LETONIKA-2021/1-0006).
