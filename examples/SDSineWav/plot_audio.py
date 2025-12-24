import matplotlib.pyplot as plt
import scipy.io.wavfile as wav

# Read the WAV file
rate, data = wav.read('SDSine.wav')

# Plot the audio data
plt.figure(figsize=(10, 4))
plt.plot(data)
plt.title('SDSine.wav Audio Waveform')
plt.xlabel('Sample')
plt.ylabel('Amplitude')
plt.show()