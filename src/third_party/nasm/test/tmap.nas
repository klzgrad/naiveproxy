;; NASM note: this file abuses the section flags in such a way that
;; NASM 0.98.37 broke when this was compiled with:
;; nasm -o tmap.o -f elf -DLINUX tmap.nas

;;-----------------------------------------------------------------------------
;;
;; $Id$
;;
;; Copyright (C) 1998-2000 by DooM Legacy Team.
;;
;; This program is free software; you can redistribute it and/or
;; modify it under the terms of the GNU General Public License
;; as published by the Free Software Foundation; either version 2
;; of the License, or (at your option) any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;;
;; $Log$
;; Revision 1.2  2003/09/10 23:33:38  hpa
;; Use the version of tmap.nas that actually caused problems
;;
;; Revision 1.10  2001/02/24 13:35:21  bpereira
;; no message
;;
;; Revision 1.9  2001/02/10 15:24:19  hurdler
;; Apply Rob's patch for Linux version
;;
;; Revision 1.8  2000/11/12 09:48:15  bpereira
;; no message
;;
;; Revision 1.7  2000/11/06 20:52:16  bpereira
;; no message
;;
;; Revision 1.6  2000/11/03 11:48:40  hurdler
;; Fix compiling problem under win32 with 3D-Floors and FragglScript (to verify!)
;;
;; Revision 1.5  2000/11/03 03:27:17  stroggonmeth
;; Again with the bug fixing...
;;
;; Revision 1.4  2000/11/02 17:50:10  stroggonmeth
;; Big 3Dfloors & FraggleScript commit!!
;;
;; Revision 1.3  2000/04/24 20:24:38  bpereira
;; no message
;;
;; Revision 1.2  2000/02/27 00:42:11  hurdler
;; fix CR+LF problem
;;
;; Revision 1.1.1.1  2000/02/22 20:32:32  hurdler
;; Initial import into CVS (v1.29 pr3)
;;
;;
;; DESCRIPTION:
;;      assembler optimised rendering code for software mode
;;      draw floor spans, and wall columns.
;;
;;-----------------------------------------------------------------------------


[BITS 32]

%ifdef LINUX
%macro cextern 1
[extern %1]
%endmacro

%macro cglobal 1
[global %1]
%endmacro

%define CODE_SEG .data
%else
%macro cextern 1
%define %1 _%1
[extern %1]
%endmacro

%macro cglobal 1
%define %1 _%1
[global %1]
%endmacro

%define CODE_SEG .text                         
%endif


;; externs
;; columns
cextern dc_x
cextern dc_yl
cextern dc_yh
cextern ylookup
cextern columnofs
cextern dc_source
cextern dc_texturemid
cextern dc_iscale
cextern centery
cextern dc_colormap
cextern dc_transmap
cextern colormaps

;; spans
cextern ds_x1
cextern ds_x2
cextern ds_y
cextern ds_xfrac
cextern ds_yfrac
cextern ds_xstep
cextern ds_ystep
cextern ds_source
cextern ds_colormap
;cextern ds_textureheight

; polygon edge rasterizer
cextern prastertab


;;----------------------------------------------------------------------
;;
;; R_DrawColumn
;;
;; New  optimised version 10-01-1998 by D.Fabrice and P.Boris
;; TO DO: optimise it much farther... should take at most 3 cycles/pix
;;      once it's fixed, add code to patch the offsets so that it
;;      works in every screen width.
;;
;;----------------------------------------------------------------------

[SECTION .data]

;;.align        4
loopcount       dd      0
pixelcount      dd      0
tystep          dd      0

[SECTION CODE_SEG write]

;----------------------------------------------------------------------------
;fixed_t FixedMul (fixed_t a, fixed_t b)
;----------------------------------------------------------------------------
cglobal FixedMul
;       align   16
FixedMul:
        mov     eax,[esp+4]
        imul    dword [esp+8]
        shrd    eax,edx,16
        ret

;----------------------------------------------------------------------------
;fixed_t FixedDiv2 (fixed_t a, fixed_t b);
;----------------------------------------------------------------------------
cglobal FixedDiv2
;       align   16
FixedDiv2:
        mov     eax,[esp+4]
        mov     edx,eax                 ;; these two instructions allow the next
        sar     edx,31                  ;; two to pair, on the Pentium processor.
        shld    edx,eax,16
        sal     eax,16
        idiv    dword [esp+8]
        ret

;----------------------------------------------------------------------------
; void  ASM_PatchRowBytes (int rowbytes);
;----------------------------------------------------------------------------
cglobal ASM_PatchRowBytes
;       align   16
ASM_PatchRowBytes:
        mov     eax,[esp+4]
        mov     [p1+2],eax
        mov     [p2+2],eax
        mov     [p3+2],eax
        mov     [p4+2],eax
        mov     [p5+2],eax
        mov     [p6+2],eax
        mov     [p7+2],eax
        mov     [p8+2],eax
        mov     [p9+2],eax
        mov     [pa+2],eax
        mov     [pb+2],eax
        mov     [pc+2],eax
        mov     [pd+2],eax
        mov     [pe+2],eax
        mov     [pf+2],eax
        mov     [pg+2],eax
        mov     [ph+2],eax
        mov     [pi+2],eax
        mov     [pj+2],eax
        mov     [pk+2],eax
        mov     [pl+2],eax
        mov     [pm+2],eax
        mov     [pn+2],eax
        mov     [po+2],eax
        mov     [pp+2],eax
        mov     [pq+2],eax
        add     eax,eax
        mov     [q1+2],eax
        mov     [q2+2],eax
        mov     [q3+2],eax
        mov     [q4+2],eax
        mov     [q5+2],eax
        mov     [q6+2],eax
        mov     [q7+2],eax
        mov     [q8+2],eax
        ret


;----------------------------------------------------------------------------
; 8bpp column drawer
;----------------------------------------------------------------------------

cglobal R_DrawColumn_8
;       align   16
R_DrawColumn_8:
        push    ebp                     ;; preserve caller's stack frame pointer
        push    esi                     ;; preserve register variables
        push    edi
        push    ebx
;;
;; dest = ylookup[dc_yl] + columnofs[dc_x];
;;
        mov     ebp,[dc_yl]
        mov     ebx,ebp
        mov     edi,[ylookup+ebx*4]
        mov     ebx,[dc_x]
        add     edi,[columnofs+ebx*4]  ;; edi = dest
;;
;; pixelcount = yh - yl + 1
;;
        mov     eax,[dc_yh]
        inc     eax
        sub     eax,ebp                 ;; pixel count
        mov     [pixelcount],eax        ;; save for final pixel
        jle     near vdone                   ;; nothing to scale
;;
;; frac = dc_texturemid - (centery-dc_yl)*fracstep;
;;
        mov     ecx,[dc_iscale]        ;; fracstep
        mov     eax,[centery]
        sub     eax,ebp
        imul    eax,ecx
        mov     edx,[dc_texturemid]
        sub     edx,eax
        mov     ebx,edx
        shr     ebx,16                  ;; frac int.
        and     ebx,0x7f
        shl     edx,16                  ;; y frac up

        mov     ebp,ecx
        shl     ebp,16                  ;; fracstep f. up
        shr     ecx,16                  ;; fracstep i. ->cl
        and     cl,0x7f
        mov     esi,[dc_source]
;;
;; lets rock :) !
;;
        mov     eax,[pixelcount]
        mov     dh,al
        shr     eax,2
        mov     ch,al                   ;; quad count
        mov     eax,[dc_colormap]
        test    dh,0x3
        je      near v4quadloop
;;
;;  do un-even pixel
;;
        test    dh,0x1
        je      two_uneven

        mov     al,[esi+ebx]            ;; prep un-even loops
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
        and     bl,0x7f                 ;; mask 0-127 texture index
        mov     [edi],dl                ;; output pixel
p1:     add     edi,0x12345678
;;
;;  do two non-quad-aligned pixels
;;
two_uneven:
        test    dh,0x2
        je      f3

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
        and     bl,0x7f                 ;; mask 0-127 texture index
        mov     [edi],dl                ;; output pixel
        mov     al,[esi+ebx]
        add     edx,ebp                 ;; fetch source texel
        adc     bl,cl                   ;; ypos f += ystep f
        mov     dl,[eax]                ;; ypos i += ystep i
        and     bl,0x7f                 ;; colormap texel
p2:     add     edi,0x12345678          ;; mask 0-127 texture index
        mov     [edi],dl
p3:     add     edi,0x12345678          ;; output pixel
;;
;;  test if there was at least 4 pixels
;;
f3:
        test    ch,0xff                 ;; test quad count
        je      near vdone
;;
;; ebp : ystep frac. upper 16 bits
;; edx : y     frac. upper 16 bits
;; ebx : y     i.    lower 7 bits,  masked for index
;; ecx : ch = counter, cl = y step i.
;; eax : colormap aligned 256
;; esi : source texture column
;; edi : dest screen
;;
v4quadloop:
        mov     dh,0x7f                 ;; prep mask
align 4
vquadloop:
        mov     al,[esi+ebx]            ;; prep loop
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
        mov     [edi],dl                ;; output pixel
        and     bl,0x7f                 ;; mask 0-127 texture index

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
p4:     add     edi,0x12345678
        mov     dl,[eax]
        and     bl,0x7f
        mov     [edi],dl

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
p5:     add     edi,0x12345678
        mov     dl,[eax]
        and     bl,0x7f
        mov     [edi],dl

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
p6:     add     edi,0x12345678
        mov     dl,[eax]
        and     bl,0x7f
        mov     [edi],dl

p7:     add     edi,0x12345678

        dec     ch
        jne     vquadloop

vdone:
        pop     ebx                     ;; restore register variables
        pop     edi
        pop     esi
        pop     ebp                     ;; restore caller's stack frame pointer
        ret

;;----------------------------------------------------------------------
;;13-02-98:
;;      R_DrawSkyColumn : same as R_DrawColumn but:
;;
;;      - wrap around 256 instead of 127.
;;      this is needed because we have a higher texture for mouselook,
;;      we need at least 200 lines for the sky.
;;
;;      NOTE: the sky should never wrap, so it could use a faster method.
;;            for the moment, we'll still use a wrapping method...
;;
;;      IT S JUST A QUICK CUT N PASTE, WAS NOT OPTIMISED AS IT SHOULD BE !!!
;;
;;----------------------------------------------------------------------

cglobal R_DrawSkyColumn_8
;       align   16
R_DrawSkyColumn_8:
        push    ebp
        push    esi
        push    edi
        push    ebx
;;
;; dest = ylookup[dc_yl] + columnofs[dc_x];
;;
        mov     ebp,[dc_yl]
        mov     ebx,ebp
        mov     edi,[ylookup+ebx*4]
        mov     ebx,[dc_x]
        add     edi,[columnofs+ebx*4]   ;; edi = dest
;;
;; pixelcount = yh - yl + 1
;;
        mov     eax,[dc_yh]
        inc     eax
        sub     eax,ebp                 ;; pixel count
        mov     [pixelcount],eax        ;; save for final pixel
        jle     near    vskydone        ;; nothing to scale
;;
;; frac = dc_texturemid - (centery-dc_yl)*fracstep;
;;
        mov     ecx,[dc_iscale]        ;; fracstep
        mov     eax,[centery]
        sub     eax,ebp
        imul    eax,ecx
        mov     edx,[dc_texturemid]
        sub     edx,eax
        mov     ebx,edx
        shr     ebx,16                  ;; frac int.
        and     ebx,0xff
        shl     edx,16                  ;; y frac up
        mov     ebp,ecx
        shl     ebp,16                  ;; fracstep f. up
        shr     ecx,16                  ;; fracstep i. ->cl
        mov     esi,[dc_source]
;;
;; lets rock :) !
;;
        mov     eax,[pixelcount]
        mov     dh,al
        shr     eax,0x2
        mov     ch,al                   ;; quad count
        mov     eax,[dc_colormap]
        test    dh,0x3
        je      vskyquadloop
;;
;;  do un-even pixel
;;
        test    dh,0x1
        je      f2
        mov     al,[esi+ebx]            ;; prep un-even loops
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
        mov     [edi],dl                ;; output pixel
p8:     add     edi,0x12345678
;;
;;  do two non-quad-aligned pixels
;;
f2:     test    dh,0x2
        je      skyf3

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
        mov     [edi],dl                ;; output pixel

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp                 ;; ypos f += ystep f
        adc     bl,cl                   ;; ypos i += ystep i
        mov     dl,[eax]                ;; colormap texel
p9:     add     edi,0x12345678
        mov     [edi],dl                ;; output pixel

pa:     add     edi,0x12345678
;;
;;  test if there was at least 4 pixels
;;
skyf3:  test    ch,0xff                 ;; test quad count
        je      vskydone
;;
;; ebp : ystep frac. upper 24 bits
;; edx : y     frac. upper 24 bits
;; ebx : y     i.    lower 7 bits,  masked for index
;; ecx : ch = counter, cl = y step i.
;; eax : colormap aligned 256
;; esi : source texture column
;; edi : dest screen
;;
align 4
vskyquadloop:
        mov     al,[esi+ebx]            ;; prep loop
        add     edx,ebp                 ;; ypos f += ystep f
        mov     dl,[eax]                ;; colormap texel
        adc     bl,cl                   ;; ypos i += ystep i
        mov     [edi],dl                ;; output pixel

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
pb:     add     edi,0x12345678
        mov     dl,[eax]
        mov     [edi],dl

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
pc:     add     edi,0x12345678
        mov     dl,[eax]
        mov     [edi],dl

        mov     al,[esi+ebx]            ;; fetch source texel
        add     edx,ebp
        adc     bl,cl
pd:     add     edi,0x12345678
        mov     dl,[eax]
        mov     [edi],dl

pe:     add     edi,0x12345678

        dec     ch
        jne     vskyquadloop
vskydone:
        pop     ebx
        pop     edi
        pop     esi
        pop     ebp
        ret


;;----------------------------------------------------------------------
;; R_DrawTranslucentColumn_8
;;
;; Vertical column texture drawer, with transparency. Replaces Doom2's
;; 'fuzz' effect, which was not so beautiful.
;; Transparency is always impressive in some way, don't know why...
;;----------------------------------------------------------------------

cglobal R_DrawTranslucentColumn_8
R_DrawTranslucentColumn_8:
        push    ebp                     ;; preserve caller's stack frame pointer
        push    esi                     ;; preserve register variables
        push    edi
        push    ebx
;;
;; dest = ylookup[dc_yl] + columnofs[dc_x];
;;
        mov     ebp,[dc_yl]
        mov     ebx,ebp
        mov     edi,[ylookup+ebx*4]
        mov     ebx,[dc_x]
        add     edi,[columnofs+ebx*4]   ;; edi = dest
;;
;; pixelcount = yh - yl + 1
;;
        mov     eax,[dc_yh]
        inc     eax
        sub     eax,ebp                 ;; pixel count
        mov     [pixelcount],eax        ;; save for final pixel
        jle     near    vtdone         ;; nothing to scale
;;
;; frac = dc_texturemid - (centery-dc_yl)*fracstep;
;;
        mov     ecx,[dc_iscale]        ;; fracstep
        mov     eax,[centery]
        sub     eax,ebp
        imul    eax,ecx
        mov     edx,[dc_texturemid]
        sub     edx,eax
        mov     ebx,edx

        shr     ebx,16                  ;; frac int.
        and     ebx,0x7f
        shl     edx,16                  ;; y frac up

        mov     ebp,ecx
        shl     ebp,16                  ;; fracstep f. up
        shr     ecx,16                  ;; fracstep i. ->cl
        and     cl,0x7f
        push    cx
        mov     ecx,edx
        pop     cx
        mov     edx,[dc_colormap]
        mov     esi,[dc_source]
;;
;; lets rock :) !
;;
        mov     eax,[pixelcount]
        shr     eax,0x2
        test    byte [pixelcount],0x3
        mov     ch,al                   ;; quad count
        mov     eax,[dc_transmap]
        je      vt4quadloop
;;
;;  do un-even pixel
;;
        test    byte [pixelcount],0x1
        je      trf2

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     ecx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     dl,[edx]
        mov     [edi],dl
pf:     add     edi,0x12345678
;;
;;  do two non-quad-aligned pixels
;;
trf2:    test    byte [pixelcount],0x2
        je      trf3

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     ecx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     dl,[edx]
        mov     [edi],dl
pg:     add     edi,0x12345678

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     ecx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     dl,[edx]
        mov     [edi],dl
ph:     add     edi,0x12345678
;;
;;  test if there was at least 4 pixels
;;
trf3:   test    ch,0xff                 ;; test quad count
        je near vtdone

;;
;; ebp : ystep frac. upper 24 bits
;; edx : y     frac. upper 24 bits
;; ebx : y     i.    lower 7 bits,  masked for index
;; ecx : ch = counter, cl = y step i.
;; eax : colormap aligned 256
;; esi : source texture column
;; edi : dest screen
;;
vt4quadloop:
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [tystep],ebp
pi:     add     edi,0x12345678
        mov     al,[edi]                ;; fetch dest  : index into colormap
pj:     sub     edi,0x12345678
        mov     ebp,edi
pk:     sub     edi,0x12345678
        jmp short inloop
align 4
vtquadloop:
        add     ecx,[tystep]
        adc     bl,cl
q1:     add     ebp,0x23456789
        and     bl,0x7f
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     dl,[edx]
        mov     [edi],dl
        mov     al,[ebp]                ;; fetch dest   : index into colormap
inloop:
        add     ecx,[tystep]
        adc     bl,cl
q2:     add     edi,0x23456789
        and     bl,0x7f
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     dl,[edx]
        mov     [ebp+0x0],dl
        mov     al,[edi]                ;; fetch dest   : index into colormap

        add     ecx,[tystep]
        adc     bl,cl
q3:     add     ebp,0x23456789
        and     bl,0x7f
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     dl,[edx]
        mov     [edi],dl
        mov     al,[ebp]                ;; fetch dest   : index into colormap

        add     ecx,[tystep]
        adc     bl,cl
q4:     add     edi,0x23456789
        and     bl,0x7f
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     dl,[edx]
        mov     [ebp],dl
        mov     al,[edi]                ;; fetch dest   : index into colormap

        dec     ch
        jne     vtquadloop
vtdone:
        pop     ebx
        pop     edi
        pop     esi
        pop     ebp
        ret


;;----------------------------------------------------------------------
;; R_DrawShadeColumn
;;
;;   for smoke..etc.. test.
;;----------------------------------------------------------------------
cglobal R_DrawShadeColumn_8
R_DrawShadeColumn_8:
        push    ebp                     ;; preserve caller's stack frame pointer
        push    esi                     ;; preserve register variables
        push    edi
        push    ebx

;;
;; dest = ylookup[dc_yl] + columnofs[dc_x];
;;
        mov     ebp,[dc_yl]
        mov     ebx,ebp
        mov     edi,[ylookup+ebx*4]
        mov     ebx,[dc_x]
        add     edi,[columnofs+ebx*4]  ;; edi = dest
;;
;; pixelcount = yh - yl + 1
;;
        mov     eax,[dc_yh]
        inc     eax
        sub     eax,ebp                 ;; pixel count
        mov     [pixelcount],eax       ;; save for final pixel
        jle near shdone                ;; nothing to scale
;;
;; frac = dc_texturemid - (centery-dc_yl)*fracstep;
;;
        mov     ecx,[dc_iscale]        ;; fracstep
        mov     eax,[centery]
        sub     eax,ebp
        imul    eax,ecx
        mov     edx,[dc_texturemid]
        sub     edx,eax
        mov     ebx,edx
        shr     ebx,16                  ;; frac int.
        and     ebx,byte +0x7f
        shl     edx,16                  ;; y frac up

        mov     ebp,ecx
        shl     ebp,16                  ;; fracstep f. up
        shr     ecx,16                  ;; fracstep i. ->cl
        and     cl,0x7f

        mov     esi,[dc_source]
;;
;; lets rock :) !
;;
        mov     eax,[pixelcount]
        mov     dh,al
        shr     eax,2
        mov     ch,al                   ;; quad count
        mov     eax,[colormaps]
        test    dh,3
        je      sh4quadloop
;;
;;  do un-even pixel
;;
        test    dh,0x1
        je      shf2

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     edx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     [edi],dl
pl:     add     edi,0x12345678
;;
;;  do two non-quad-aligned pixels
;;
shf2:
        test    dh,0x2
        je      shf3

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     edx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     [edi],dl
pm:     add     edi,0x12345678

        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        add     edx,ebp
        adc     bl,cl
        mov     al,[edi]                ;; fetch dest  : index into colormap
        and     bl,0x7f
        mov     dl,[eax]
        mov     [edi],dl
pn:     add     edi,0x12345678
;;
;;  test if there was at least 4 pixels
;;
shf3:
        test    ch,0xff                 ;; test quad count
        je near shdone

;;
;; ebp : ystep frac. upper 24 bits
;; edx : y     frac. upper 24 bits
;; ebx : y     i.    lower 7 bits,  masked for index
;; ecx : ch = counter, cl = y step i.
;; eax : colormap aligned 256
;; esi : source texture column
;; edi : dest screen
;;
sh4quadloop:
        mov     dh,0x7f                 ;; prep mask
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [tystep],ebp
po:     add     edi,0x12345678
        mov     al,[edi]                ;; fetch dest  : index into colormap
pp:     sub     edi,0x12345678
        mov     ebp,edi
pq:     sub     edi,0x12345678
        jmp short shinloop

align  4
shquadloop:
        add     edx,[tystep]
        adc     bl,cl
        and     bl,dh
q5:     add     ebp,0x12345678
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [edi],dl
        mov     al,[ebp]                ;; fetch dest : index into colormap
shinloop:
        add     edx,[tystep]
        adc     bl,cl
        and     bl,dh
q6:     add     edi,0x12345678
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [ebp],dl
        mov     al,[edi]                ;; fetch dest : index into colormap

        add     edx,[tystep]
        adc     bl,cl
        and     bl,dh
q7:     add     ebp,0x12345678
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [edi],dl
        mov     al,[ebp]                ;; fetch dest : index into colormap

        add     edx,[tystep]
        adc     bl,cl
        and     bl,dh
q8:     add     edi,0x12345678
        mov     dl,[eax]
        mov     ah,[esi+ebx]            ;; fetch texel : colormap number
        mov     [ebp],dl
        mov     al,[edi]                ;; fetch dest : index into colormap

        dec     ch
        jne     shquadloop

shdone:
        pop     ebx                     ;; restore register variables
        pop     edi
        pop     esi
        pop     ebp                     ;; restore caller's stack frame pointer
        ret



;;----------------------------------------------------------------------
;;
;;      R_DrawSpan
;;
;;      Horizontal texture mapping
;;
;;----------------------------------------------------------------------


[SECTION .data]

oldcolormap     dd      0

[SECTION CODE_SEG write]

cglobal R_DrawSpan_8
R_DrawSpan_8:
        push    ebp                     ;; preserve caller's stack frame pointer
        push    esi                     ;; preserve register variables
        push    edi
        push    ebx
;;
;; initilise registers
;;
  
        mov     edx, [ds_xfrac]
        mov     eax, [ds_ystep]
        ror     edx, 14
        ror     eax, 15
        mov      bl, dl
        mov     ecx, [ds_xstep]
        mov      dh, al
        mov      ax, 1
        mov     [tystep], eax


        mov     eax, [ds_yfrac]
        ror     ecx, 13
        ror     eax, 16
        mov      dl, cl
        mov      bh, al
        xor      cx, cx
        and     ebx, 0x3fff
        mov     [pixelcount],ecx

        mov     ecx, [ds_x2]
        mov     edi, [ds_y]
        mov     esi, [ds_x1]
        mov     edi, [ylookup+edi*4]
        mov     ebp, ebx
        add     edi, [columnofs+esi*4]
        sub     esi, ecx                ;; pixel count
        shr     ebp, 2
        mov     ecx, [ds_colormap]
        mov      ax, si
        mov     esi, [ds_source]
        sar      ax,1
        jnc     near .midloop           ;; check parity

;   summary
; edx = high16bit xfrac[0..13], ah=ystep[16..24] al=xtep[14..21]
; ebx = high16bit =0, bh=yfrac[16..24], bl=xfrac[14..21] 
; ecx = colormap table cl=0 (colormap is aligned 8 bits)
; eax = high16bit yfrac[0..15], dx = count
; esi = flat texture source
; edi = screeen buffer destination
; ebp = work register
; pixelcount = high16bit xstep[0..13] rest to 0
; tystep     = high16bit ystep[0..15] low 16 bit = 2 (increment of count)

align 4
.loop
        add     eax, [tystep]
         mov      cl, [esi+ebp]
        adc      bh, dh
         mov      cl, [ecx]
        and      bh, 0x3f 
         mov   [edi], cl
        mov     ebp, ebx        
         inc     edi
         shr     ebp, 2

.midloop:
        add     edx, [pixelcount]
         mov      cl, [esi+ebp]
        adc      bl, dl
         mov      cl, [ecx]
        mov     ebp, ebx 
         mov   [edi], cl
        inc     edi
         shr     ebp, 2

        test    eax, 0xffff
        jnz     near .loop

.hdone: pop     ebx                     ;; restore register variables
        pop     edi
        pop     esi
        pop     ebp                     ;; restore caller's stack frame pointer
        ret


[SECTION .data]

obelix          dd      0
etaussi         dd      0

[SECTION CODE_SEG]

cglobal R_DrawSpan_8_old
R_DrawSpan_8_old:
        push    ebp                     ;; preserve caller's stack frame pointer
        push    esi                     ;; preserve register variables
        push    edi
        push    ebx
;;
;; find loop count
;;
        mov     eax,[ds_x2]
        inc     eax
        sub     eax,[ds_x1]             ;; pixel count
        mov     [pixelcount],eax        ;; save for final pixel
        js near .hdone                  ;; nothing to scale
        shr     eax,0x1                 ;; double pixel count
        mov     [loopcount],eax
;;
;; build composite position
;;
        mov     ebp,[ds_xfrac]
        shl     ebp,10
        and     ebp,0xffff0000
        mov     eax,[ds_yfrac]
        shr     eax,6
        and     eax,0xffff
        mov     edi,[ds_y]
        or      ebp,eax

        mov     esi,[ds_source]
;;
;; calculate screen dest
;;
        mov     edi,[ylookup+edi*4]
        mov     eax,[ds_x1]
        add     edi,[columnofs+eax*4]
;;
;; build composite step
;;
        mov     ebx,[ds_xstep]
        shl     ebx,10
        and     ebx,0xffff0000
        mov     eax,[ds_ystep]
        shr     eax,6
        and     eax,0xffff
        or      ebx,eax

        mov     [obelix],ebx
        mov     [etaussi],esi

;; %eax      aligned colormap
;; %ebx      aligned colormap
;; %ecx,%edx  scratch
;; %esi      virtual source
;; %edi      moving destination pointer
;; %ebp      frac

        mov     eax,[ds_colormap]
        mov     ecx,ebp
        add     ebp,ebx                 ;; advance frac pointer
        shr     cx,10
        rol     ecx,6
        and     ecx,4095                ;; finish calculation for third pixel
        mov     edx,ebp
        shr     dx,10
        rol     edx,6
        add     ebp,ebx                 ;; advance frac pointer
        and     edx,4095                ;; finish calculation for fourth pixel
        mov     ebx,eax
        mov     al,[esi+ecx]            ;; get first pixel
        mov     bl,[esi+edx]            ;; get second pixel

        test dword [pixelcount],0xfffffffe

        mov     dl,[eax]                ;; color translate first pixel

;;      movw    $0xf0f0,%dx             ;;see visplanes start

        je      .hchecklast

        mov     dh,[ebx]                ;; color translate second pixel
        mov     esi,[loopcount]
align 4
.hdoubleloop:
        mov     ecx,ebp
        shr     cx,10
        rol     ecx,6
         add     ebp,[obelix]            ;; advance frac pointer
        mov     [edi],dx                ;; write first pixel
         and     ecx,4095                ;; finish calculation for third pixel
        mov     edx,ebp
        shr     dx,10
        rol     edx,6
         add     ecx,[etaussi]
        and     edx,4095                ;; finish calculation for fourth pixel
         mov     al,[ecx]                ;; get third pixel
        add     ebp,[obelix]            ;; advance frac pointer
         add     edx,[etaussi]
        mov     bl,[edx]                ;; get fourth pixel
         mov     dl,[eax]                ;; color translate third pixel
        add     edi,byte +0x2           ;; advance to third pixel destination
         dec     esi                     ;; done with loop?
        mov     dh,[ebx]                ;; color translate fourth pixel
         jne     .hdoubleloop
;; check for final pixel
.hchecklast:
        test dword [pixelcount],0x1
        je      .hdone
        mov     [edi],dl                ;; write final pixel
.hdone: pop     ebx                     ;; restore register variables
        pop     edi
        pop     esi
        pop     ebp                     ;; restore caller's stack frame pointer
        ret


;; ========================================================================
;;  Rasterization des segments d'un polyg“ne textur‚ de maniŠre LINEAIRE.
;;  Il s'agit donc d'interpoler les coordonn‚es aux bords de la texture en
;;  mˆme temps que les abscisses minx/maxx pour chaque ligne.
;;  L'argument 'dir' indique quels bords de la texture sont interpolés:
;;    0 : segments associ‚s aux bord SUPERIEUR et INFERIEUR ( TY constant )
;;    1 : segments associ‚s aux bord GAUCHE    et DROITE    ( TX constant )
;; ========================================================================
;;
;;  void   rasterize_segment_tex( LONG x1, LONG y1, LONG x2, LONG y2, LONG tv1, LONG tv2, LONG tc, LONG dir );
;;                                   ARG1     ARG2     ARG3     ARG4      ARG5      ARG6     ARG7       ARG8
;;
;;  Pour dir = 0, (tv1,tv2) = (tX1,tX2), tc = tY, en effet TY est constant.
;;
;;  Pour dir = 1, (tv1,tv2) = (tY1,tY2), tc = tX, en effet TX est constant.
;;
;;
;;  Uses:  extern struct rastery *_rastertab;
;;

[SECTION CODE_SEG write]

MINX            EQU    0
MAXX            EQU    4
TX1             EQU    8
TY1             EQU    12
TX2             EQU    16
TY2             EQU    20
RASTERY_SIZEOF  EQU    24

cglobal rasterize_segment_tex
rasterize_segment_tex:
        push    ebp
        mov     ebp,esp

        sub     esp,byte +0x8           ;; alloue les variables locales

        push    ebx
        push    esi
        push    edi
        o16 mov ax,es
        push    eax

;;        #define DX       [ebp-4]
;;        #define TD       [ebp-8]

        mov     eax,[ebp+0xc]           ;; y1
        mov     ebx,[ebp+0x14]          ;; y2
        cmp     ebx,eax
        je near .L_finished             ;; special (y1==y2) segment horizontal, exit!

        jg near .L_rasterize_right

;;rasterize_left:       ;; on rasterize un segment … la GAUCHE du polyg“ne

        mov     ecx,eax
        sub     ecx,ebx
        inc     ecx                     ;; y1-y2+1

        mov     eax,RASTERY_SIZEOF
        mul     ebx                     ;; * y2
        mov     esi,[prastertab]
        add     esi,eax                 ;; point into rastertab[y2]

        mov     eax,[ebp+0x8]           ;; ARG1
        sub     eax,[ebp+0x10]          ;; ARG3
        shl     eax,0x10                ;;     ((x1-x2)<<PRE) ...
        cdq
        idiv    ecx                     ;; dx =     ...        / (y1-y2+1)
        mov     [ebp-0x4],eax           ;; DX

        mov     eax,[ebp+0x18]          ;; ARG5
        sub     eax,[ebp+0x1c]          ;; ARG6
        shl     eax,0x10
        cdq
        idiv    ecx                     ;;      tdx =((tx1-tx2)<<PRE) / (y1-y2+1)
        mov     [ebp-0x8],eax           ;; idem tdy =((ty1-ty2)<<PRE) / (y1-y2+1)

        mov     eax,[ebp+0x10]          ;; ARG3
        shl     eax,0x10                ;; x = x2<<PRE

        mov     ebx,[ebp+0x1c]          ;; ARG6
        shl     ebx,0x10                ;; tx = tx2<<PRE    d0
                                        ;; ty = ty2<<PRE    d1
        mov     edx,[ebp+0x20]          ;; ARG7
        shl     edx,0x10                ;; ty = ty<<PRE     d0
                                        ;; tx = tx<<PRE     d1
        push    ebp
        mov     edi,[ebp-0x4]           ;; DX
        cmp     dword [ebp+0x24],byte +0x0      ;; ARG8   direction ?

        mov     ebp,[ebp-0x8]           ;; TD
        je      .L_rleft_h_loop
;;
;; TY varie, TX est constant
;;
.L_rleft_v_loop:
        mov     [esi+MINX],eax           ;; rastertab[y].minx = x
          add     ebx,ebp
        mov     [esi+TX1],edx           ;;             .tx1  = tx
          add     eax,edi
        mov     [esi+TY1],ebx           ;;             .ty1  = ty

        ;;addl    DX, %eax        // x     += dx
        ;;addl    TD, %ebx        // ty    += tdy

        add     esi,RASTERY_SIZEOF      ;; next raster line into rastertab[]
        dec     ecx
        jne     .L_rleft_v_loop
        pop     ebp
        jmp     .L_finished
;;
;; TX varie, TY est constant
;;
.L_rleft_h_loop:
        mov     [esi+MINX],eax           ;; rastertab[y].minx = x
          add     eax,edi
        mov     [esi+TX1],ebx           ;;             .tx1  = tx
          add     ebx,ebp
        mov     [esi+TY1],edx           ;;             .ty1  = ty

        ;;addl    DX, %eax        // x     += dx
        ;;addl    TD, %ebx        // tx    += tdx

        add     esi,RASTERY_SIZEOF      ;; next raster line into rastertab[]
        dec     ecx
        jne     .L_rleft_h_loop
        pop     ebp
        jmp     .L_finished
;;
;; on rasterize un segment … la DROITE du polyg“ne
;;
.L_rasterize_right:
        mov     ecx,ebx
        sub     ecx,eax
        inc     ecx                     ;; y2-y1+1

        mov     ebx,RASTERY_SIZEOF
        mul     ebx                     ;;   * y1
        mov     esi,[prastertab]
        add     esi,eax                 ;;  point into rastertab[y1]

        mov     eax,[ebp+0x10]          ;; ARG3
        sub     eax,[ebp+0x8]           ;; ARG1
        shl     eax,0x10                ;; ((x2-x1)<<PRE) ...
        cdq
        idiv    ecx                     ;;  dx =     ...        / (y2-y1+1)
        mov     [ebp-0x4],eax           ;; DX

        mov     eax,[ebp+0x1c]          ;; ARG6
        sub     eax,[ebp+0x18]          ;; ARG5
        shl     eax,0x10
        cdq
        idiv    ecx                     ;;       tdx =((tx2-tx1)<<PRE) / (y2-y1+1)
        mov     [ebp-0x8],eax           ;;  idem tdy =((ty2-ty1)<<PRE) / (y2-y1+1)

        mov     eax,[ebp+0x8]           ;; ARG1
        shl     eax,0x10                ;; x  = x1<<PRE

        mov     ebx,[ebp+0x18]          ;; ARG5
        shl     ebx,0x10                ;; tx = tx1<<PRE    d0
                                        ;; ty = ty1<<PRE    d1
        mov     edx,[ebp+0x20]          ;; ARG7
        shl     edx,0x10                ;; ty = ty<<PRE     d0
                                        ;; tx = tx<<PRE     d1
        push    ebp
        mov     edi,[ebp-0x4]           ;; DX

        cmp     dword [ebp+0x24], 0     ;; direction ?

         mov     ebp,[ebp-0x8]          ;; TD
        je      .L_rright_h_loop
;;
;; TY varie, TX est constant
;;
.L_rright_v_loop:

        mov     [esi+MAXX],eax           ;; rastertab[y].maxx = x
          add     ebx,ebp
        mov     [esi+TX2],edx          ;;             .tx2  = tx
          add     eax,edi
        mov     [esi+TY2],ebx          ;;             .ty2  = ty

        ;;addl    DX, %eax        // x     += dx
        ;;addl    TD, %ebx        // ty    += tdy

        add     esi,RASTERY_SIZEOF
        dec     ecx
        jne     .L_rright_v_loop

        pop     ebp

        jmp     short .L_finished
;;
;; TX varie, TY est constant
;;
.L_rright_h_loop:
        mov     [esi+MAXX],eax           ;; rastertab[y].maxx = x
          add     eax,edi
        mov     [esi+TX2],ebx          ;;             .tx2  = tx
          add     ebx,ebp
        mov     [esi+TY2],edx          ;;             .ty2  = ty

        ;;addl    DX, %eax        // x     += dx
        ;;addl    TD, %ebx        // tx    += tdx

        add     esi,RASTERY_SIZEOF
        dec     ecx
        jne     .L_rright_h_loop

        pop     ebp

.L_finished:
        pop     eax
        o16 mov es,ax
        pop     edi
        pop     esi
        pop     ebx

        mov     esp,ebp
        pop     ebp
        ret


;;; this version can draw 64x64 tiles, but they would have to be arranged 4 per row,
;; so that the stride from one line to the next is 256
;;
;; .data
;;xstep         dd      0
;;ystep         dd      0
;;texwidth      dd      64              ;; texture width
;; .text
;; this code is kept in case we add high-detail floor textures for example (256x256)
;       align   16
;_R_DrawSpan_8:
;       push ebp                        ;; preserve caller's stack frame pointer
;       push esi                        ;; preserve register variables
;       push edi
;       push ebx
;;
;; find loop count
;;
;       mov eax,[ds_x2]
;       inc eax
;       sub eax,[ds_x1]                 ;; pixel count
;       mov [pixelcount],eax            ;; save for final pixel
;       js near .hdone                  ;; nothing to scale
;;
;; calculate screen dest
;;
;       mov edi,[ds_y]
;       mov edi,[ylookup+edi*4]
;       mov eax,[ds_x1]
;       add edi,[columnofs+eax*4]
;;
;; prepare registers for inner loop
;;
;       xor eax,eax
;       mov edx,[ds_xfrac]
;       ror edx,16
;       mov al,dl
;       mov ecx,[ds_yfrac]
;       ror ecx,16
;       mov ah,cl
;
;       mov ebx,[ds_xstep]
;       ror ebx,16
;       mov ch,bl
;       and ebx,0xffff0000
;       mov [xstep],ebx
;       mov ebx,[ds_ystep]
;       ror ebx,16
;       mov dh,bl
;       and ebx,0xffff0000
;       mov [ystep],ebx
;
;       mov esi,[ds_source]
;
;;; %eax      Yi,Xi in %ah,%al
;;; %ebx      aligned colormap
;;; %ecx      Yfrac upper, dXi in %ch, %cl is counter (upto 1024pels, =4x256)
;;; %edx      Xfrac upper, dYi in %dh, %dl receives mapped pixels from (ebx)
;;;  ystep    dYfrac, add to %ecx, low word is 0
;;;  xstep    dXfrac, add to %edx, low word is 0
;;; %ebp      temporary register serves as offset like %eax
;;; %esi      virtual source
;;; %edi      moving destination pointer
;
;       mov ebx,[pixelcount]
;       shr ebx,0x2                     ;; 4 pixels per loop
;       test bl,0xff
;       je near .hchecklast
;       mov cl,bl
;
;       mov ebx,[dc_colormap]
;;;
;;; prepare loop with first pixel
;;;
;       add ecx,[ystep]                 ;;pr‚a1
;       adc ah,dh
;       add edx,[xstep]
;       adc al,ch
;       and eax,0x3f3f
;       mov bl,[esi+eax]                ;;pr‚b1
;       mov dl,[ebx]                    ;;pr‚c1
;
;       add ecx,[ystep]                 ;;a2
;        adc ah,dh
;
;.hdoubleloop:
;       mov [edi+1],dl
;        add edx,[xstep]
;       adc al,ch
;        add edi,byte +0x2
;       mov ebp,eax
;        add ecx,[ystep]
;       adc ah,dh
;        and ebp,0x3f3f
;       add edx,[xstep]
;        mov bl,[esi+ebp]
;       adc al,ch
;        mov dl,[ebx]
;       and eax,0x3f3f
;        mov [edi],dl
;       mov bl,[esi+eax]
;        add ecx,[ystep]
;       adc ah,dh
;        add edx,[xstep]
;       adc al,ch
;        mov dl,[ebx]
;       mov ebp,eax
;        mov [edi+1],dl
;       and ebp,0x3f3f
;        add ecx,[ystep]
;       adc ah,dh
;        mov bl,[esi+ebp]
;       add edi,byte +0x2
;        add edx,[xstep]
;       adc al,ch
;        mov dl,[ebx]
;       and eax,0x3f3f
;        mov [edi],dl
;       mov bl,[esi+eax]
;        add ecx,[ystep]
;       adc ah,dh
;        mov dl,[ebx]
;       dec cl
;        jne near .hdoubleloop
;;; check for final pixel
;.hchecklast:
;;; to do
;.hdone:
;       pop ebx
;       pop edi
;       pop esi
;       pop ebp
;       ret
