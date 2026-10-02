| AmigaOS version string for Revs.
|
| Format (https://wiki.amigaos.net/wiki/Version_Strings):
|     $VER: <name> <version>.<revision> (<dd>.<mm>.<yyyy>)
| <name> is the game's TITLE, not the file name — the convention for a product.  The WHDLoad
| slave carries its own FILE name instead ("$VER: Revs.slave", whdload/RevsSlave.s): a slave is
| a component, the executable is the product.
|
| NOTHING REFERENCES THIS STRING, AND NOTHING MAY.  AmigaDOS `Version`, WHDLoad's crash report and
| every archive tool find a version by SCANNING the file for "$VER: ", so it only has to be present
| in the load image — which is exactly the shape --gc-sections deletes.  The section therefore
| carries SHF_GNU_RETAIN ("R"), which keeps it with no relocation pointing at it.  Re-verify after
| any change to LDFLAGS, elf2hunk or this file:
|     strings out/Revs.exe | grep '\$VER:'
|
| ⚠ The version lives in THREE places — keep them in step:
|     src/platform/amiga/version.s         this file (the executable)
|     whdload/RevsSlave.s                  slv_info + the slave's own $VER:
|     whdload/Revs Install/ReadMe          the History section
| The date is HARDCODED, not stamped at build time, so identical input gives an identical binary
| (byte-comparing two builds is a standard check here — docs/headless-fsuae.md).
	.section .rodata.version,"aR"
	.balign 2
	.asciz "$VER: Revs 0.91 (02.10.2026)"
	.balign 2
