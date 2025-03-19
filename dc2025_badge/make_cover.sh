#!/bin/bash

TITLE="DC2025_badge_cover"
NAME=dc2025_badge
REV=$(grep "(rev" ${NAME}.kicad_sch |cut -d'"' -f 2)

OUTDIR=production/cover-$(date +%Y%m%d)-${REV}

mkdir -p cover
mkdir -p $OUTDIR

echo "separating the cover board"
kikit separate \
  --source 'rectangle; tlx: 155mm; tly: 15mm; brx: 275mm; bry: 155mm' \
  ${NAME}.kicad_pcb cover/${NAME}_cover.kicad_pcb

echo "preparing the fabrication files for JLCPCB"
kikit fab jlcpcb \
  --no-drc \
  --nametemplate "${TITLE}-{}" \
  cover/${NAME}_cover.kicad_pcb ${OUTDIR}

echo "saving the source file"
cp cover/${NAME}_cover.kicad_pcb ${OUTDIR}


