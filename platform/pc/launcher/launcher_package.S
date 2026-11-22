/* The launcher's packages (make_package.py), built in: the usual game's
 * bytes between gLauncherPackage and gLauncherPackageEnd (PACKAGE_BIN is
 * its path), the widescreen game's between gLauncherWidePackage and
 * gLauncherWidePackageEnd (PACKAGE_WIDE_BIN; none without it). */
    .section .rodata
    .balign 16
    .globl gLauncherPackage
gLauncherPackage:
    .incbin PACKAGE_BIN
    .globl gLauncherPackageEnd
gLauncherPackageEnd:
    .balign 16
    .globl gLauncherWidePackage
gLauncherWidePackage:
#ifdef PACKAGE_WIDE_BIN
    .incbin PACKAGE_WIDE_BIN
#endif
    .globl gLauncherWidePackageEnd
gLauncherWidePackageEnd:

#ifdef __ELF__
    .section .note.GNU-stack,"",@progbits
#endif
