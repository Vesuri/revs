| Asset embedding for the Revs Amiga build (the equivalent of Bin2Hunk).
|
| Paths are resolved relative to the amiga/ build directory (ASFLAGS -I"$(CURDIR)").
|
| ⚠ .incbin dependencies are INVISIBLE to `gcc -MMD`, so every file listed here must
| also be listed in the Makefile's INCBIN_DEPS.  Without that, editing or regenerating
| an embedded blob leaves a STALE copy in the binary while the on-disk file looks
| correct.  On the Atari port this exact trap shifted an embedded data block by one byte
| and cost a long debugging session.

	.section .rodata
	.align 4

| revs_mem.bin — the post-load 6502 memory image built from revs.ssd by
| tools/ssd_load.py.  Embedding it (rather than loading from disc at runtime) means
| every build boots the SAME initial state and the SAME code path, which is what makes
| a cross-build comparison mean anything at all.
	.global revs_mem_bin
	.global revs_mem_bin_end
revs_mem_bin:
	.incbin "../disasm/revs_mem.bin"
revs_mem_bin_end:
