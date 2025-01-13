#!/bin/bash
# Change directory to the VICE folder
cd /home/gpf/code/dreamcast/VICE/vice/

# Set the KOS environment variables
source /opt/toolchains/dc/kos/environ.sh

# Build the VICE project
make x64

# Run the dc.sh script
bash /home/gpf/code/dreamcast/VICE/vice/dc.sh
