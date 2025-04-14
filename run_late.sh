#!/bin/bash

open http://localhost:9090/ &

chmod +x ./late
xattr -d com.apple.quarantine ./late
./late
