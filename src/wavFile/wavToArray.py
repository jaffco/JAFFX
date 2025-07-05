import numpy as np
import sys
import os

def wav_to_c_array(input_file):
    try:
        # First try with scipy
        try:
            from scipy.io import wavfile
            rate, data = wavfile.read(input_file)
            print("Successfully read with scipy.wavfile")
        except Exception as scipy_error:
            print(f"scipy.wavfile failed: {scipy_error}")
            print("Trying alternative method with librosa...")
            
            # Fallback to librosa if available
            try:
                import librosa
                data, rate = librosa.load(input_file, sr=None, mono=False)
                # librosa returns data as float32 in range [-1, 1] by default
                # and in shape (samples,) for mono or (channels, samples) for stereo
                if len(data.shape) > 1:
                    # Transpose if needed to match scipy format (samples, channels)
                    if data.shape[0] < data.shape[1]:  # likely (channels, samples)
                        data = data.T
                print("Successfully read with librosa")
            except ImportError:
                print("librosa not available. Trying wave module...")
                
                # Fallback to built-in wave module
                import wave
                import struct
                
                with wave.open(input_file, 'rb') as wav_file:
                    frames = wav_file.getnframes()
                    sample_rate = wav_file.getframerate()
                    channels = wav_file.getnchannels()
                    sample_width = wav_file.getsampwidth()
                    
                    print(f"Wave info: {frames} frames, {sample_rate} Hz, {channels} channels, {sample_width} bytes/sample")
                    
                    raw_audio = wav_file.readframes(frames)
                    
                    # Convert based on sample width
                    if sample_width == 1:  # 8-bit
                        data = np.frombuffer(raw_audio, dtype=np.uint8)
                        data = (data.astype(np.float32) - 128.0) / 128.0
                    elif sample_width == 2:  # 16-bit
                        data = np.frombuffer(raw_audio, dtype=np.int16)
                        data = data.astype(np.float32) / 32768.0
                    elif sample_width == 3:  # 24-bit (tricky)
                        # 24-bit audio needs special handling
                        data = []
                        for i in range(0, len(raw_audio), 3):
                            # Convert 3 bytes to 24-bit signed integer
                            sample = struct.unpack('<I', raw_audio[i:i+3] + b'\x00')[0]
                            if sample >= 2**23:
                                sample -= 2**24
                            data.append(sample)
                        data = np.array(data, dtype=np.float32) / (2**23)
                    elif sample_width == 4:  # 32-bit
                        data = np.frombuffer(raw_audio, dtype=np.int32)
                        data = data.astype(np.float32) / (2**31)
                    else:
                        raise ValueError(f"Unsupported sample width: {sample_width}")
                    
                    # Reshape for multi-channel
                    if channels > 1:
                        data = data.reshape(-1, channels)
                    
                    rate = sample_rate
                print("Successfully read with wave module")
        
        print(f"Sample rate: {rate} Hz")
        print(f"Data shape: {data.shape}")
        print(f"Data type: {data.dtype}")
        
        # Handle different channel configurations
        if len(data.shape) > 1:
            # Multi-channel audio - convert to mono by averaging channels
            print(f"Converting {data.shape[1]}-channel audio to mono")
            data = np.mean(data, axis=1)
        
        # Ensure data is float32 and normalized
        data = data.astype(np.float32)
        data = np.clip(data, -1.0, 1.0)
        
        print(f"Final data shape: {data.shape}")
        print(f"Data range: [{np.min(data):.6f}, {np.max(data):.6f}]")
        
        # Generate the output file name with .h extension
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        # Replace special characters in filename for valid C identifier
        safe_name = "".join(c if c.isalnum() else "_" for c in base_name)
        output_file = f"{safe_name}.h"
        
        # Write the C array to the output file
        with open(output_file, 'w') as f:
            f.write(f"// Converted from {input_file}\n")
            f.write(f"// Sample rate: {rate} Hz\n")
            f.write(f"// Length: {len(data)} samples\n")
            f.write(f"// Duration: {len(data)/rate:.2f} seconds\n\n")
            f.write(f"#ifndef {safe_name.upper()}_H\n")
            f.write(f"#define {safe_name.upper()}_H\n\n")
            f.write(f"const float {safe_name}_data[] = {{\n")
            
            # Write data in chunks for better readability
            chunk_size = 8
            for i in range(0, len(data), chunk_size):
                chunk = data[i:i+chunk_size]
                f.write("    " + ", ".join(f"{x:.6f}f" for x in chunk))
                if i + chunk_size < len(data):
                    f.write(",")
                f.write("\n")
            
            f.write("};\n\n")
            f.write(f"const unsigned int {safe_name}_data_len = {len(data)};\n")
            f.write(f"const unsigned int {safe_name}_sample_rate = {rate};\n")
            f.write(f"const float {safe_name}_duration = {len(data)/rate:.6f}f;\n\n")
            f.write(f"#endif // {safe_name.upper()}_H\n")
        
        print(f"Conversion complete. Output saved to {output_file}")
        print(f"Array name: {safe_name}_data")
        print(f"Array length: {safe_name}_data_len")
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python wavToArray.py <input_wav_file>")
        print("\nIf you get import errors, you can install dependencies with:")
        print("pip install scipy librosa")
    else:
        wav_to_c_array(sys.argv[1])

