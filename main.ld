ENTRY(main)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 128K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 80K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector)) /* Startup code */
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } >FLASH

    /* Constant data goes into FLASH */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)         /* .rodata sections (constants, strings, etc.) */
        *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
        . = ALIGN(4);
        _sromfs = .;
        *(.rom*)
        _eromfs = .;
        . = ALIGN(4);
    } >FLASH

    /* Initialized data will initially be loaded in FLASH at the end of the .text section. */
    _sidata = LOADADDR(.data);
    .data :
    {
        _sdata = .;
        *(.init_array)
        *(.data)        /* Initialized data */
        *(.data.*)        /* Initialized data */
        _edata = .;
    } >RAM AT >FLASH

    .bss(NOLOAD) : {
        _sbss = .;
        __bss_start__ = .;
        *(.bss)         /* Zero-filled run time allocate data memory */
        *(.bss.*)         /* Zero-filled run time allocate data memory */
        _ebss = .;
        __bss_end__ = .;
        end = .;        /* For checking stack-heap collision detection from sbrk() */
    } >RAM AT >RAM

    /* Remove information from the standard libraries */
    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
        libnosys.a ( * )
    }
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
