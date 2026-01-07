SECTIONS
{
    . = 0;

    .entry : {
        KEEP(*(.entry))
    }

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }
}