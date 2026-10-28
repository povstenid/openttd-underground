class MetroLoopAcceptance extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_loop_acceptance"; }
	function GetShortName()   { return "MLAC"; }
	function GetDescription() { return "GS metro acceptance: closed loop half surface / half underground, stations and signals on both halves, two wagon trains, two full circuits each."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-10"; }
	function CreateInstance() { return "MetroLoopAcceptance"; }
}

RegisterGS(MetroLoopAcceptance());
