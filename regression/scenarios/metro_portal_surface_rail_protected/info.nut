class MetroPortalSurfaceRailProtected extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_surface_rail_protected"; }
	function GetShortName()   { return "MPSP"; }
	function GetDescription() { return "The surface rail of a metro portal cannot be removed or demolished; GSMetro.RemovePortal removes both."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroPortalSurfaceRailProtected"; }
}

RegisterGS(MetroPortalSurfaceRailProtected());
