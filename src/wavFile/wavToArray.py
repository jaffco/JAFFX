import numpy as np
from scipy.io import wavfile
import sys
import os

def wav_to_c_array(input_file):
    try:
        # Read the .wav file
        rate, data = wavfile.read(input_file)
        
        # Ensure the data is in float32 format
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.iinfo(data.dtype).max  # Normalize if necessary
        
        # Generate the output file name with .h extension
        output_file = os.path.splitext(input_file)[0] + ".h"
        
        # Write the C array to the output file
        with open(output_file, 'w') as f:
            f.write(f"// Converted from {input_file}\n")
            f.write(f"const float wav_data[] = {{\n")
            f.write(",\n".join(map(str, data.flatten())))
            f.write("\n};\n")
            f.write(f"const unsigned int wav_data_len = {len(data)};\n")
        
        print(f"Conversion complete. Output saved to {output_file}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python wav_to_c_array.py <input_wav_file>")
    else:
        wav_to_c_array(sys.argv[1])

