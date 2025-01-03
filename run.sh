#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <path_to_script>"
  exit 1
fi

FILEPATH=$(realpath "$1")
DIRPATH=$(dirname "$FILEPATH")

cd "$DIRPATH" || { echo "Error: Could not change directory to $FILEPATH"; exit 1; }

make clean ; make; make program-dfu

# https://github.com/electro-smith/DaisyWiki/wiki/1.-Setting-Up-Your-Development-Environment#4a-flashing-the-daisy-via-usb
echo "Ignore Error 74 if related to download get_status"


