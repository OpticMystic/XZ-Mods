/* ARM glibc 2.13 expects PLT relocations inside the general REL range. */
SECTIONS
{
    .rel.dyn : { *(.rel.dyn) *(.rel.plt) }
}
INSERT BEFORE .ARM.exidx;
