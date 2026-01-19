# CASPy - Python Bindings for CASPI

Python bindings for the CASPI audio DSP library, providing high-performance, real-time safe oscillators and audio processing primitives.

## Features

- 🎵 Band-limited oscillators (Sine, Saw, Square, Triangle)
- ⚡ Real-time safe C++ implementation
- 🐍 Pythonic API with NumPy integration
- 📊 Jupyter notebook support
- 🔊 Compatible with sounddevice for audio playback

## Requirements

- Python 3.8 or higher
- CMake 3.15 or higher
- C++14 compatible compiler
    - Windows: MSVC 2019+ (Visual Studio Build Tools)
    - Linux: GCC 7+ or Clang 5+
    - macOS: Xcode Command Line Tools

## Quick Start

### Installation
```bash
# Navigate to CASPy directory
cd /path/to/CASPI/Tools/CASPy

# Create and activate virtual environment
uv venv
.venv/Scripts/Activate.ps1    # Windows PowerShell
# source .venv/bin/activate    # Linux/macOS

# Build and install
uv build --wheel --out-dir dist/
uv pip install dist/caspy-0.1.0-*.whl

# Install development dependencies
uv pip install -e ".[dev,audio]"

# Verify installation
python -c "import caspy; print(f'CASPy {caspy.__version__} ready!')"
```

### Basic Usage
```python
import caspy

# Create oscillator
osc = caspy.Saw()
osc.set_frequency(440.0, 48000.0)  # A4 at 48kHz

# Render 1 second of audio
samples = osc.render(48000)
print(f"Generated {len(samples)} samples")
```

## Using with Jupyter Notebook

### Option 1: Start Jupyter from Virtual Environment
```bash
# Activate virtual environment
.venv/Scripts/Activate.ps1    # Windows
# source .venv/bin/activate    # Linux/macOS

# Start Jupyter
jupyter notebook
```

### Option 2: Register as Jupyter Kernel (Recommended)
```bash
# Activate virtual environment
.venv/Scripts/Activate.ps1

# Register kernel
python -m ipykernel install --user --name=caspy-dev --display-name="Python (CASPy)"

# Start Jupyter from anywhere
jupyter notebook

# In notebook: Kernel → Change Kernel → Python (CASPy)
```

### Jupyter Example
```python
import caspy
import matplotlib.pyplot as plt

# Create and configure oscillator
osc = caspy.Sine()
osc.set_frequency(440.0, 48000.0)

# Render and visualize
samples = osc.render(1000)
plt.figure(figsize=(12, 4))
plt.plot(samples)
plt.title("Sine Wave - 440 Hz")
plt.xlabel("Sample")
plt.ylabel("Amplitude")
plt.grid(True)
plt.show()
```

## Available Oscillators

| Oscillator | Description | Characteristics |
|------------|-------------|----------------|
| `Sine()` | Pure sine wave | Single fundamental, clean tone |
| `Saw()` | Sawtooth wave | All harmonics, bright/buzzy |
| `Square()` | Square wave | Odd harmonics only, hollow tone |
| `Triangle()` | Triangle wave | Odd harmonics, softer than square |

All oscillators use band-limited step (BLEP) synthesis to prevent aliasing.

## API Reference

### Common Methods

All oscillators share the same interface:

**`set_frequency(frequency: float, sample_rate: float)`**
- Set oscillator frequency in Hz
- `frequency`: Must be ≥ 0 and < sample_rate/2 (Nyquist)
- `sample_rate`: Must be > 0

**`reset_phase()`**
- Reset phase to zero
- Use for hard sync or retriggering

**`render_sample() -> float`**
- Generate single sample
- Returns value in range [-1.0, 1.0]
- Advances internal phase

**`render(num_samples: int) -> list[float]`**
- Generate multiple samples
- Preserves phase continuity across calls
- Returns list of floats

### Usage Examples
```python
import caspy

# Initialize
osc = caspy.Square()
osc.set_frequency(440.0, 48000.0)

# Single samples
sample1 = osc.render_sample()
sample2 = osc.render_sample()  # Phase continues

# Batch rendering
chunk1 = osc.render(1000)  # First 1000 samples
chunk2 = osc.render(1000)  # Next 1000 samples (continuous)

# Reset and restart
osc.reset_phase()
samples = osc.render(48000)  # Fresh from phase 0
```

### Audio Playback Example
```python
import caspy
import sounddevice as sd

# Create 440 Hz sawtooth
osc = caspy.Saw()
osc.set_frequency(440.0, 48000.0)
samples = osc.render(48000)  # 1 second

# Play audio
sd.play(samples, 48000)
sd.wait()
```

### Multi-Oscillator Example
```python
import caspy

# Create chord (A major: A, C#, E)
frequencies = [440.0, 554.37, 659.25]
sample_rate = 48000
duration = 48000  # 1 second

# Mix oscillators
mixed = [0.0] * duration
for freq in frequencies:
    osc = caspy.Sine()
    osc.set_frequency(freq, sample_rate)
    samples = osc.render(duration)
    mixed = [m + s/len(frequencies) for m, s in zip(mixed, samples)]

# Play chord
import sounddevice as sd
sd.play(mixed, sample_rate)
sd.wait()
```

## Development Workflow

### Directory Structure
```
Tools/CASPy/
├── CMakeLists.txt              # CMake build config
├── pyproject.toml              # Python package metadata
├── README.md                   # This file
├── .gitignore
├── bindings/                   # C++ source files
│   ├── caspy_module.cpp       # Main pybind11 module
│   ├── bind_core.cpp          # Core type bindings
│   └── bind_oscillators.cpp   # Oscillator bindings
└── examples/                   # Example notebooks
    └── oscillators.ipynb
```

### Building from Source
```bash
# Clean previous builds
rm -rf build dist _skbuild

# Build wheel
uv build --wheel --out-dir dist/

# Install
uv pip install --force-reinstall dist/caspy-*.whl
```

### Editable Install (for active development)
```bash
# Install in editable mode
uv pip install -e ".[dev]"

# After C++ changes, rebuild
uv pip install -e . --force-reinstall --no-deps

# Python changes are immediately visible (no rebuild needed)
```

### Running Tests
```bash
# Install test dependencies
uv pip install pytest

# Run tests (when implemented)
pytest tests/
```

## Troubleshooting

### "Module not found" Error
```bash
# Verify installation
python -c "import sys; print('\n'.join(sys.path))"
python -c "import caspy; print(caspy.__file__)"

# Reinstall if needed
uv pip uninstall caspy
uv pip install dist/caspy-*.whl
```

### CMake Build Errors

**Windows:** Ensure Visual Studio Build Tools are installed
```powershell
# Check for MSVC
where cl.exe
```

**Linux/macOS:** Install build essentials
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake

# macOS
xcode-select --install
```

### Jupyter Kernel Not Found
```bash
# List available kernels
jupyter kernelspec list

# Re-register kernel
python -m ipykernel install --user --name=caspy-dev --display-name="Python (CASPy)"
```

## License

[Specify your license here]

## Contributing

Contributions are welcome! Please ensure:
- C++ code follows CASPI coding standards
- New oscillators include Python bindings
- Examples demonstrate new features
- Code is tested on Windows, Linux, and macOS

## Links

- CASPI Repository: [Link to main repo]
- Documentation: [Link to docs]
- Issues: [Link to issue tracker]