; relaxed encodings for FPU instructions, which NASM should support
; -----------------------------------------------------------------

%define void
%define reg_fpu0 st0
%define reg_fpu st1

; no operands instead of one operand:

  ; F(U)COM(P), FCOM2, FCOMP3, FCOMP5

    FCOM            void
    FCOMP           void
    FUCOM           void
    FUCOMP          void
;    FCOM2           void
;    FCOMP3          void
;    FCOMP5          void

  ; FLD, FST, FSTP, FSTP1, FSTP8, FSTP9

    FLD             void
    FST             void
    FSTP            void
;    FSTP1           void
;    FSTP8           void
;    FSTP9           void

  ; FXCH, FXCH4, FXCH7, FFREE, FFREEP

    FXCH            void
;    FXCH4           void
;    FXCH7           void
    FFREE           void
    FFREEP          void

; no operands instead of two operands:

  ; FADD(P), FMUL(P), FSUBR(P), FSUB(P), FDIVR(P), FDIV(P)

    FADD            void
    FADDP           void
    FMUL            void
    FMULP           void
    FSUBR           void
    FSUBRP          void
    FSUB            void
    FSUBP           void
    FDIVR           void
    FDIVRP          void
    FDIV            void
    FDIVP           void

; one operand instead of two operands:

  ; FADD, FMUL, FSUB, FSUBR, FDIV, FDIVR

    FADD            reg_fpu
    FMUL            reg_fpu
    FSUB            reg_fpu
    FSUBR           reg_fpu
    FDIV            reg_fpu
    FDIVR           reg_fpu

  ; FADD, FMUL, FSUBR, FSUB, FDIVR, FDIV (with TO qualifier)

    FADD            to reg_fpu
    FMUL            to reg_fpu
    FSUBR           to reg_fpu
    FSUB            to reg_fpu
    FDIVR           to reg_fpu
    FDIV            to reg_fpu

  ; FADDP, FMULP, FSUBRP, FSUBP, FDIVRP, FDIVP

    FADDP           reg_fpu
    FMULP           reg_fpu
    FSUBRP          reg_fpu
    FSUBP           reg_fpu
    FDIVRP          reg_fpu
    FDIVP           reg_fpu

  ; FCMOV(N)B, FCMOV(N)E, FCMOV(N)BE, FCMOV(N)U, and F(U)COMI(P)

    FCMOVB          reg_fpu
    FCMOVNB         reg_fpu
    FCMOVE          reg_fpu
    FCMOVNE         reg_fpu
    FCMOVBE         reg_fpu
    FCMOVNBE        reg_fpu
    FCMOVU          reg_fpu
    FCMOVNU         reg_fpu
    FCOMI           reg_fpu
    FCOMIP          reg_fpu
    FUCOMI          reg_fpu
    FUCOMIP         reg_fpu

; two operands instead of one operand:

  ; these don't really exist, and thus are _NOT_ supported:

;   FCOM            reg_fpu,reg_fpu0
;   FCOM            reg_fpu0,reg_fpu
;   FUCOM           reg_fpu,reg_fpu0
;   FUCOM           reg_fpu0,reg_fpu
;   FCOMP           reg_fpu,reg_fpu0
;   FCOMP           reg_fpu0,reg_fpu
;   FUCOMP          reg_fpu,reg_fpu0
;   FUCOMP          reg_fpu0,reg_fpu

;   FCOM2           reg_fpu,reg_fpu0
;   FCOM2           reg_fpu0,reg_fpu
;   FCOMP3          reg_fpu,reg_fpu0
;   FCOMP3          reg_fpu0,reg_fpu
;   FCOMP5          reg_fpu,reg_fpu0
;   FCOMP5          reg_fpu0,reg_fpu

;   FXCH            reg_fpu,reg_fpu0
;   FXCH            reg_fpu0,reg_fpu
;   FXCH4           reg_fpu,reg_fpu0
;   FXCH4           reg_fpu0,reg_fpu
;   FXCH7           reg_fpu,reg_fpu0
;   FXCH7           reg_fpu0,reg_fpu

; EOF
