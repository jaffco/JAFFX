# JAFFX: Affordable, portable, hackable digital audio for everybody

The Jaffx repository is a free, open-source tool for programming not only the JAFFX reprogrammable pedal, but also other projects using the [Daisy Seed](https://electro-smith.com/).

The repository is structured around use of a [single header file](./Jaffx.hpp) that wraps the [libDaisy hardware abstraction library](https://github.com/electro-smith/libDaisy) and provides some additional utilities. Its use is demonstrated through the concise examples in `src`, and augmented by helpful scripts for generating project files, building, flashing and more. 

To get started, [install the Daisy Toolchain](https://daisy.audio/tutorials/cpp-dev-env/#1-install-the-toolchain). Once installed, run the `init.sh` script to configure your local copy of this repository. From there, you should be able to use `run.sh` (or `SHIFT+CMD+B` if working in VSCode) to build programs and flash them to your Daisy. You can use `python projectGen.py <project_name>` to generate new projects in `src` from the template.

When developing for the Daisy, it is often useful to use serial monitoring for testing and debugging. Many examples in `src` demonstrate this. If developing in VSCode, we recommend installing Microsoft's [serial monitor extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor), which will add easy access to serial monitoring via the terminal panel.