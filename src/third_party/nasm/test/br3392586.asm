	bits 32
%define CACHE_SIZE 32
%define MAXHEIGHT 1024
%define DPS_MAXSPANS MAXHEIGHT+1
%define spanpackage_t_size 32
%define SPAN_SIZE (((DPS_MAXSPANS + 1 + ((CACHE_SIZE - 1) / spanpackage_t_size)) + 1) * spanpackage_t_size)
D_PolysetDraw:
	sub esp,SPAN_SIZE
