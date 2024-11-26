# JAFFX: Affordable, portable, hackable audio fx for everybody

The Jaffx repository is a free, open-source tool for programming not only the JAFFX reprogrammable pedal, but also other projects using the [Daisy Seed](https://electro-smith.com/).

The repository is structured around use of a [single header file](./Jaffx.hpp) that wraps the [libDaisy toolchain](https://github.com/electro-smith/libDaisy). Its use is demonstrated through the concise examples in `src`, and augmented by helpful scripts for generating project files, building, flashing and more. 

To get started, follow the steps [here](https://github.com/electro-smith/DaisyWiki/wiki/1.-Setting-Up-Your-Development-Environment) to get your development environment set up. Once you have the daisy toolchain working, run the `init.sh` script to configure your local copy of this repository. From there, you should be able to use `run.sh` (or `SHIFT+CMD+B` if working in VSCode) to build programs and flash them to your Daisy. You can use `python projectGen.py <project_name>` to generate new projects in `src` from the template.

When developing for the Daisy, it is often useful to use serial monitoring for testing and debugging. Many examples in `src` demonstrate this. If developing in VSCode, we recommend installing Microsoft's [serial monitor extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor), which will add easy access to serial monitoring via the terminal panel.