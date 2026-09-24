ENTRY(_start)

SECTIONS
{
    . = 0x0;

    .text : ALIGN(4K)
    {
        *(.text)
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(.bss)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}