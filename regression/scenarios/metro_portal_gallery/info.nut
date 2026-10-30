class MetroPortalGallery extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_gallery"; }
	function GetShortName()   { return "MPGL"; }
	function GetDescription() { return "GS metro regression: every portal and ramp in all four directions, saved for looking at them."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-30"; }
	function CreateInstance() { return "MetroPortalGallery"; }
}

RegisterGS(MetroPortalGallery());
