//////////////////////////////////////////////////////////////////////////////
//
// John's No-ADS proxy auto configuration script
//	http://www.schooner.com/~loverso/no-ads/
//	loverso@schooner.com
//	Questions/help web forum at http://www.network54.com/Hide/Forum/223428
//
// Copyright 1996-2004, John LoVerso.  All Rights Reserved.
//
//	Permission is given to use and distribute this file, as long as this
//	copyright message and author notice are not removed.
//
//	No responsibility is taken for any errors on inaccuracies inherent
//	either to the comments or the code of this program, but if reported
//	to me, then an attempt will be made to fix them.
//
// ("no monies exchanged" in Copyright clause removed 11/2001)
//
var noadsver = "$Id: no-ads.pac,v 5.70 2007/05/11 16:56:01 loverso Exp loverso $";

// ****
// **** If you do not use a proxy to access the Internet, then the following
// **** line is already fine.
// ****
// **** If you use an a proxy to access the Internet, as required by your
// **** ISP or firewall, then change the line below, replacing
// **** "DIRECT" with "PROXY hostname:port", using the correct hostname:port
// **** for your proxy server.
// ****
var normal = "DIRECT";

// ***
// *** If you are not using a blackhold proxy, then you can leave this
// *** setting as is.
// ***
// *** Otherwise, update the next line with the correct hostname:port
// *** of your blackhole proxy server.  If you are using Larry Wang's
// *** BHP for Windows, you need to change the "0.0.0.0" to "127.0.0.1"
// ***
var blackhole = "PROXY 0.0.0.0:3421";

// ***
// *** If you need a different proxy to access local/internal hosts vs.
// *** the rest of the Internet, set 'localproxy' to that value.  Otherwise,
// *** 'localproxy' defaults to the same value as 'normal', so you do
// *** not need to change anything in the normal case.
// ***
// *** Some typical cases:
// ***	- 'normal' might be one proxy, and 'localproxy' might be another
// ***	- 'normal' might be a proxy, and 'localproxy' might be "DIRECT"
// ***
// *** You will also need to change the LOCAL section below by adding
// *** rules to match your local/internal hosts.
// ***
var localproxy = normal;

// ***
// *** 'bypass' is the preferred proxy setting for when no-ads is inactive.
// *** Either use '= normal' or '= localproxy' (or perhaps just "DIRECT").
// *** This only matters when you need to use a localproxy.
// *** (You probably don't need to care about this)
// ***
var bypass = normal;

///////////////////////////////////////////////////////////////////////////////
//
// This simple kludge uses a mechanism built into most browsers (IE, Netscape,
// Mozilla, Firefox, and Opera) on most platforms to block connections to
// banner ad servers.
//
// This mechanism uses the "proxy auto configuration" to blackhole requests
// to load ad images without forcing all your traffic through an ad-blocking
// proxy server.  Of course, unlike ad-blocking proxy servers, this does not
// otherwise not strip cookies.
//
// "Proxy auto configuration" invokes the JavaScript FindProxyForURL function
// below each time your browser requests a URL.  This works even if you have
// JavaScript otherwise disabled in your browser!  (Which you should!)
//

//
// Send me your additions or comments.  I'll credit you in the file.
// (But I've removed all email addresses to stop spam harvesters).
//


///////////////////////////////////////////////////////////////////////////////
//
// These are the basic steps needed to use "no-ads.pac".
// Detailed instructions follow below!
//
// 1. Save this as a file (no-ads.pac) on your local disk
//    (or, add it to your home page, if you have one)
// 2. Select a no-ads "blackhole".
// 3. Configure your browser to use this file as its auto proxy configuration.
// 4. Clear your browser's cache
//    (or else it may still show you ads it has saved on your disk).
//


///////////////////////////////////////////////////////////////////////////////
//
// 1. SAVE THIS FILE
//
// Copy this file to your local machine; use your home directory (UNIX)
// or your Desktop or C:\ directory (Windows).
//



///////////////////////////////////////////////////////////////////////////////
//
// 2. SELECT A NO-ADS BLACKHOLE
//
// You can skip this section if you are using any version of Internet Explorer.
// You can also skip this section for Netscape 7.1, Mozilla 1.4, or
// Firefox 1.0 (or later), as they include PAC failover support (but do
// read the note in section "2a" below).
//
//
// The basic trick of no-ads is to match the site or URL of annoying web content
// and tell your browser to use a proxy that will deny loading of that resource
// (image, page, etc).
//
// A "black-hole" proxy server is one that always denies loading a web page.
// ("send it off to a blackhole").
//
// When you initially get "no-ads.pac", it is using this as the blackhole:
//
//	"PROXY 0.0.0.0:3421"
//
// This says to use the local host at a port which nothing should be listening
// on.  Thus, this is "a server that doesn't repond."
//
// This is a good default for all systems, and especially Windows.
// However, if you are using the Blackhole Proxy Server on Windows, 
// be sure to change it to "PROXY 127.0.0.1:3421"
//
//
// Some possibilities for the blackhole:
//
//	a. A server that doesn't respond.
//
//		*** This works for all versions of Internet Explorer.
//		*** This mostly works for Mozilla, Firefox, and Netscape.
//
//		If you do nothing, then this is configured to direct annoying
//		content to the proxy running on your own host at port 3421.
//		Since you shouldn't have anything running on that port, that
//		connection will timeout and the annoying content will never be
//		loaded.
//
//		Older versions of Netscape wait to connect to the proxy server
//		(usually it needs to load part of the image to layout the web
//		page), and then asks if you want to disable the proxy that
//		doesn't answer.
//
//		Older versions of Mozilla will give an alert saying it couldn't
//		connect to the proxy server.
//
//		Mozilla 1.4+, Firefox 1.0+ and Netscape 7.1 will only give
//		you this alert if the whole page being display is blocked,
//		rather than just an image on that page.  Thus, I still
//		recommend a blackhole proxy even though it isn't needed.
//
//		Opera will disable your auto proxy config if the proxy server
//		doesn't respond.
//
//		IE doesn't care that the proxy server isn't responding.  As
//		this avoids a connection for annoying content, it is fastest.
//
//	b. A simple, blackhole server
//
//		When needed, I run a simple "server" at port 3421 that denies
//		all requests.  Some options you can use for this:
//
//		- On Windows, you can try Larry Wang's black-hole proxy program:
//
//			http://leisuresuit10.tripod.com/BlackHoleProxy/
//
//		  I can not vouch that his binaries are virus free, but he does
//		  offer the source code.
//
//		- I use this shell script on UNIX; it is invoked via inetd.
//		  /usr/local/lib/noproxy:
//
//			#!/bin/sh
//			read a
//			read b
//			echo HTTP/1.0 501 No Ads Accepted
//			echo ""
//			exit
//
//		  Add this line to inetd.conf ('kill -HUP' inetd afterwards):
//
//		    3421 stream tcp nowait nobody /usr/local/lib/noproxy noproxy
//
//		  This simple script doesn't work on Linux because of the
//		  (IMHO) broken way its TCP stack works.  See the bottom of
//		  http://www.schooner.com/~loverso/no-ads/ for a complete copy
//		  of the `noproxy' shell script.
//
//		  If always exec'ing a shell was expensive on your computer
//		  (it isn't on mine), then you could use a "wait"-style Perl
//		  script that would accept() incoming connections.
//
//		- Sean Burke has a black-hole proxy written in Perl script:
//
//		  http://www.speech.cs.cmu.edu/~sburke/pub/black_hole_http_server.pl
//		  (This is a standalone server, not run from inetd).
//
//	e. A trick: use an HTTP/1.0 non-proxy server
//
//		An HTTP/1.0 non-proxy server will return a 501 error when
//		given a proxy request.  Thus, just use the address of your
//		local intranet web server as your blackhole PROXY.
//		The downside of this is that it will probably also log an
//		error, which wastes a small amount of resources.
//
//	***
//	*** Be sure to update the "blackhole" variable above with a setting of
//	*** "PROXY hostname:port" that matches your blackhole server!!
//	***
//
//	***
//	*** If you already use a proxy server to access the WWW,
//	*** change the "normal" variable above from "DIRECT" to
//	*** be "PROXY proxy:port" to match your proxy server.
//	***


///////////////////////////////////////////////////////////////////////////////
//
// 3. TO CONFIGURE YOUR BROWSER
//
// The Proxy Auto Configuration file can be either on the local disk or
// accessed from a web server, with the following constraints:
//
//	a. IE4 can only load the PAC from a web server (http:// URL)
//	b. Netscape, Mozilla, Firefox and IE (5 or later) can load the
//	   PAC from anywhere.
//	c. Netscape, Mozilla, Firefox and (probably) Opera require the correct
//	   MIME type when loading the PAC from a web server.
//
//
// To set the Proxy Auto Configuration with Netscape, Mozilla, or Firefox:
//
//   1. Enable Proxy Auto Config:
//
//	For Netsacpe/Mozilla:
//
//		Open "Edit->Preferences"
//		Select "Advanced"
//		Select "Proxies"
//
//	For Firefox (1.0):
//
//		Open "Tools->Options"
//		Select "Coonection Settings" on the General tab:
//
//	Select the "Auto proxy configuration URL" option.
//	Enter URL or path of where you've saved this file, such as:
//
//		http://yourserver/no-ads.pac
//
//	If you place this on your local disk, you should use a
//	file: URL such as:
//
//		file:/home/loverso/no-ads.pac			(UNIX)
//		file:///c:/windows/desktop/no-ads.pac		(Windows)
//
//	(file:/ and file:// will work in Mozilla, but file:/// is correct
//	required for Firefox)
//
//   2. If you are serving this from a web server, these browsers require
//      the correct MIME type on the file before using it.  You must configure
//      your web server to provide a "application/x-ns-proxy-autoconfig"
//	MIME type.
//
//      a. For Apache, name the file with a ".pac" extension and add this
//	   line to the http.conf (or the .htaccess file in the same directory):
//
//		AddType application/x-ns-proxy-autoconfig .pac
//
//      b. For IIS (instructions from Kevin Roth)
//
//	   Open Internet Services Manager
//	   Right click on the web site (or directory) you wish to change.
//	   Choose Properties
//	   Click the "HTTP Headers" tab
//	   Click the "File Types" button in the "MIME Map" section
//	   Click the "New Type..." button
//	   Enter "pac" for "Associated Extension"
//	   Enter "application/x-ns-proxy-autoconfig" for "Content Type (MIME)"
//	   Click OK to close the Add type dialog, the MIME types dialog,
//		and the main properties dialog.
//
//      (This is definately needed for NS, but not for IE)
//
//
// To set the Proxy Auto Configuration with IE:
//
//   1. Enable Proxy Auto Config:
//
//	Open "Tools->Internet Options"
//	Select "Connections" tab
//	Click "LAN Settings"
//		or Choose an entry from "Dial-up settings" and click "Settings"
//
//	On the settings dialog, select "Use automatic configuration script"
//	Enter the URL of this file in Address field.
//
//		http://yourserver/no-ads.pac
//		file:///c:/windows/desktop/no-ads.pac		(Windows)
//
//	You can only use a file: URL with IE5 (or later).
//	("file:///" with with IE versions after 5.0 SP2)
//
//   2. Fix Security Settings (IMPORTANT):
//
//	Select "Security" tab
//	Select "Local intranet"
//	Click "Sites" box
//	Unselect "include all sites that bypass the proxy server" option
//
//   3. Disable "Auto Proxy Caching" (IMPORTANT):
//      (thanks to Kevin Roth for alerting me of this!)
//
//	IE contains a proxy result caching mechanism that will defeat the
//	ability to block servers that server both ad and non-ad content.
//	To prevent this, add the registry key described in this MS KB article:
//
//		http://support.microsoft.com/?kbid=271361
//
//	You can do so by downloading this file and clicking on it to load
//	it into the registry.  This must be done on a per-user basis.
//	http://www.schooner.com/~loverso/no-ads/IE-no-auto-proxy-cache.reg
//
//   IE doesn't currently check the MIME type of the PAC file.
//
//   To see some notes from MS on PAC in IE, see
//	http://msdn.microsoft.com/library/periodic/period99/faq0599.htm
//	(they seem to have removed this URL)
//
//
// To set the Proxy Auto Configuration with Opera 6 (6.04 on Windows tested):
//
//   1. Enable Proxy Auto Config:
//	Open the Preferences (Alt-P)
//	Select "Network"
//	Click the "Proxy servers" box
//	Select "Use automatic proxy configuration"
//	Enter the URL of this file as
//
//		http://yourserver/no-ads.pac
//		file://c:/windows/desktop/no-ads.pac
//
//	(file:/// might be needed; I've not tested Opera lately)
//
//   2. You must use a blackhole proxy for Opera (it will not work with an
//	address of a server that does not respond).
//
//   3. Be sure to clear the cache and exit/restart Opera.
//


///////////////////////////////////////////////////////////////////////////////
//
// 4. CLEAR YOUR BROWSER'S CACHE
//
// For Internet Explorer:
//
//	Open "Tools->Internet Options"
//	Select "Delete Files" under "Temporary Internet Files"
//	Click "OK"
//
// For Mozilla/Netscape Navigator:
//
//	Open "Edit->Preferences"
//	Select "Advanced"
//	Select "Proxies"
//	Click "Clear Disk Cache"
//	Click "Clear Memory Cache"
//
// For Firefox:
//
//	Open "Tools->Options"
//	Select the "Privay" tab
//	Scroll down or go to the "Cache" section
//	Click "Clear"
//
// For Opera:
//
//	Open "File->Preferences"
//	Select "History and cache"
//	Click "Empty now"
//


///////////////////////////////////////////////////////////////////////////////
//
// To see the definition of this page's JavaScript contents, see
//
//	http://home.netscape.com/eng/mozilla/2.0/relnotes/demo/proxy-live.html
//
// Microsoft includes this in their KB article:
//
//	http://support.microsoft.com/support/kb/articles/Q209/2/66.ASP
//
// Special PAC functions:
// Hostname:
//	isPlainHostName(host)
//	dnsDomainIs(host, domain)
//	localHostOrDomainIs(host, hostdom)
//	isResolvable(host)
//	isInNet(host, pattern, mask)
// Utility:
//	dnsResolve(host)
//	myIpAddress()
//	dnsDomainLevels(host)
// URL:
//	shExpMatch(str, shexp)
// Time:
//	weekdayRange(wd1, wd2, gmt)
//	dateRange(...)
//	timeRange(...)
//
// Other functions and methods that may work:
//	http://developer.netscape.com/docs/manuals/communicator/jsref/win1.htm
//	Note that "alert()" only works with Netscape4 and IE, and Mozilla 1.4+.
//
// NOTE:
//	isInNet() will resolve a hostname to an IP address, and cause
//	hangs on Mozilla/Firefox.  Currently, these are stubbed out and replaced
//	with shExpMatch(host, "a.b.c.*"), which doesn't do the same thing,
//	but is sufficient for these purposes.
//
// Additional Mozilla/Firefox comments:
//
//	All the above PAC functions are implemented in JavaScript,
//	and are added to the body of your PAC file when it is loaded.
//	See the "components/nsProxyAutoConfig.js" browser install
//	directory.
//
//	- shExpMatch() is implemented as three pattern.replaces()
//		 followed by a call to RegExp()  (SLOW)
//	- isPlainHostname() just checks for lack of "." in the string
//	- dnsDomainIs() just matches strings exactly
//	- alert() is bound to this.proxyAlert(), which displays a message
//		in the JavaScript console window

///////////////////////////////////////////////////////////////////////////////
//
// Regular Expressions
//
// Angus Turnbull pointed out the JavaScript 1.2 RE operators to me.
// These should work in NS4 and IE4 (or later), but I have only tested on
// Mozilla (1.3), IE5.5, and IE6.  PLEASE TELL ME IF IT WORKS FOR YOU!
//
// A good introduction is at:
//	http://www.evolt.org/article/Regular_Expressions_in_JavaScript/17/36435/
// Some references:
//	(old Netscape documentation is gone)
//	http://devedge.netscape.com/library/manuals/2000/javascript/1.5/reference/regexp.html
//	http://developer.netscape.com/docs/manuals/js/client/jsref/regexp.htm
//	http://www.webreference.com/js/column5/
//	http://msdn.microsoft.com/library/default.asp?url=/library/en-us/script56/html/js56jsobjRegExpression.asp
//	http://msdn.microsoft.com/library/default.asp?url=/library/en-us/script56/html/js56jsgrpRegExpSyntax.asp
// Real-time evaluator:
//	http://www.cuneytyilmaz.com/prog/jrx/
//
// I'm slowly replacing multiple glob patterns with regexps.
// By using RE literals of /.../ rather than the constructor 'new RegExp()',
// the regexps should be compiled as no-ads.pac is loaded.
// 
// Important notes:
// -	if using the constructor, \ needs to be quoted; thus "\\." is used
//	to match a literal '.'.  In the RE literal form, I need to end up
//	quoting any / for a URL path.
// -    Avoid these for now; they are broken or not supported in "older"
//	browsers such as NS4 and IE4:
//	- look-aheads (?=pat)
//	- non-greedy ? - a ? that follows *,+,?, and {}; (s)? is NOT non-greedy
//

// matches several common URL paths for ad images:
// such as: /banner/ /..._banner/ /banner_...
// but matches several words and includes plurals
var re_banner = /\/(.*_){0,1}(ad|adverts?|adimage|adframe|adserver|admentor|adview|banner|popup|popunder)(s)?[_.\/]/i;

// matches host names staring with "ad" but not (admin|add|adsl)
// or any hostname starting with "pop", "clicks", and "cash"
// or any hostname containing "banner"
// ^(ad(s)?.{0,4}\.|pop|click|cash|[^.]*banner|[^.]*adserv)
// ^(ad(?!(min|sl|d\.))|pop|click|cash|[^.]*banner|[^.]*adserv)
// ^(ad(?!(min|sl|d\.))|pop|click|cash|[^.]*banner|[^.]*adserv|.*\.ads\.)
var re_adhost = /^(www\.)?(ad(?!(ult|obe.*|min|sl|d|olly.*))|tology|pop|click|cash|[^.]*banner|[^.]*adserv|.+\.ads?\.)/i;

// neg:
//	admin.foobar.com
//	add.iahoo.com
//	adsl.allow.com
//	administration.all.net
// pos:
//	fire.ads.ighoo.com
//	ads.foo.org
//	ad0121.aaaa.com
//	adserver.goo.biz
//	popup.foo.bar

///////////////////////////////////////////////////////////////////////////////

var isActive = 1;

function FindProxyForURL(url, host)
{
    // debug
    // alert("checking: url=" + url + ", host=" + host);

    // Excellent kludge from Sean M. Burke:
    // Enable or disable no-ads for the current browser session.
    //
    // To disable, visit this URL:		http://no-ads.int/off
    // To re-enable, visit this URL:		http://no-ads.int/on
    //
    // (this will not work with Mozilla or Opera if the alert()s are present)
    //
    // This happens before lowercasing the URL, so make sure you use lowercase!
    //
    if (shExpMatch(host, "no-ads.int")) {
        if (shExpMatch(url, "*/on*")) {
	    isActive = 1;
	    //alert("no-ads is enabled.\n" + url);
	} else if (shExpMatch(url, "*/off*")) {
	    isActive = 0;
	    //alert("no-ads has been disabled.\n" + url);
	} else if (shExpMatch(url, "*no-ads.int/")) {
	    alert("no-ads is "+(isActive ? "enabled" : "disabled")+".\n" + url);
	} else {
	    alert("no-ads unknown option.\n" + url);
	}

	return blackhole;
    }

    if (!isActive) {
	// alert("allowing (not active): return " + bypass);
	return bypass;
    }

    // Suggestion from Quinten Martens
    // Make everything lower case.
    // WARNING: all shExpMatch rules following MUST be lowercase!
    url = url.toLowerCase();
    host = host.toLowerCase();

    //
    // Local/Internal rule
    // matches to this rule get the 'local' proxy.
    // Adding rules here enables the use of 'local'
    //
    if (0
	// LOCAL
	// add rules such as:
  	//	|| dnsDomainIs(host, "schooner.com")
	//	|| isPlainHostName(host)
	// or for a single host
	//	|| (host == "some-local-host")
	) {
	// alert("allowing (local): return " + localproxy);
	return localproxy;
    }

    //
    // Whitelist section from InvisiBill
    //
    // Add sites here that should never be matched for ads.
    //
    if (0
	// WHITELIST
    	// To add whitelist domains, simple add a line such as:
  	//	|| dnsDomainIs(host, "schooner.com")
	// or for a single host
	//	|| (host == "some-host-name")

	// Note: whitelisting schooner.com will defeat the "is-it-working"
	// test page at http://www.schooner.com/~loverso/no-ads/ads/

	// Apple.com "Switch" ads
	|| shExpMatch(url, "*.apple.com/switch/ads/*")

	// SprintPCS
	|| dnsDomainIs(host, ".sprintpcs.com")

	// Lego
	|| dnsDomainIs(host, ".lego.com")

	// Dell login popups
	|| host == "ecomm.dell.com"

	|| host == "click2tab.mozdev.org"
	|| host == "addons.mozilla.org"

	// Uncomment for metacrawler
	// || (host == "clickit.go2net.com")

        // Wunderground weather station banners
	|| shExpMatch(url, "*banners.wunderground.com/cgi-bin/banner/ban/wxbanner*")
	|| shExpMatch(url, "*banners.wunderground.com/weathersticker/*")
	) {
	// alert("allowing (whitelist): return " + normal);
	return normal;
    }

    // To add more sites, simply include them in the correct format.
    //
    // The sites below are ones I currently block.  Tell me of others you add!

    if (0
	// BLOCK
	// Block IE4/5 "favicon.ico" fetches
	// (to avoid being tracked as having bookmarked the site)
	|| shExpMatch(url, "*/favicon.ico")

	//////
	//
	// Global Section
	// tries to match common names
	//

	// RE for common URL paths
	|| re_banner.test(url)

	// RE for common adserver hostnames.
	// The regexp matches all hostnames starting with "ad" that are not
	//	admin|add|adsl
	// (replaces explicit shExpMatch's below)
	|| re_adhost.test(host)

//	|| (re_adhost.test(host)
//	    && !(
//	        shExpMatch(host, "add*")
//	     || shExpMatch(host, "admin*")
//	     || shExpMatch(host, "adsl*")
//	   )
//	)
//	// or any subdomain "ads"
//	|| (dnsDomainLevels(host) > 2 && shExpMatch(host, "*.ads.*"))

	//////
	//
	// banner/ad organizations
	// Just delete the entire namespace
	//

        // doubleclick
	|| dnsDomainIs(host, ".doubleclick.com")
        || dnsDomainIs(host, ".doubleclick.net")
        || dnsDomainIs(host, ".rpts.net")
	|| dnsDomainIs(host, ".2mdn.net")
	|| dnsDomainIs(host, ".2mdn.com")

	// these set cookies
	|| dnsDomainIs(host, ".globaltrack.com")
	|| dnsDomainIs(host, ".burstnet.com")
	|| dnsDomainIs(host, ".adbureau.net")
	|| dnsDomainIs(host, ".targetnet.com")
	|| dnsDomainIs(host, ".humanclick.com")
	|| dnsDomainIs(host, ".linkexchange.com")

	|| dnsDomainIs(host, ".fastclick.com")
	|| dnsDomainIs(host, ".fastclick.net")

        // one whole class C full of ad servers (fastclick)
	// XXX this might need the resolver
//        || isInNet(host, "205.180.85.0", "255.255.255.0")
	|| shExpMatch(host, "205.180.85.*")

	// these use 1x1 images to track you
	|| dnsDomainIs(host, ".admonitor.com")
	|| dnsDomainIs(host, ".focalink.com")

	|| dnsDomainIs(host, ".websponsors.com")
	|| dnsDomainIs(host, ".advertising.com")
	|| dnsDomainIs(host, ".cybereps.com")
	|| dnsDomainIs(host, ".postmasterdirect.com")
	|| dnsDomainIs(host, ".mediaplex.com")
	|| dnsDomainIs(host, ".adtegrity.com")
	|| dnsDomainIs(host, ".bannerbank.ru")
	|| dnsDomainIs(host, ".bannerspace.com")
	|| dnsDomainIs(host, ".theadstop.com")
	|| dnsDomainIs(host, ".l90.com")
	|| dnsDomainIs(host, ".webconnect.net")
	|| dnsDomainIs(host, ".avenuea.com")
	|| dnsDomainIs(host, ".flycast.com")
	|| dnsDomainIs(host, ".engage.com")
	|| dnsDomainIs(host, ".imgis.com")
	|| dnsDomainIs(host, ".datais.com")
	|| dnsDomainIs(host, ".link4ads.com")
	|| dnsDomainIs(host, ".247media.com")
	|| dnsDomainIs(host, ".hightrafficads.com")
	|| dnsDomainIs(host, ".tribalfusion.com")
	|| dnsDomainIs(host, ".rightserve.net")
	|| dnsDomainIs(host, ".admaximize.com")
	|| dnsDomainIs(host, ".valueclick.com")
	|| dnsDomainIs(host, ".adlibris.se")
	|| dnsDomainIs(host, ".vibrantmedia.com")
	|| dnsDomainIs(host, ".coremetrics.com")
	|| dnsDomainIs(host, ".vx2.cc")
	|| dnsDomainIs(host, ".webpower.com")
	|| dnsDomainIs(host, ".everyone.net")
	|| dnsDomainIs(host, ".zedo.com")
	|| dnsDomainIs(host, ".bigbangmedia.com")
	|| dnsDomainIs(host, ".ad-annex.com")
	|| dnsDomainIs(host, ".iwdirect.com")
	|| dnsDomainIs(host, ".adlink.de")
	|| dnsDomainIs(host, ".bidclix.net")
	|| dnsDomainIs(host, ".webclients.net")
	|| dnsDomainIs(host, ".linkcounter.com")
	|| dnsDomainIs(host, ".sitetracker.com")
	|| dnsDomainIs(host, ".adtrix.com")
	|| dnsDomainIs(host, ".netshelter.net")
	|| dnsDomainIs(host, ".rn11.com")
	// http://vpdc.ru4.com/content/images/66/011.gif
	|| dnsDomainIs(host, ".ru4.com")
	// no '.' for rightmedia.net
	|| dnsDomainIs(host, "rightmedia.net")
	|| dnsDomainIs(host, ".casalemedia.com")
	|| dnsDomainIs(host, ".casalemedia.com")

	// C-J
	|| dnsDomainIs(host, ".commission-junction.com")
	|| dnsDomainIs(host, ".qkimg.net")
	// emjcd.com ... many others

	// */adv/*
	|| dnsDomainIs(host, ".bluestreak.com")

	// Virtumundo -- as annoying as they get
	|| dnsDomainIs(host, ".virtumundo.com")
	|| dnsDomainIs(host, ".treeloot.com")
	|| dnsDomainIs(host, ".memberprize.com")

	// internetfuel and _some_ of the sites they redirect to
	// (more internetfuel - from Sam G)
	|| dnsDomainIs(host, ".internetfuel.net")
	|| dnsDomainIs(host, ".internetfuel.com")
	|| dnsDomainIs(host, ".peoplecaster.com")
	|| dnsDomainIs(host, ".cupidsdatabase.com")
	|| dnsDomainIs(host, ".automotive-times.com")
	|| dnsDomainIs(host, ".healthy-lifetimes.com")
	|| dnsDomainIs(host, ".us-world-business.com")
	|| dnsDomainIs(host, ".internet-2-web.com")
	|| dnsDomainIs(host, ".my-job-careers.com")
	|| dnsDomainIs(host, ".freeonline.com")
	|| dnsDomainIs(host, ".exitfuel.com")
	|| dnsDomainIs(host, ".netbroadcaster.com")
	|| dnsDomainIs(host, ".spaceports.com")
	|| dnsDomainIs(host, ".mircx.com")
	|| dnsDomainIs(host, ".exitchat.com")
	|| dnsDomainIs(host, ".atdmt.com")
	|| dnsDomainIs(host, ".partner2profit.com")
	|| dnsDomainIs(host, ".centrport.net")
	|| dnsDomainIs(host, ".centrport.com")
	|| dnsDomainIs(host, ".rampidads.com")

	//////
	//
	// banner servers
	// (typically these set cookies or serve animated ads)
	//

	|| dnsDomainIs(host, "commonwealth.riddler.com")
	|| dnsDomainIs(host, "banner.freeservers.com")
	|| dnsDomainIs(host, "usads.futurenet.com")
	|| dnsDomainIs(host, "banners.egroups.com")
	|| dnsDomainIs(host, "ngadclient.hearme.com")
	|| dnsDomainIs(host, "affiliates.allposters.com")
	|| dnsDomainIs(host, "adincl.go2net.com")
	|| dnsDomainIs(host, "webads.bizservers.com")
	|| dnsDomainIs(host, ".addserv.com")
	|| dnsDomainIs(host, ".falkag.net")
	|| (host == "promote.pair.com")

	// marketwatch.com (flash ads), but CSS get loaded
	|| (dnsDomainIs(host, ".mktw.net")
	    && !shExpMatch(url, "*/css/*"))
	|| dnsDomainIs(host, ".cjt1.net")
	|| dnsDomainIs(host, ".bns1.net")
	
	// "undergroundonline"
	// comes from iframe with this url: http://mediamgr.ugo.com/html.ng/size=728x90&affiliate=megagames&channel=games&subchannel=pc&Network=affiliates&rating=g
	|| dnsDomainIs(host, "image.ugo.com")
	|| dnsDomainIs(host, "mediamgr.ugo.com")

	// web ads and "cheap Long Distance"
	|| dnsDomainIs(host, "zonecms.com")
	|| dnsDomainIs(host, "zoneld.com")

	// AOL
	|| dnsDomainIs(host, ".atwola.com")
	|| dnsDomainIs(host, "toolbar.aol.com")

	// animated ads shown at techbargains
	|| (dnsDomainIs(host, ".overstock.com")
	    && shExpMatch(url, "*/linkshare/*"))
	|| (dnsDomainIs(host, ".supermediastore.com")
	    && shExpMatch(url, "*/lib/supermediastore/*"))
	|| (dnsDomainIs(host, ".shop4tech.com")
	    && shExpMatch(url, "*/assets/*"))
	|| (dnsDomainIs(host, ".softwareandstuff.com")
	    && shExpMatch(url, "*/media/*"))
	|| (dnsDomainIs(host, ".buy.com")
	    && shExpMatch(url, "*/affiliate/*"))

	|| (dnsDomainIs(host, "pdaphonehome.com")
	    && (shExpMatch(url, "*/pocketpcmagbest.gif")
		|| shExpMatch(url, "*/link-msmobiles.gif")))
	|| (dnsDomainIs(host, "ppc4you.com")
	    && shExpMatch(url, "*/ppc_top_sites.gif"))

	// more animated ads... these really drive me crazy
	|| (dnsDomainIs(host, ".freewarepalm.com")
	    && shExpMatch(url, "*/sponsors/*"))

	//////
	//
	// popups/unders
	//

	|| dnsDomainIs(host, "remotead.cnet.com")
	|| dnsDomainIs(host, ".1st-dating.com")
	|| dnsDomainIs(host, ".mousebucks.com")
	|| dnsDomainIs(host, ".yourfreedvds.com")
	|| dnsDomainIs(host, ".popupsavings.com")
	|| dnsDomainIs(host, ".popupmoney.com")
	|| dnsDomainIs(host, ".popuptraffic.com")
	|| dnsDomainIs(host, ".popupnation.com")
	|| dnsDomainIs(host, ".infostart.com")
	|| dnsDomainIs(host, ".popupad.net")
	|| dnsDomainIs(host, ".usapromotravel.com")
	|| dnsDomainIs(host, ".goclick.com")
	|| dnsDomainIs(host, ".trafficwave.net")
	|| dnsDomainIs(host, ".popupad.net")
	|| dnsDomainIs(host, ".paypopup.com")

	// Popups from ezboard
	|| dnsDomainIs(host, ".greenreaper.com")
	|| dnsDomainIs(host, ".spewey.com")
	|| dnsDomainIs(host, ".englishharbour.com")
	|| dnsDomainIs(host, ".casino-trade.com")
	|| dnsDomainIs(host, "got2goshop.com")
	// more ezboard crud (from Miika Asunta)
	|| dnsDomainIs(host, ".addynamix.com")
	|| dnsDomainIs(host, ".trafficmp.com")
	|| dnsDomainIs(host, ".makingmoneyfromhome.net")
	|| dnsDomainIs(host, ".leadcart.com")

	// http://www.power-mark.com/js/popunder.js
	|| dnsDomainIs(host, ".power-mark.com")

	//////
	//
	// User tracking (worse than ads) && hit counting "services"
	//

	// "web trends live"
	|| dnsDomainIs(host, ".webtrendslive.com")
	|| dnsDomainIs(host, ".wtlive.com")

	// 1x1 tracking images
	// ** (but also used in some pay-for-clicks that I want to follow,
	// **  so disabled for now.  9/2001)
	// || dnsDomainIs(host, "service.bfast.com")

	// one whole class C full of ad servers
	// XXX this might need the resolver
//	|| isInNet(host, "66.40.16.0", "255.255.255.0")
	|| shExpMatch(host, "66.40.16.*")

	|| dnsDomainIs(host, ".web-stat.com")
	|| dnsDomainIs(host, ".superstats.com")
	|| dnsDomainIs(host, ".allhits.ru")
	|| dnsDomainIs(host, ".list.ru")
	|| dnsDomainIs(host, ".counted.com")
	|| dnsDomainIs(host, ".rankyou.com")
	|| dnsDomainIs(host, ".clickcash.com")
	|| dnsDomainIs(host, ".clickbank.com")
	|| dnsDomainIs(host, ".paycounter.com")
	|| dnsDomainIs(host, ".cashcount.com")
	|| dnsDomainIs(host, ".clickedyclick.com")
	|| dnsDomainIs(host, ".clickxchange.com")
	|| dnsDomainIs(host, ".sitestats.com")
	|| dnsDomainIs(host, ".site-stats.com")
	|| dnsDomainIs(host, ".hitbox.com")
	|| dnsDomainIs(host, ".exitdirect.com")
	|| dnsDomainIs(host, ".realtracker.com")
	|| dnsDomainIs(host, ".etracking.com")
	|| dnsDomainIs(host, ".livestat.com")
	|| dnsDomainIs(host, ".spylog.com")
	|| dnsDomainIs(host, ".freestats.com")
	|| dnsDomainIs(host, ".addfreestats.com")
	|| dnsDomainIs(host, ".topclicks.net")
	|| dnsDomainIs(host, ".mystat.pl")
	|| dnsDomainIs(host, ".hitz4you.de")
	|| dnsDomainIs(host, ".hitslink.com")
	|| dnsDomainIs(host, ".thecounter.com")
	|| dnsDomainIs(host, ".roiservice.com")
	|| dnsDomainIs(host, ".overture.com")
	|| dnsDomainIs(host, ".xiti.com")
	|| dnsDomainIs(host, ".cj.com")
	|| dnsDomainIs(host, ".anrdoezrs.net")
	|| dnsDomainIs(host, ".hey.it")
	|| dnsDomainIs(host, ".ppctracking.net")
	|| dnsDomainIs(host, ".darkcounter.com")
	|| dnsDomainIs(host, ".2o7.com")
	|| dnsDomainIs(host, ".2o7.net")
	|| dnsDomainIs(host, ".gostats.com")
	|| dnsDomainIs(host, ".everstats.com")
	|| dnsDomainIs(host, ".onestat.com")
	|| dnsDomainIs(host, ".statcounter.com")
	|| dnsDomainIs(host, ".trafic.ro")
	|| dnsDomainIs(host, ".exitexchange.com")

	// clickability, via CNN
	|| dnsDomainIs(host, ".clickability.com")
	|| dnsDomainIs(host, ".savethis.com")

	//////
	//
	// Dead domain parking
	//
	|| dnsDomainIs(host, ".netster.com")

	//////
	//
	// Search engine "optimizers"
	//
	|| dnsDomainIs(host, ".searchmarketing.com")

	//////
	//
	// Spyware/worms
	//

	|| dnsDomainIs(host, ".friendgreetings.com")
	|| dnsDomainIs(host, ".permissionedmedia.com")
	|| dnsDomainIs(host, ".searchbarcash.com")

	//////
	//
	// "Surveys"
	//

	|| dnsDomainIs(host, ".zoomerang.com")

	//////
	//
	// "Casino" ads (scams)
	//

	|| dnsDomainIs(host, ".aceshigh.com")
	|| dnsDomainIs(host, ".idealcasino.net")
	|| dnsDomainIs(host, ".casinobar.net")
	|| dnsDomainIs(host, ".casinoionair.com")

	|| (dnsDomainIs(host, ".go2net.com")
	    && shExpMatch(url, "*adclick*")
	)

	//////
	//
	// Spammers
	//

	|| dnsDomainIs(host, ".licensed-collectibles.com")
	|| dnsDomainIs(host, ".webdesignprofessional.com")

	//////
	//
	// Directed at extra annoying places
	//

	// Attempts to download ad-supported spyware without asking first
	|| dnsDomainIs(host, ".gator.com")

	// ebay
	|| ((dnsDomainIs(host, "pics.ebay.com")
	     || dnsDomainIs(host, "pics.ebaystatic.com"))
	    && shExpMatch(url, "*/pics/mops/*/*[0-9]x[0-9]*")
	)
	|| (dnsDomainIs(host, "ebayobjects.com")
	    && shExpMatch(url, "*search/keywords*")
	)
	|| dnsDomainIs(host, "admarketplace.com")
	|| dnsDomainIs(host, "admarketplace.net")

	// Bravenet & Ezboard
	|| (dnsDomainIs(host, ".ezboard.com")
	    && shExpMatch(url, "*/bravenet/*")
	)
	|| (dnsDomainIs(host, ".bravenet.com")
	    && (   shExpMatch(host, "*counter*")
		|| shExpMatch(url, "*/jsbanner*")
	        || shExpMatch(url, "*/bravenet/*")
	    )
	)

	// GeoCities
	// (checking "toto" from Prakash Persaud)
	|| ((   dnsDomainIs(host,"geo.yahoo.com")
	     || dnsDomainIs(host,".geocities.com"))
	    && (
		   shExpMatch(url,"*/toto?s*")
		|| shExpMatch(url, "*geocities.com/js_source*")
		|| dnsDomainIs(host, "visit.geocities.com")
	    )
	)

	// Yahoo ads (direct and via Akamai)
	// http://us.a1.yimg.com/us.yimg.com/a/...
	|| (dnsDomainIs(host,"yimg.com")
	    && (   shExpMatch(url,"*yimg.com/a/*")
		|| shExpMatch(url,"*yimg.com/*/adv/*")
	    )
	)
	// "eyewonder" ads at Yahoo
	|| dnsDomainIs(host,"qz3.net")
	|| dnsDomainIs(host,".eyewonder.com")

	// background ad images
	|| dnsDomainIs(host,"buzzcity.com")

	// FortuneCity - ads and tracking
	|| (dnsDomainIs(host,".fortunecity.com")
	    && (    shExpMatch(url,"*/js/adscript*")
		 || shExpMatch(url,"*/js/fctrack*")
	    )
	)

	// zdnet
	// tracking webbugs:
	// http://gserv.zdnet.com/clear/ns.gif?a000009999999999999+2093
	|| (dnsDomainIs(host, ".zdnet.com")
	    && (   dnsDomainIs(host, "ads3.zdnet.com")
		|| host == "gserv.zdnet.com"
		|| shExpMatch(url, "*/texis/cs/ad.html")
		|| shExpMatch(url, "*/adverts")
	     )
	)

	// cnet
	// web bugs and ad redirections
	// taken care of by hostname rules:
	//	http://adimg.com.com/...
	//	http://adlog.com.com/...
	// http://dw.com.com/clear/c.gif
	// http://dw.com.com/redir?astid=2&destUrl=http%3A%2F%2Fwww.buy ...
	// http://mads.com.com/mac-ad?...
	|| (host == "dw.com.com" || host == "mads.com.com")
	|| (dnsDomainIs(host, ".com.com")
	    && (   host == "dw.com.com"
		|| host == "mads.com.com"
	     )
	)

	// nytimes
	|| (dnsDomainIs(host, ".nytimes.com")
	    && shExpMatch(url,"*/adx/*")
	)

	// pop-after
	|| dnsDomainIs(host, ".unicast.net")


	// Be Free affiliate ads
	|| dnsDomainIs(host, ".reporting.net")
	|| dnsDomainIs(host, ".affliate.net")
	|| (dnsDomainIs(host, ".akamai.net")
	    && shExpMatch(url, "*.affiliate.net/*")
	)

	// Infospace.com popunder
	// for "webmarket.com" & "shopping.dogpile.com" -- just say no!
	|| (dnsDomainIs(host, ".infospace.com")
	    && shExpMatch(url, "*/goshopping/*")
	)
	|| dnsDomainIs(host, ".webmarket.com")
	|| dnsDomainIs(host, "shopping.dogpile.com")

	// goto.com popunder for information.gopher.com
	|| dnsDomainIs(host, "information.gopher.com")

	// About.com popunder and floating ad bar
	|| (dnsDomainIs(host, ".about.com")
	    && (0
	    || shExpMatch(url, "*/sprinks/*")
	    || shExpMatch(url, "*about.com/0/js/*")
	    || shExpMatch(url, "*about.com/f/p/*")
	    )
	)

	// Dell
	|| (dnsDomainIs(host, ".dell.com")
	    && shExpMatch(url, "*/images/affiliates/*")
	)

	// IFilm iframes
	|| (dnsDomainIs(host, ".ifilm.com")
	    && (shExpMatch(url, "*/partners/*")
	        || shExpMatch(url, "*/redirect*")
	    )
	)

	// tomshardware
	// they are most annoying:
	// - cookies on their background images to track you
	// - looping shockwave ads
	// this kills most of the crud
//	     || isInNet(host, "216.92.21.0", "255.255.255.0")
	|| ((dnsDomainIs(host, ".tomshardware.com")
	     || shExpMatch(host, "216.92.21.*"))
	    && (   shExpMatch(url, "*/cgi-bin/banner*")
	        || shExpMatch(url, "*/cgi-bin/bd.m*")
	        || shExpMatch(url, "*/images/banner/*")
	    )
	)

	|| shExpMatch(url, "*mapsonus.com/ad.images*")

	// Slashdot: added these when I saw hidden 1x1 images with cookies
	|| dnsDomainIs(host, "adfu.blockstackers.com")
	|| (dnsDomainIs(host, "slashdot.org")
	    && (
	           shExpMatch(url, "*/slashdot/pc.gif*")
		|| shExpMatch(url, "*/pagecount.gif*")
		|| shExpMatch(url, "*/adlog.pl*")
	    )
        )
	|| dnsDomainIs(host, "googlesyndication.com")
	|| dnsDomainIs(host, "google-analytics.com")

	// it-aint-cool.com
	|| (dnsDomainIs(host, "aintitcool.com")
	    && (
	           shExpMatch(url, "*/newline/*")
		|| shExpMatch(url, "*/drillteammedia/*")
		|| shExpMatch(url, "*/foxsearchlight/*")
		|| shExpMatch(url, "*/media/aol*")
		|| shExpMatch(url, "*swf")
	    )
	)

	// Staples & CrossMediaServices
	|| (dnsDomainIs(host, ".staples.com")
	    && shExpMatch(url, "*/pixeltracker/*")
	)
	|| dnsDomainIs(host, "pt.crossmediaservices.com")

	// OfficeMax affiliate art (affArt->affart because of toLowerCase)
	|| (dnsDomainIs(host, ".officemax.com")
	    && shExpMatch(url, "*/affart/*")
	)

	// complicated JavaScript for directed ads!
// 1/5/2004: allow /js/ as they now use it for graphs
//	|| (dnsDomainIs(host, ".anandtech.com")
//	    && (shExpMatch(url,"*/js/*")
//	        || shExpMatch(url,"*/bnr_*")
//	    )
//	)

	// hardocp
	// http://65.119.30.151/UploadFilesForNewegg/onlineads/newegg728hardocp.swf
	|| (host == "hera.hardocp.com")
	|| shExpMatch(url,"*/onlineads/*")

	// complicated JavaScript for gliding ads!
	|| (dnsDomainIs(host, ".fatwallet.com")
	    && shExpMatch(url,"*/js/*")
	)

	// cnet ads
	|| dnsDomainIs(host, "promo.search.com")

	// IMDB celeb photos
	// (Photos/CMSIcons->photos/cmsicons because of toLowerCase)
	|| (dnsDomainIs(host, "imdb.com")
	    && (   shExpMatch(url, "*/photos/cmsicons/*")
	        || shExpMatch(url, "*/icons/*/celeb/*")
	        || shExpMatch(url, "*.swf")
	    )
	)
	// incredibly annoying IMDB shock/flash ads
	|| dnsDomainIs(host, "kliptracker.com")
	|| dnsDomainIs(host, "klipmart.com")

	|| host == "spinbox.techtracker.com"

	// Amazon affiliate 'search'. retrieves a JS that writes new HTML
	// that references one or more images "related to your search".
	// (If there is a real use for rcm.amazon.com, let me know)
	// http://rcm.amazon.com/e/cm?t=starlingtechnolo&amp;l=st1&amp;search=cynicism&amp;mode=books&amp;p=11&amp;o=1&amp;bg1=CEE7FF&amp;fc1=000000&amp;lc1=083194&amp;lt1=_blank
	|| host == "rcm.amazon.com"

	//////
	//
	// "Other Scum And Villainry"
	//

	// Popup from "reserved" domains at register.com
	// (I considered blocking all of register.com)
	|| (dnsDomainIs(host, ".register.com")
	    && (shExpMatch(url,"*.js")
		|| shExpMatch(host, "searchtheweb*")
		|| shExpMatch(host, "futuresite*")
	    )
	)

	|| dnsDomainIs(host, ".oingo.com")
	|| dnsDomainIs(host, ".namingsolutions.com")

	// "Data collection"
	|| dnsDomainIs(host, ".coremetrics.com")

	// Sets your home page
	|| dnsDomainIs(host, ".firehunt.com")

	// tracking
	|| dnsDomainIs(host, ".appliedsemantics.com")

	// Scum who buy ad space from the above
	// || dnsDomainIs(host, ".hartfordrents.com")
	// || dnsDomainIs(host, ".chicagocomputerrentals.com")
	// || dnsDomainIs(host, ".ccrsolutions.com")
	// || dnsDomainIs(host, ".rushcomputer.com")
	// || dnsDomainIs(host, ".localesimates.com")
	// || dnsDomainIs(host, ".unitedvision.com")
	// XXX this might need the resolver
//	|| isInNet(host, "216.216.246.31", "255.255.255.255")
	|| (host == "216.216.246.31")

	// avsforum ads
//	|| isInNet(host, "216.66.21.35", "255.255.255.255")
	|| (host == "216.66.21.35")
	|| dnsDomainIs(host, ".avsads.com")

	// bogus "search" sites at non-existent sites
	|| dnsDomainIs(host, ".search411.com")

	// palmgear.com
	|| (dnsDomainIs(host, ".palmgear.com")
	    && (   shExpMatch(url, "*/adsales/*")
		|| shExpMatch(url, "*/emailblast*")
	    )
	)

	//////
	//
	// Contributed adult sites
	//

	|| dnsDomainIs(host, ".porntrack.com")
	|| dnsDomainIs(host, ".sexe-portail.com")
	|| dnsDomainIs(host, ".sextracker.com")
	|| dnsDomainIs(host, ".sexspy.com")
	|| dnsDomainIs(host, ".offshoreclicks.com")
	|| dnsDomainIs(host, ".exxxit.com")
	|| dnsDomainIs(host, "private-dailer.biz")
	|| shExpMatch(url, "*retestrak.nl/misc/reet.gif")
	|| shExpMatch(url, "*dontstayin.com/*.swf")

	// debug
	// || (alertmatch("NOT:" + url) && 0)

	) {

	// alert("blackholing: " + url);

	// deny this request
	return blackhole;

    } else {
	// debug
	// alert("allowing: " + url);

	// all other requests go direct and avoid any overhead
	return normal;
    }
}

///////////////////////////////////////////////////////////////////////////////
//
// This line is just for testing; you can ignore it.  But, if you are having
// problems where you think this PAC file isn't being loaded, then change this
// to read "if (1)" and the alert box should appear when the browser loads this
// file.
//
// This works for IE4, IE5, IE5.5, IE6 and Netscape 2.x, 3.x, and 4.x.
// (For IE6, tested on Win2K)
// This does not work for Mozilla before 1.4 (and not for Netscape 6.x).
// In Mozilla 1.4+ and Fireox, this will write to the JavaScript console.
//
if (0) {
	alert("no-ads.pac: LOADED:\n" +
		"	version:	"+noadsver+"\n" +
		"	normal:		"+normal+"\n" +
		"	blackhole:	"+blackhole+"\n" +
		"	localproxy:	"+localproxy+"\n" +
		"	bypass:		"+bypass+"\n"
		// MSG
	);
}

// The above should show you that this JavaScript is executed in an
// unprotected global context.  NEVER point at someone elses autoconfig file;
// always load from your own copy!

// an alert that returns true
function alertmatch(str)
{
	// alert("match: "+str);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
//
// Replacement function for dnsDomainIs().  This is to replace the
// prefix problem, which a leading '.' used to be used for.
//
//	dnsDomainIs("bar.com", "bar.com") => true
//	dnsDomainIs("www.bar.com", "bar.com") => true
//	dnsDomainIs("www.foobar.com", "bar.com") => true	<<< incorrect
//
//	isInDomain("bar.com", "bar.com") => true
//	isInDomain("www.bar.com", "bar.com") => true
//	isInDomain("www.foobar.com", "bar.com") => false	<<< correct
//
function isInDomain(host, domain) {
    if (host.length > domain.length) {
	return (host.substring(host.length - domain.length - 1) == "."+domain);
    }
    return (host == domain);
}

///////////////////////////////////////////////////////////////////////////////
//
// Tired of reading boring comments?  Try reading today's comics:
//	http://www.schooner.com/~loverso/comics/
//
// or getting a quote from my collection:
//	http://www.schooner.com/~loverso/quote/
//

// eof
	//intelliserv.net
	//intellisrv.net
	//rambler.ru
	//rightmedia.net
	//calloffate.com
	//fairmeasures.com

