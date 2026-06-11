#!/usr/bin/env bash
# Converts audio files to the format expected by the ESP32:
# mono, 16-bit PCM, 16 kHz WAV.
#
# Usage:
#   ./scripts/convert_audio.sh                  # converts all *.m4a in current dir
#   ./scripts/convert_audio.sh input.mp3        # converts a single file
#   ./scripts/convert_audio.sh *.wav            # re-encodes existing WAVs to spec

set -euo pipefail

DEST="data"
mkdir -p "$DEST"

convert() {
  local input="$1"
  local basename
  basename="$(basename "${input%.*}")"
  local output="${DEST}/${basename}.wav"

  echo "Converting: $input -> $output"
  ffmpeg -y -i "$input" \
    -ac 1 \
    -ar 16000 \
    -sample_fmt s16 \
    "$output"
}

if [ "$#" -gt 0 ]; then
  for f in "$@"; do
    convert "$f"
  done
else
  shopt -s nullglob
  files=(*.m4a *.mp3 *.ogg *.flac *.aac *.opus)
  if [ "${#files[@]}" -eq 0 ]; then
    echo "No audio files found in the current directory."
    echo "Usage: $0 [file1 file2 ...]"
    exit 1
  fi
  for f in "${files[@]}"; do
    convert "$f"
  done
fi

echo ""
echo "Done. Files written to: $DEST/"
echo ""
echo "File sizes:"
du -sh "${DEST}"/*.wav 2>/dev/null || echo "  (no files yet)"
