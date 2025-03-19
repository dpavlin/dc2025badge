#!/bin/bash

NAME=dc2025_badge
TITLE="DC2025_badge"

# get sch revision
REV=$(grep "(rev" ${NAME}.kicad_sch |cut -d'"' -f 2)
OUTDIR=production/main-$(date +%Y%m%d)-${REV}

mkdir -p $OUTDIR

# generate gerbers, BOM and position files for JLCPCB
echo "generating fabrication files for JLCPCB"
kikit fab jlcpcb \
  --no-drc \
  --nametemplate "${TITLE}-{}" \
  --assembly \
  --schematic ${NAME}.kicad_sch \
  --field "JLCPCB#" \
  panel/${NAME}-panel.kicad_pcb $OUTDIR

# export the schematic as pdf
echo "exporting the schematic as pdf"
kicad-cli sch export pdf --output ${OUTDIR}/${NAME}-schematic.pdf ${NAME}.kicad_sch

# copy the source pcb files
echo "saving the source file"
cp panel/${NAME}-panel.kicad_pcb $OUTDIR
cp ${NAME}.kicad_pcb $OUTDIR

