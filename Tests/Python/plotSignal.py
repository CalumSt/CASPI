import matplotlib.pyplot as plt
import pandas as pd
import pathlib
import csv
import sys

"""
This is a Python script that creates a plot when pointed at a folder containing CSVs.
Assumes that the CSVs is in the format time/sample_index, sample \n
Saves the figures to a folder above the parent directory called Figures
Usage: python plotSignal.py <dataDir> <outputDir>

Dependencies:
    matplotlib
    numpy
    pathlib
    csv
    sys
"""

def plotSignal(signalDataDirectory: pathlib.Path, outputDirectory: pathlib.Path):
# Generate a list of file names
    directory = pathlib.Path(signalDataDirectory)
    files = [file for file in directory.glob('*')]  # Get all .csv files
    # loop over each
    for file in directory.glob('*.csv'):
        data = pd.read_csv(file)  # read file as two lists
        title = file.stem # get base file name

        createPlot(data, title, outputDirectory) # give to plotting function

def createPlot(data: pd.DataFrame, title: str, outputDir: pathlib.Path) -> None:
    plt.plot(data.iloc[:, 0], data.iloc[:, 1])
    plt.title(title)
    if isinstance(data.iloc[1, 1], int): # check if second entry is time (non-int) or sample index (int)
        plt.xlabel("Sample")
    else:
        plt.xlabel("Time")

    filename = title + ".png"
    outputFilepath = outputDir.joinpath(filename)
    plt.savefig(outputFilepath, dpi=300, bbox_inches="tight")
    print("Saved to " + str(outputFilepath))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python plotSignal.py <dataDir> <outputDir>")
        sys.exit(1)
    dataDir = pathlib.Path(sys.argv[1])
    outputDir = pathlib.Path(sys.argv[2])
    plotSignal(dataDir, outputDir)