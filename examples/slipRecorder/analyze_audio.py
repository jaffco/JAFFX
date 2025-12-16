import numpy as np
import scipy.io.wavfile as wav
import matplotlib.pyplot as plt
from scipy.fft import fft, fftfreq
from scipy.signal import spectrogram
import os

# Load the WAV file
wav_file = 'rec_00026.wav'
rate, data = wav.read(wav_file)

print(f"Sample rate: {rate} Hz")
print(f"Data shape: {data.shape}")
print(f"Data type: {data.dtype}")
print(f"Duration: {len(data)/rate:.2f} seconds")

# If stereo, take left channel for analysis
if data.ndim == 2:
    left = data[:, 0].astype(np.float32)
    right = data[:, 1].astype(np.float32)
    print("Stereo file")
else:
    left = data.astype(np.float32)
    right = None

# Normalize to -1 to 1 if not already
if np.max(np.abs(left)) > 1:
    left = left / 32768.0  # assuming 16-bit, but it's float
else:
    left = left  # already float

# Plot time domain waveform (first 1 second)
plt.figure(figsize=(12, 6))
plt.subplot(2, 1, 1)
plt.plot(left[:rate])
plt.title('Waveform (Left Channel, First Second)')
plt.xlabel('Sample')
plt.ylabel('Amplitude')

# Plot full waveform overview
plt.subplot(2, 1, 2)
plt.plot(left)
plt.title('Full Waveform Overview')
plt.xlabel('Sample')
plt.ylabel('Amplitude')
plt.tight_layout()
plt.savefig('waveform.png')
plt.close()

# Compute FFT
N = len(left)
yf = fft(left)
xf = fftfreq(N, 1/rate)

# Plot FFT (magnitude)
plt.figure(figsize=(10, 6))
plt.plot(xf[:N//2], np.abs(yf[:N//2]))
plt.title('FFT Magnitude')
plt.xlabel('Frequency (Hz)')
plt.ylabel('Magnitude')
plt.xlim(0, 20000)  # up to 20kHz
plt.savefig('fft.png')
plt.close()

# Spectrogram
f, t, Sxx = spectrogram(left, fs=rate, nperseg=1024, noverlap=512)

plt.figure(figsize=(12, 6))
plt.pcolormesh(t, f, 10 * np.log10(Sxx), shading='gouraud')
plt.ylabel('Frequency [Hz]')
plt.xlabel('Time [sec]')
plt.title('Spectrogram')
plt.colorbar(label='Intensity [dB]')
plt.ylim(0, 10000)  # up to 10kHz
plt.savefig('spectrogram.png')
plt.close()

# Look for periodic glitches: compute derivative and find spikes
diff = np.diff(left)
threshold = 10 * np.std(diff)  # Higher threshold for significant spikes
spikes = np.where(np.abs(diff) > threshold)[0]

print(f"Number of potential glitch points (threshold 10*std): {len(spikes)}")

# Also look for periods of silence (amplitude below threshold)
silence_threshold = 0.01  # 1% of full scale
silent_samples = np.where(np.abs(left) < silence_threshold)[0]

# Group silent samples into runs
silent_runs = []
if len(silent_samples) > 0:
    start = silent_samples[0]
    for i in range(1, len(silent_samples)):
        if silent_samples[i] != silent_samples[i-1] + 1:
            silent_runs.append((start, silent_samples[i-1]))
            start = silent_samples[i]
    silent_runs.append((start, silent_samples[-1]))

print(f"Number of silent runs: {len(silent_runs)}")
if silent_runs:
    run_lengths = [end - start + 1 for start, end in silent_runs]
    avg_run_length = np.mean(run_lengths)
    print(f"Average silent run length: {avg_run_length:.1f} samples ({avg_run_length/rate*1000:.1f} ms)")

# Check for buffer-related periodicity
# Audio block size: 256 samples per channel, so 512 floats
block_size = 512
# SD write chunk: 4096 floats
sd_chunk = 4096

# Expected SD write interval
sd_write_interval_samples = sd_chunk / 2  # since stereo, sd_chunk is floats, /2 for samples
sd_write_interval_ms = (sd_write_interval_samples / rate) * 1000
print(f"Expected SD write interval: {sd_write_interval_samples:.0f} samples ({sd_write_interval_ms:.1f} ms)")

# Plot diff with spikes
plt.figure(figsize=(12, 6))
plt.plot(diff)
if len(spikes) < 1000:  # Only plot if not too many
    plt.scatter(spikes, diff[spikes], color='red', s=1)
plt.title('Derivative with Detected Spikes (Threshold 10*std)')
plt.xlabel('Sample')
plt.ylabel('Difference')
plt.savefig('diff_spikes.png')
plt.close()

# Analyze periodicity of spikes
if len(spikes) > 1:
    intervals = np.diff(spikes)
    plt.figure(figsize=(10, 6))
    plt.hist(intervals, bins=50)
    plt.title('Histogram of Intervals Between Spikes')
    plt.xlabel('Interval (samples)')
    plt.ylabel('Count')
    plt.savefig('spike_intervals.png')
    plt.close()

    # Most common interval
    unique, counts = np.unique(intervals, return_counts=True)
    most_common = unique[np.argmax(counts)]
    period_samples = most_common
    period_ms = (period_samples / rate) * 1000
    print(f"Most common spike interval: {period_samples} samples ({period_ms:.2f} ms)")

# Check for buffer-related periodicity
# Audio block size: 256 samples per channel, so 512 floats
block_size = 512
# SD write chunk: 4096 floats
sd_chunk = 4096

# Check if spikes align with block boundaries
block_boundaries = np.arange(block_size, len(left), block_size)
sd_boundaries = np.arange(sd_chunk, len(left), sd_chunk)

# Count spikes near boundaries
near_block = 0
near_sd = 0
for spike in spikes:
    if np.min(np.abs(spike - block_boundaries)) < 10:  # within 10 samples
        near_block += 1
    if np.min(np.abs(spike - sd_boundaries)) < 10:
        near_sd += 1

print(f"Spikes near audio block boundaries: {near_block}")
print(f"Spikes near SD write boundaries: {near_sd}")

# Generate markdown report
with open('audio_analysis_report.md', 'w') as f:
    f.write('# Audio Analysis Report: rec_00026.wav\n\n')
    f.write(f'## File Information\n')
    f.write(f'- Sample Rate: {rate} Hz\n')
    f.write(f'- Channels: {"Stereo" if data.ndim == 2 else "Mono"}\n')
    f.write(f'- Duration: {len(data)/rate:.2f} seconds\n')
    f.write(f'- Data Type: {data.dtype}\n\n')

    f.write('## Waveform Analysis\n')
    f.write('![Waveform](waveform.png)\n\n')
    f.write('The waveform shows the audio signal over time. The first second is plotted in detail, and the full overview shows the entire recording.\n\n')

    f.write('## Frequency Analysis\n')
    f.write('![FFT](fft.png)\n\n')
    f.write('The FFT shows the frequency content of the signal. Look for any unusual peaks or patterns.\n\n')

    f.write('![Spectrogram](spectrogram.png)\n\n')
    f.write('The spectrogram shows how frequency content changes over time. Periodic glitches may appear as vertical lines or patterns.\n\n')

    f.write('## Glitch Detection\n')
    f.write(f'Number of detected glitch points (threshold 10*std): {len(spikes)}\n\n')
    f.write('![Derivative with Spikes](diff_spikes.png)\n\n')
    f.write('Spikes in the signal derivative indicate sudden changes, potentially glitches.\n\n')

    f.write('## Silence Analysis\n')
    f.write(f'Number of silent runs (amplitude < 0.01): {len(silent_runs)}\n')
    if silent_runs:
        f.write(f'Average silent run length: {avg_run_length:.1f} samples ({avg_run_length/rate*1000:.1f} ms)\n')
        if 'silent_intervals' in locals() and len(silent_intervals) > 0:
            f.write(f'Average interval between silent runs: {avg_interval:.1f} samples ({avg_interval/rate*1000:.1f} ms)\n')
    f.write('\n')

    f.write(f'Expected SD write interval: {sd_write_interval_samples:.0f} samples ({sd_write_interval_ms:.1f} ms)\n\n')

    f.write('## Buffer Analysis\n')
    f.write(f'Audio block size: {block_size} floats\n')
    f.write(f'SD write chunk size: {sd_chunk} floats\n')
    f.write(f'Spikes near audio block boundaries: {near_block}\n')
    f.write(f'Spikes near SD write boundaries: {near_sd}\n\n')

    f.write('## Findings\n')
    if len(spikes) > 0:
        f.write('Periodic glitches detected. ')
        if near_sd > near_block:
            f.write('Glitches appear to correlate with SD write operations.\n')
        elif near_block > 0:
            f.write('Glitches may be related to audio block processing.\n')
        else:
            f.write('Glitch periodicity does not clearly align with buffer boundaries.\n')
    else:
        f.write('No significant glitches detected in the analysis.\n')

print("Analysis complete. Report saved to audio_analysis_report.md")