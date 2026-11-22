/* The launcher's package (make_package.py), built in: its bytes between
 * gLauncherPackage and gLauncherPackageEnd. PACKAGE_BIN is its path. */
    .section .rodata
    .balign 16
    .globl gLauncherPackage
gLauncherPackage:
    .incbin PACKAGE_BIN
    .globl gLauncherPackageEnd
gLauncherPackageEnd:

#ifdef __ELF__
    .section .note.GNU-stack,"",@progbits
#endif
