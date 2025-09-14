#!/bin/bash

# Color definitions
RED='\033[0;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
ORANGE='\033[38;5;208m'
PURPLE='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Let's get it started!
echo -e "${BLUE}Initializing ${ORANGE}JAFFX${NC}${YELLOW}${NC}..."

# Get submodules 
echo -e "${BLUE}Fetching ${PURPLE}submodules${NC}..."
git submodule update --recursive --init

# Set dir variables 
START_DIR=$PWD
LIBDAISY_DIR=$PWD/libDaisy

# Build libDaisy 
echo -e "${BLUE}Building ${YELLOW}libDaisy${NC}${BLUE}${NC}..."
cd "$LIBDAISY_DIR" ; make -s clean ; make -j4 -s
if [ $? -ne 0 ]
then
    echo -e "${RED}Failed to compile ${YELLOW}libDaisy${NC}.${NC}"
    echo -e "${YELLOW}Have you installed the Daisy Toolchain?${NC}"
    echo -e "${YELLOW}See README.md${NC}"
    exit 1
fi
echo -e "${GREEN}Built ${YELLOW}libDaisy${NC}!${NC}"

# We made it!
echo -e "${GREEN}${ORANGE}JAFFX${NC}${GREEN} Init Complete${NC}!"
echo -e "${YELLOW}Use ${CYAN}${BOLD}./run.sh path/to/file${NC}${YELLOW} to build and flash programs${NC}"