# wavFile
This example demonstrates how to store the audio data from a `.wav` file in a Daisy program.

Use `wavToArray.py` to store the audio data of a `.wav` file in a header file. `#including` this file in `wavFile.cpp` will make your Daisy play the audio in a loop. 

## NOTES: 
- Make sure your audio is small enough to fit in Daisy's flash memory (128KB)
- Make sure your `.wav` file has the same sample rate as set in `Jaffx.hpp`