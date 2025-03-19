#!/bin/bash

NAME=dc2025_badge
TITLE="DC 2025 badge"

mkdir -p panel

echo "panelizing the main board"
kikit panelize \
  --source 'rectangle; tlx: 15mm; tly: 15mm; brx: 155mm; bry: 155mm' \
  --layout 'grid; rows: 1; cols: 2; space: 5mm; vbackbone: 7mm' \
  --tabs annotation \
  --post 'millradius: 1mm' \
  --cuts 'mousebites; drill: 0.5mm; spacing: 0.7mm; offset: -0.2mm; prolong: 0mm' \
  --framing 'frame; width: 7mm; space: 4mm;cuts: h' \
  --tooling '3hole; hoffset: 2.5mm; voffset: 2.5mm; size: 1.152mm' \
  --fiducials '3fid; hoffset: 5mm; voffset: 2.5mm; coppersize: 2mm; opening: 1mm;' \
  --text "simple; text: ${TITLE}, order# JLCJLCJLCJLC; voffset: 2.5mm; justify: center; vjustify: center;" \
  --copperfill 'hatched; clearance: 2mm; spacing: 0.5mm; width: 0.5mm' \
  ${NAME}.kicad_pcb panel/${NAME}-panel.kicad_pcb


