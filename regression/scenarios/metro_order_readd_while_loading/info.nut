class MetroOrderReaddWhileLoading extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_order_readd_while_loading"; }
	function GetShortName()   { return "MORL"; }
	function GetDescription() { return "GS metro regression: removing and re-adding the underground station order while the train loads there keeps the train calling at it."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroOrderReaddWhileLoading"; }
}

RegisterGS(MetroOrderReaddWhileLoading());
