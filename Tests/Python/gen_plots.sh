# gen_plots.sh
#!/bin/bash

source .venv/Scripts/activate
pip install -r requirements.txt
python plotSignal.py ../Builds/Debug/GeneratedSignals ../Builds/Debug/Figures