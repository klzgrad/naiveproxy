#!/usr/bin/perl
%packed_insns = (
    'vfmadd'    => 0x98,
    'vfmaddsub' => 0x96,
    'vfmsubadd' => 0x97,
    'vfmsub'    => 0x9a,
    'vfnmadd'   => 0x9c,
    'vfnmsub'   => 0x9e
    );

%scalar_insns = (
    'vfmadd'    => 0x99,
    'vfmsub'    => 0x9b,
    'vfnmadd'   => 0x9d,
    'vfnmsub'   => 0x9f
    );

foreach $pi ( sort(keys(%packed_insns)) ) {
    $op = $packed_insns{$pi};
    foreach $order ('132', '213', '231') {
	$xorder = substr($order,1,1).substr($order,0,1).substr($order,2,1);
	foreach $o ($order, $xorder) {
	    for ($w = 0; $w < 2; $w++) {
		$suf = $w  ? 'pd' : 'ps';
		for ($l = 128; $l <= 256; $l <<= 1) {
		    $sx  = ($l == 256) ? 'SY' : 'SO';
		    $mm  = ($l == 256) ? 'ymm' : 'xmm';
		    printf "%-15s %-31s %-8s%-39s %s\n",
		    	"\U${pi}${o}${suf}",
			"${mm}reg,${mm}reg,${mm}rm",
		    	"[rvm:",
		        sprintf("vex.dds.%d.66.0f38.w%d %02x /r]",
			    $l, $w, $op),
		    "FMA,FUTURE,${sx}";
		}
	    }
	}
	$op += 0x10;
    }
}

foreach $si ( sort(keys(%scalar_insns)) ) {
    $op = $scalar_insns{$si};
    foreach $order ('132', '213', '231') {
	$xorder = substr($order,1,1).substr($order,0,1).substr($order,2,1);
	foreach $o ($order, $xorder) {
	    for ($w = 0; $w < 2; $w++) {
		$suf = $w ? 'sd' : 'ss';
		$sx  = $w ? 'SQ' : 'SD';
		$l  = 128;
		$mm  = 'xmm';
		printf "%-15s %-31s %-8s%-39s %s\n",
		    "\U${si}${o}${suf}",
		    "${mm}reg,${mm}reg,${mm}rm",
		    '[rvm:',
		    sprintf("vex.dds.%d.66.0f38.w%d %02x /r]",
			$l, $w, $op),
		"FMA,FUTURE,${sx}";
	    }
	}
	$op += 0x10;
    }
}
