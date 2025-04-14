#!/bin/bash

models=models

mkdir -p "$models"

lang=$(echo $1 | tr '[:upper:]' '[:lower:]')

case "$lang" in
    lv)
        model_url='https://huggingface.co/AiLab-IMCS-UL/whisper-large-v3-lv-late-cv19/resolve/main/ggml-model-q8_0.bin?download=true'
        echo "Latvian Whisper large-v3 model selected (Q8-quantized)"
        ;;
    ltg)
        model_url='https://huggingface.co/AiLab-IMCS-UL/whisper-large-v3-latgalian-2503/resolve/main/ggml-whisper-large-v3-latgalian-2503-q8_0.bin?download=true'
        echo "Latgalian Whisper large-v3 model selected (Q8-quantized)"
        ;;
    *)
        model_url='https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-q5_0.bin?download=true'
        echo "Original Whisper large-v3 model selected (Q5-quantized)"
        ;;
esac

if [ ! -f "$models/silero_vad.onnx" ]; then
    curl -fL -o "$models/silero_vad.onnx" 'https://github.com/snakers4/silero-vad/raw/refs/heads/v4.0stable/files/silero_vad.onnx'
fi

curl -fL -o "$models/whisper-ggml.bin" "$model_url"
