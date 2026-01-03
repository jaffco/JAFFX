 # TODO:
 * Step 1: Get I2C4 working w/ DMA on Daisy Seed (in progress)
 * Step 2: Read raw register values and make sure they match defaults on FUSB302 datasheet
 * Step 3: Start importing https://github.com/graycatlabs/usb-c-arduino:
    - will need to rewrite all the I2C communication
    - strip all other non-necessary logic (keep source+sink code in case we want to charge toothbrush with our guitar pedal)?
    - use https://github.com/ReclaimerLabs/USB-C-Explorer/blob/1e312f194c6b402f8bfcd729c8bc48df8440bd78/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L260
    in main loop to read + list all the possible power profiles
 * Step 4: Use the function at https://github.com/ReclaimerLabs/USB-C-Explorer/blob/1e312f194c6b402f8bfcd729c8bc48df8440bd78/firmware/USB-C%20Explorer/USB-C%20Explorer/main.c#L420 to detect BC1.2 at the same time