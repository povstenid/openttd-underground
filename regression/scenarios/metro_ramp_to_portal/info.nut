class MetroRampToPortal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_ramp_to_portal"; }
	function GetShortName()   { return "MRTP"; }
	function GetDescription() { return "GS metro regression: a ramp up to a level that is already the surface becomes a portal."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroRampToPortal"; }
}

RegisterGS(MetroRampToPortal());
