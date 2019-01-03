#!/usr/bin/python3

import shasta
import GetConfig

config = GetConfig.getConfig()

a = shasta.Assembler()
a.accessMarkerGraphVertices()
a.accessMarkerGraphEdges()
a.removeMarkerGraphBubbles(
    maxLength = int(config['MarkerGraph']['bubbleLengthThreshold']))


