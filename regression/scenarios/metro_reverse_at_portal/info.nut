class MetroReverseAtPortal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_reverse_at_portal"; }
	function GetShortName()   { return "MRAP"; }
	function GetDescription() { return "GS metro scenario: a multi-part train reversed at many moments while it straddles a portal must not disconnect or crash."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-25"; }
	function CreateInstance() { return "MetroReverseAtPortal"; }
}

RegisterGS(MetroReverseAtPortal());