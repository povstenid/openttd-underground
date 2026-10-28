class MetroPortalExitHeadOn extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_exit_head_on"; }
	function GetShortName()   { return "MPHO"; }
	function GetDescription() { return "GS metro regression: a train whose way on from a portal mouth is held waits below ground at its last signal, so a train diving into that mouth can pass; no head-to-head deadlock in the mouth."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-25"; }
	function CreateInstance() { return "MetroPortalExitHeadOn"; }
}
RegisterGS(MetroPortalExitHeadOn());
