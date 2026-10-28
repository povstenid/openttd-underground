class MetroPortalCollisionControl extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_collision_control"; }
	function GetShortName()   { return "MPCC"; }
	function GetDescription() { return "GS metro negative control: two trains sent head-on through a portal must still collide; layer separation must not make portal tiles collision-free."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroPortalCollisionControl"; }
}

RegisterGS(MetroPortalCollisionControl());
