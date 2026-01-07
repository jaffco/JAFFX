This project demonstrates hot-loading a separate binary into a firmware and running the
contained native code.

There is a separate standalone project inside of this folder, under `./nativePlugin/`, which needs to be built first.

## Step 1: Write standalone code in the `nativePlugin/` folder.
Write standalone code in the `nativePlugin/` folder and export the desired functions.
- The `lpa_abi.h` file is used to export these functions, this is a purely C ABI. Use this file to bridge the gap between the standalone code and the main firmware (export only the bare minimum functions needed). There is an `LPA_Entry` struct where you can define more functions to implement in .c/.cpp files.
- Make sure to follow the example on how to properly define the `LPA_Entry` instance at the bottom of the source file. Use the proper attributes for alignment (based on the types of fields used) and preventing deadstripping of the struct or function definitions. To help with this, use:
    - `arm-none-eabi-objdump -h build/nativePluginTest.elf` to verify that there is a .entry section added at location 0x0.
    ```
    build/nativePluginTest.elf:     file format elf32-littlearm

    Sections:
    Idx Name          Size      VMA       LMA       File off  Algn
    0 .entry        0000000c  00000000  00000000  00001000  2**3
                    CONTENTS, ALLOC, LOAD, READONLY, DATA
    1 .text         00000024  0000000c  0000000c  0000100c  2**2
                    CONTENTS, ALLOC, LOAD, READONLY, CODE
    2 .comment      00000043  00000000  00000000  00001030  2**0
                    CONTENTS, READONLY
    3 .ARM.attributes 0000002e  00000000  00000000  00001073  2**0
                    CONTENTS, READONLY
    ```
    - `make disasm` will create a `.disasm` file showing the assembly source of the binary with the associated C/C++ code. Use this to ensure your code is present and not deadstripped.

## Step 2: Use dedicated `nativePlugin/Makefile` for standalone build
Running `make` will output a `.bin` that can be manually loaded into memory for running in the main firmware. For easier access, a header file containing the binary as a C-style array is automatically generated as well. Just include the header file in the main firmware and access it as normal.

## Step 3: Load into firmware and run
Because the `.entry` struct was placed at 0x0, it is the 0th element in the memory section where the binary is loaded into. Use a typecast to the `LPA_Entry` struct and functions can be called directly from there in C/C++ source files.