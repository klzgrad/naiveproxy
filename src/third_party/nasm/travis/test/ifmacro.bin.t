This is a dummy macro, arg1 = this, arg2 = that
%ifdef LF
%ifmacro dummy 2
%ifmacro dummy 1+
%ifmacro dummy 2+
%ifmacro dummy
%ifmacro dummy 1-2
%ifmacro dummy 2-3
%ifndef CR
%ifnmacro dummy 1
%ifnmacro dummy 3
%ifnmacro dummy 3+
%ifnmacro dummy 0-1
%ifnmacro dummy 3-4
%ifnmacro LF
%elifdef LF
%elifmacro dummy 2
%elifmacro dummy 1+
%elifmacro dummy 2+
%elifmacro dummy
%elifmacro dummy 1-2
%elifmacro dummy 2-3
%elifndef CR
%elifnmacro dummy 1
%elifnmacro dummy 3
%elifnmacro dummy 3+
%elifnmacro dummy 0-1
%elifnmacro dummy 3-4
%elifnmacro LF
