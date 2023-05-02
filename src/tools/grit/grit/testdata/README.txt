Google Desktop for Enterprise
Copyright (C) 2007 Google Inc.
All Rights Reserved

---------
Contents
---------
This distribution contains the following files:

GoogleDesktopSetup.msi - Installation and setup program
GoogleDesktop.adm - Group Policy administrative template file
AdminGuide.pdf - Google Desktop for Enterprise administrative guide


--------------
Documentation
--------------
Full documentation and installation instructions are in the 
administrative guide, and also online at 
http://desktop.google.com/enterprise/adminguide.html.


------------------------
IBM Lotus Notes Plug-In
------------------------
The Lotus Notes plug-in is included in the release of Google 
Desktop for Enterprise. The IBM Lotus Notes Plug-in for Google 
Desktop indexes mail, calendar, task, contact and journal 
documents from Notes.  Discussion documents including those from 
the discussion and team room templates can also be indexed by 
selecting an option from the preferences.  Once indexed, this data
will be returned in Google Desktop searches.  The corresponding
document can be opened in Lotus Notes from the Google Desktop 
results page.

Install: The plug-in will install automatically during the Google 
Desktop setup process if Lotus Notes is already installed.  Lotus 
Notes must not be running in order for the install to occur.  The
Class ID for this plug-in is {8F42BDFB-33E8-427B-AFDC-A04E046D3F07}.

Preferences: Preferences and selection of databases to index are
set in the 'Google Desktop for Notes' dialog reached through the 
'Actions' menu.

Reindexing: Selecting 'Reindex all databases' will index all the 
documents in each database again.


Notes Plug-in Known Issues
---------------------------

If the 'Google Desktop for Notes' item is not available from the 
Lotus Notes Actions menu, then installation was not successful. 
Installation consists of writing one file, notesgdsplugin.dll, to 
the Notes application directory and a setting to the notes.ini 
configuration file.  The most likely cause of an unsuccessful 
installation is that the installer was not able to locate the 
notes.ini file. Installation will complete if the user closes Notes
and manually adds the following setting to this file on a new line:
AddinMenus=notesgdsplugin.dll

If the notesgdsplugin.dll file is not in the application directory
(e.g., C:\Program Files\Lotus\Notes) after Google Desktop 
installation, it is likely that Notes was not installed correctly. 

Only local databases can be indexed.  If they can be determined, 
the user's local mail file and address book will be included in the
list automatically.  Mail archives and other databases must be 
added with the 'Add' button.

Some users may experience performance issues during the initial 
indexing of a database.  The 'Perform the initial index of a 
database only when I'm idle' option will limit the indexing process
to times when the user is not using the machine. If this does not 
alleviate the problem or the user would like to continually index 
but just do so more slowly or quickly, the GoogleWaitTime notes.ini
value can be set. Increasing the GoogleWaitTime value will slow 
down the indexing process, and lowering the value will speed it up.
A value of zero causes the fastest possible indexing.  Removing the
ini parameter altogether returns it to the default (20).

Crashes have been known to occur with certain types of history 
bookmarks.  If the Notes client seems to crash randomly, try 
disabling the 'Index note history' option.  If it crashes before,
you can get to the preferences, add the following line to your 
notes.ini file:
GDSNoIndexHistory=1
