class MetroUndergroundOrderFlags extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_underground_order_flags"; }
	function GetShortName()   { return "MUOF"; }
	function GetDescription() { return "GS metro regression: GSOrder.SetOrderFlags works on underground station orders (non-stop + full load waits, clearing it lets the train go)."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroUndergroundOrderFlags"; }
}

RegisterGS(MetroUndergroundOrderFlags());
