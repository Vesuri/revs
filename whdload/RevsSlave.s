;*---------------------------------------------------------------------------
;  :Modul.	RevsSlave.s
;  :Contents.	WHDLoad slave for the Amiga port of "Revs" (Geoff Crammond, Acornsoft 1985)
;  :Author.	Vesuri
;  :History.	30.09.26 started, from the Rescue on Fractalus port's slave
;  :Requires.	WHDLoad 17+, whdload/kick13.s, an installed Kickstart 1.3 image
;  :Copyright.	Public Domain
;  :Language.	68000 Assembler
;  :Translator.	vasm (Devpac mode)
;---------------------------------------------------------------------------*
;
; WHY A KICKEMU AND NOT A PLAIN LOADER
;
; A plain WHDLoad slave runs the installed program with no operating system at all, and the Revs
; port is an ordinary AmigaDOS executable whose display takeover IS operating system calls
; (OpenLibrary graphics/dos, AllocMem, LoadView/WaitTOF, the VERTB IntVects takeover,
; ciaa.resource for the keyboard, Forbid/Permit) — and which reads the engine image off the
; player's BBC disc image with dos.library at startup.  So the slave boots a real Kickstart 1.3
; into WHDLoad's memory (WHDLoad's own kick13.s) and runs the unmodified executable inside it as a
; CLI program, exactly as the Rescue on Fractalus port does (its docs/whdload-slave.md §0 has the
; three designs that were weighed).  The installed "Revs" is byte for byte the file that runs from
; Workbench or a Shell; there is nothing for this slave to patch.
;
; 1.3 rather than 3.1: the game is 1.3-clean (graphics.library v33, dos.library v33 — PROGDIR:
; is tried only on V36+) and the 1.3 image is 256 KB against 3.1's 512 KB, which comes straight
; off the memory requirement.
;
; QUITTING
;
; The game's own quit is CTRL-Q, which returns through _bootdos below.  The WHDLoad QuitKey is
; keypad '*' — not F10, which is the BBC's f0 (return to the pits) — and WHDLoad can only see it
; through a moved VBR, i.e. on a 68010 or better.  On a stock A500 CTRL-Q is the way out, which is
; why _bootdos aborts unconditionally rather than dropping back to an invisible 1.3 CLI.
;
;---------------------------------------------------------------------------*

	IFND __VASM
	INCDIR	Include:
	ENDC
	INCLUDE	whdload.i
	INCLUDE	whdmacros.i

;============================================================================
; kick13.s configuration
;============================================================================

CHIPMEMSIZE	= $64000	;400 KB — measured need + margin, docs/phases.md §Phase 7
FASTMEMSIZE	= $80000	;512 KB, plus the separate 256 KB Kickstart image
NUMDRIVES	= 1		;NOT 0: only 3.1 survives a driveless boot, 1.2/1.3 crash
WPDRIVES	= %0000		;all emulated drives write protected
BLACKSCREEN			;1.3's boot colours all black -- no CLI flash before the game
BOOTDOS				;_bootdos below runs as a real CLI process
CACHECHIP			;instruction cache on (chip write-through) for accelerated machines
HDINIT				;mount slv_CurrentDir as DH0: -- BOOTDOS requires it
SEGTRACKER			;so a WHDLoad crash report names our hunk + offset
STACKSIZE	= 4096		;V33's initial CLI default is 4000 bytes
	IFD TUNE
DEBUG				;extra internal checks in the OS emulation
MEMFREE		= $200		;record the low-water mark of free chip/fast memory
	ENDC

slv_Version	= 17
slv_Flags	= WHDLF_NoError	;kick13.s ORs in EmulPriv (needed by exec.Supervisor)
slv_keyexit	= $5d		;keypad '*'.  Only seen via a moved VBR, i.e. 68010+.  Not F10 ($59):
				;that is the BBC's f0, SHIFT+F10 = return to the pits.

	INCLUDE	whdload/kick13.s

;============================================================================
; Slave strings
;============================================================================

slv_CurrentDir	dc.b	"data",0
slv_name	dc.b	"Revs",0
slv_copy	dc.b	"1985 Geoff Crammond / Acornsoft",0
slv_info	dc.b	"Amiga port by Vesuri",10
		dc.b	"version 0.90 (30.09.2026)",-1
		dc.b	"An unofficial, non-commercial fan project.",-1
		dc.b	"CTRL-Q quits.",10
		dc.b	"Keypad * also quits, on a 68010 or better.",0
slv_config	dc.b	0
		dc.b	"$VER: Revs.slave 0.90 (30.09.2026)",0
	EVEN

_program	dc.b	"Revs",0
_startup_error	dc.b	"Revs could not start. Check that data/revs.ssd is the "
		dc.b	"Revs Plus Revs 4 Tracks or Revs+ BBC disc image.",0
_args		dc.b	10		;empty argument line -- must be LF terminated
_args_end
	EVEN

;============================================================================
; _bootdos — called by kick13.s as a CLI process, with dos.library up
;============================================================================

_bootdos	lea	(_dosname,pc),a1
		move.l	(4),a6
		jsr	(_LVOOldOpenLibrary,a6)
		move.l	d0,a6			;A6 = dosbase
		tst.l	d0
		beq	.dos_err

		lea	(_program,pc),a0
		move.l	a0,d1
		jsr	(_LVOLoadSeg,a6)
		move.l	d0,d7			;D7 = segment list (BPTR)
		beq	.program_err

		move.l	d7,a1
		add.l	a1,a1
		add.l	a1,a1			;BPTR -> APTR
		moveq	#_args_end-_args,d0
		lea	(_args,pc),a0
		jsr	(4,a1)			;first hunk + 4 = the code

		; 20 (FAIL) = the engine image could not be read or was not the supported REVS2,
		; reported before the display was taken over.  Say so rather than "OK".
		tst.l	d0
		bne.s	.game_err
		pea	TDREASON_OK
		move.l	(_resload,pc),a2
		jmp	(resload_Abort,a2)

.game_err	pea	(_startup_error,pc)
		pea	TDREASON_FAILMSG
		move.l	(_resload,pc),a2
		jmp	(resload_Abort,a2)

.program_err	jsr	(_LVOIoErr,a6)
		pea	(_program,pc)
		move.l	d0,-(a7)
		pea	TDREASON_DOSREAD
		move.l	(_resload,pc),a2
		jmp	(resload_Abort,a2)

.dos_err	clr.l	-(a7)
		clr.l	-(a7)
		pea	TDREASON_OSEMUFAIL
		move.l	(_resload,pc),a2
		jmp	(resload_Abort,a2)

	END
