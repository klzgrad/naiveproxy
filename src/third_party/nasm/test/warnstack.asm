%warning "Good warning"
	[warning push]
	[warning -user]
%warning "Bad warning"
	[warning pop]
%warning "Good warning"
	[warning -user]
%warning "Bad warning"
	[warning pop]		; should warn but reset all
%warning "Good warning"
