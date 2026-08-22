; Emacsc style mode select   -*- asm -*-
;-----------------------------------------------------------------------------
;
; linear.asm - DOOM95 renderer inner loops.
;
; Recreation of the original module listed as source #54 in the
; DOOM95.EXE CodeView debug info.  Contains the texture mapping
; column and span routines, like id's DOS tmap.S shipped in
; README.asm of the linuxdoom release, adapted to this tree's
; globals and assembled with NASM (-f win32).
;
; The original Watcom code patched its own immediates ("convice
; tasm to modify code") and relied on 256-byte-aligned colormaps
; for an aliasing trick; both are avoided here so the code stays
; safe under DEP/W^X.  The arithmetic is identical.
;
; Assemble:  nasm -f win32 linear.asm -o obj/linear.o
;
;-----------------------------------------------------------------------------

BITS 32

%define SCREENWIDTH	320	; doomdef.h

SECTION .text

extern	_dc_yl
extern	_dc_yh
extern	_dc_x
extern	_dc_iscale
extern	_dc_texturemid
extern	_dc_source
extern	_dc_colormap
extern	_centery
extern	_ylookup
extern	_columnofs

extern	_ds_y
extern	_ds_x1
extern	_ds_x2
extern	_ds_colormap
extern	_ds_xfrac
extern	_ds_yfrac
extern	_ds_xstep
extern	_ds_ystep
extern	_ds_source


;================
;
; R_DrawColumn
;
; A column is a vertical slice from a wall texture at constant
; depth; a fixed point DDA walks down the source texels.
;
; void R_DrawColumn_Asm (void)
;
;================

	align	16
global	_R_DrawColumn_Asm
_R_DrawColumn_Asm:

	pushad

; pixel count = dc_yh - dc_yl + 1; nothing to do when negative
	mov	edx,[_dc_yh]
	sub	edx,[_dc_yl]
	js	near .done
	inc	edx			; number of pixels

; framebuffer destination address
	mov	ecx,[_dc_yl]
	mov	edi,[_ylookup + ecx*4]
	mov	eax,[_dc_x]
	add	edi,[_columnofs + eax*4]

; frac = dc_texturemid + (dc_yl - centery) * dc_iscale,
; carried with 25 fractional bits so the shift below leaves
; exactly the 7 significant bits of a 128 tall wall texture.
	mov	ebx,[_dc_iscale]
	mov	eax,[_centery]
	sub	eax,ecx			; centery - dc_yl
	imul	eax,ebx			; edx untouched
	mov	ebp,[_dc_texturemid]
	sub	ebp,eax
	shl	ebp,9			; frac, 25 frac bits

	shl	ebx,9			; fracstep, 25 frac bits

	mov	esi,[_dc_source]
	mov	eax,[_dc_colormap]

;	eax	colormap
;	ebx	fracstep
;	ecx	scratch
;	edx	pixel countdown
;	esi	texture source
;	edi	moving destination pointer
;	ebp	frac

;	texel = (frac >> 25) & 127 -- the mask is implicit,
;	a logical right shift of a doubled value keeps 7 bits.

	align	16
.pixelloop:
	mov	ecx,ebp
	shr	ecx,25
	movzx	ecx,byte [esi+ecx]	; source texel
	mov	cl,[eax+ecx]		; colormap translate
	mov	[edi],cl
	add	edi,SCREENWIDTH
	add	ebp,ebx
	dec	edx
	jnz	short .pixelloop

.done:
	popad
	ret


;================
;
; R_DrawSpan
;
; Horizontal slice of a 64x64 flat with u,v stepping.
;
; id's DOS tmap.S packed both coordinates into one register each
; (scaled <<10 / >>6), which silently drops the six lowest
; fraction bits of every component; accumulated carries from
; those bits put single texels out of place on longer spans.
; This version advances full 32 bit fractions instead, matching
; the C reference byte for byte; the two steps live in stack
; slots so the registers stay allocated to hot values.
;
; void R_DrawSpan_Asm (void)
;
;================

	align	16
global	_R_DrawSpan_Asm
_R_DrawSpan_Asm:

	pushad
	sub	esp,12			; [esp]=xstep [esp+4]=ystep [esp+8]=count

; framebuffer destination address
	mov	ecx,[_ds_y]
	mov	edi,[_ylookup + ecx*4]
	mov	eax,[_ds_x1]
	add	edi,[_columnofs + eax*4]

; span length
	mov	eax,[_ds_x2]
	sub	eax,[_ds_x1]
	js	short .hdone		; nothing to draw
	inc	eax			; number of pixels
	mov	[esp+8],eax

	mov	ebp,[_ds_yfrac]
	mov	ebx,[_ds_xfrac]
	mov	eax,[_ds_xstep]
	mov	[esp],eax
	mov	eax,[_ds_ystep]
	mov	[esp+4],eax

	mov	esi,[_ds_source]
	mov	eax,[_ds_colormap]

;	eax	colormap
;	ebx	u coordinate, 16 frac bits
;	ecx	scratch
;	edx	scratch
;	esi	flat source
;	edi	moving destination pointer
;	ebp	v coordinate, 16 frac bits

	align	16
.hpixelloop:
	mov	ecx,ebp
	shr	ecx,10
	and	ecx,4032		; v row contribution
	mov	edx,ebx
	shr	edx,16
	and	edx,63			; u column contribution
	or	ecx,edx			; complete spot calculation
	movzx	ecx,byte [esi+ecx]	; source texel
	mov	cl,[eax+ecx]		; colormap translate
	mov	[edi],cl
	inc	edi
	add	ebx,[esp]		; u += ustep
	add	ebp,[esp+4]		; v += vstep
	dec	dword [esp+8]
	jnz	short .hpixelloop

.hdone:
	add	esp,12
	popad
	ret
