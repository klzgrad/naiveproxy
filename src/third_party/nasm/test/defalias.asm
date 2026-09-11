%defalias foo bar
%ifdef foo
  %error "foo should not be defined here!"
%endif

%define foo 33
%ifndef foo
  %error "foo should be defined here!"
%endif
%ifndef bar
  %error "bar should be defined here!"
%endif
%if bar != 33
  %error "bar should have the value 33 here"
%endif

%define bar 34
%if foo != 34
  %error "foo should have the value 34 here"
%endif

%undef foo
%ifdef foo
  %error "foo should not be defined here!"
%endif
%ifdef bar
  %error "bar should not be defined here!"
%endif

%ifndefalias foo
  %error "foo was removed as an alias!"
%endif

%define bar 35
%if foo != 35
  %error "foo should have the value 35 here"
%endif

%define foo 36
%if bar != 36
  %error "bar should have the value 36 here"
%endif

%undefalias foo
%ifdef foo
  %error "foo is still defined after %undefalias"
%elifdefalias foo
  %error "foo is undefined, but still an alias"
%endif
%ifndef bar
  %error "bar disappeared after %undefalias foo"
%endif
