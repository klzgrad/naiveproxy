%define regxmm xmm0
%define regymm ymm0
%define mem [0]
%define imm 3

%macro x 1+.nolist
 %1 ; comment this line if RELAXed variants are not supported
%endmacro

  VFMADDSUB132PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB132PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB132PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB132PS regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB312PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB312PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB312PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB312PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADDSUB132PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB132PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB132PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB132PD regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB312PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB312PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB312PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB312PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD132PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD132PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD132PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD132PS regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD312PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD312PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD312PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD312PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD132PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD132PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD132PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD132PD regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD312PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD312PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD312PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD312PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD132PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD132PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD132PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD132PS    regymm,regymm,regymm     ; VEX_FMA

x VFMADD312PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD312PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD312PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD312PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD132PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD132PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD132PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD132PD    regymm,regymm,regymm     ; VEX_FMA

x VFMADD312PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD312PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD312PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD312PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD132SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMADD132SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD312SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMADD312SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMADD132SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMADD132SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD312SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMADD312SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB132PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB132PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB132PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB132PS    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB312PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB312PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB312PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB312PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB132PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB132PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB132PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB132PD    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB312PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB312PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB312PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB312PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB132SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMSUB132SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB312SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMSUB312SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB132SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMSUB132SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB312SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMSUB312SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD132PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD132PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD132PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD132PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD312PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD312PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD312PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD312PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD132PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD132PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD132PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD132PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD312PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD312PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD312PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD312PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD132SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMADD132SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD312SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMADD312SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD132SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMADD132SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD312SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMADD312SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB132PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB132PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB132PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB132PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB312PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB312PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB312PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB312PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB132PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB132PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB132PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB132PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB312PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB312PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB312PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB312PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB132SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMSUB132SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB312SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMSUB312SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB132SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMSUB132SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB312SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMSUB312SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMADDSUB213PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB213PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB213PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB213PS regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB123PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB123PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB123PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB123PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADDSUB213PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB213PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB213PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB213PD regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB123PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB123PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB123PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB123PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD213PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD213PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD213PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD213PS regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD123PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD123PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD123PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD123PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD213PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD213PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD213PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD213PD regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD123PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD123PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD123PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD123PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD213PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD213PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD213PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD213PS    regymm,regymm,regymm     ; VEX_FMA

x VFMADD123PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD123PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD123PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD123PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD213PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD213PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD213PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD213PD    regymm,regymm,regymm     ; VEX_FMA

x VFMADD123PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD123PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD123PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD123PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD213SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMADD213SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD123SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMADD123SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMADD213SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMADD213SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD123SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMADD123SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB213PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB213PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB213PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB213PS    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB123PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB123PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB123PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB123PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB213PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB213PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB213PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB213PD    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB123PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB123PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB123PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB123PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB213SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMSUB213SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB123SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMSUB123SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB213SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMSUB213SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB123SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMSUB123SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD213PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD213PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD213PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD213PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD123PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD123PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD123PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD123PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD213PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD213PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD213PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD213PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD123PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD123PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD123PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD123PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD213SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMADD213SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD123SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMADD123SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD213SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMADD213SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD123SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMADD123SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB213PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB213PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB213PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB213PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB123PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB123PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB123PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB123PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB213PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB213PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB213PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB213PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB123PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB123PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB123PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB123PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB213SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMSUB213SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB123SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMSUB123SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB213SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMSUB213SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB123SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMSUB123SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMADDSUB231PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB231PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB231PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB231PS regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB321PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB321PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB321PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB321PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADDSUB231PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADDSUB231PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADDSUB231PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMADDSUB231PD regymm,regymm,regymm     ; VEX_FMA

x VFMADDSUB321PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADDSUB321PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADDSUB321PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADDSUB321PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD231PS regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD231PS regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD231PS regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD231PS regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD321PS regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD321PS regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD321PS regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD321PS regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUBADD231PD regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUBADD231PD regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUBADD231PD regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUBADD231PD regymm,regymm,regymm     ; VEX_FMA

x VFMSUBADD321PD regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUBADD321PD regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUBADD321PD regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUBADD321PD regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD231PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD231PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD231PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD231PS    regymm,regymm,regymm     ; VEX_FMA

x VFMADD321PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD321PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD321PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD321PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD231PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMADD231PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMADD231PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMADD231PD    regymm,regymm,regymm     ; VEX_FMA

x VFMADD321PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMADD321PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMADD321PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMADD321PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMADD231SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMADD231SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD321SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMADD321SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMADD231SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMADD231SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMADD321SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMADD321SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB231PS    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB231PS    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB231PS    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB231PS    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB321PS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB321PS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB321PS    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB321PS    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB231PD    regxmm,regxmm,mem        ; VEX_FMA,SO
  VFMSUB231PD    regxmm,regxmm,regxmm     ; VEX_FMA
  VFMSUB231PD    regymm,regymm,mem        ; VEX_FMA,SY
  VFMSUB231PD    regymm,regymm,regymm     ; VEX_FMA

x VFMSUB321PD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFMSUB321PD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFMSUB321PD    regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFMSUB321PD    regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFMSUB231SS    regxmm,regxmm,mem        ; VEX_FMA,SD
  VFMSUB231SS    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB321SS    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFMSUB321SS    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFMSUB231SD    regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFMSUB231SD    regxmm,regxmm,regxmm     ; VEX_FMA

x VFMSUB321SD    regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFMSUB321SD    regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD231PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD231PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD231PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD231PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD321PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD321PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD321PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD321PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD231PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMADD231PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMADD231PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMADD231PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMADD321PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMADD321PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMADD321PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMADD321PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMADD231SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMADD231SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD321SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMADD321SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMADD231SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMADD231SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMADD321SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMADD321SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB231PS   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB231PS   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB231PS   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB231PS   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB321PS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB321PS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB321PS   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB321PS   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB231PD   regxmm,regxmm,mem        ; VEX_FMA,SO
  VFNMSUB231PD   regxmm,regxmm,regxmm     ; VEX_FMA
  VFNMSUB231PD   regymm,regymm,mem        ; VEX_FMA,SY
  VFNMSUB231PD   regymm,regymm,regymm     ; VEX_FMA

x VFNMSUB321PD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SO
x VFNMSUB321PD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX
x VFNMSUB321PD   regymm,regymm,mem        ; VEX_FMA,RELAX,SY
x VFNMSUB321PD   regymm,regymm,regymm     ; VEX_FMA,RELAX

  VFNMSUB231SS   regxmm,regxmm,mem        ; VEX_FMA,SD
  VFNMSUB231SS   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB321SS   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SD
x VFNMSUB321SS   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VFNMSUB231SD   regxmm,regxmm,mem        ; VEX_FMA,SQ
  VFNMSUB231SD   regxmm,regxmm,regxmm     ; VEX_FMA

x VFNMSUB321SD   regxmm,regxmm,mem        ; VEX_FMA,RELAX,SQ
x VFNMSUB321SD   regxmm,regxmm,regxmm     ; VEX_FMA,RELAX

  VPCLMULLQLQDQ  regxmm,regxmm,mem        ; PCLMUL,VEX_AVX,SO
  VPCLMULLQLQDQ  regxmm,regxmm,regxmm     ; PCLMUL,VEX_AVX
  VPCLMULHQLQDQ  regxmm,regxmm,mem        ; PCLMUL,VEX_AVX,SO
  VPCLMULHQLQDQ  regxmm,regxmm,regxmm     ; PCLMUL,VEX_AVX
  VPCLMULLQHQDQ  regxmm,regxmm,mem        ; PCLMUL,VEX_AVX,SO
  VPCLMULLQHQDQ  regxmm,regxmm,regxmm     ; PCLMUL,VEX_AVX
  VPCLMULHQHQDQ  regxmm,regxmm,mem        ; PCLMUL,VEX_AVX,SO
  VPCLMULHQHQDQ  regxmm,regxmm,regxmm     ; PCLMUL,VEX_AVX
  VPCLMULQDQ     regxmm,regxmm,mem,imm    ; PCLMUL,VEX_AVX,SB3,SO
  VPCLMULQDQ     regxmm,regxmm,regxmm,imm ; PCLMUL,VEX_AVX,SB3

; EOF

 	  	 
