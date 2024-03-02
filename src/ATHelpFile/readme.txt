README for ATHelpFile.sln
=========================

This is the solution for the HTML Help (CHM) help file used by Altirra. In
case you're wondering how to work on it, the XML files are designed to be
viewed in a web browser; they have processing instructions to automatically
apply necessary the XSLT transforms to convert them to HTML for display.

Since recent web browsers now treat local files from referring to other
local files as cross-domain access and prevent it from security reasons,
the StartWebServer.cmd file is provided to launch a local Python web server
in the source directory. The files are then available on localhost:8000,
e.g. http://localhost:8000/index.xml.
