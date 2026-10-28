class MetroSignalBlockHold extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_block_hold"; }
	function GetShortName()   { return "MSBH"; }
	function GetDescription() { return "GS metro regression: block signals hold following trains while their block is occupied and release them afterwards."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalBlockHold"; }
}
RegisterGS(MetroSignalBlockHold());
