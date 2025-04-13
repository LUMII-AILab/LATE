#!/bin/sh

models=models

mkdir -p "$models"

if [ ! -f "$models/silero_vad.onnx" ]; then
    curl -fL -o "$models/silero_vad.onnx" 'https://github.com/snakers4/silero-vad/raw/refs/heads/v4.0stable/files/silero_vad.onnx'
fi

if [ ! -f "$models/whisper-ggml.bin" ]; then
    curl -fL -o "$models/whisper-ggml.bin" 'https://huggingface.co/AiLab-IMCS-UL/whisper-large-v3-lv-late-cv19/resolve/main/ggml-model-q8_0.bin?download=true'
fi
