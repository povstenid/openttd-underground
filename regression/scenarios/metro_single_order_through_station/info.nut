class MetroSingleOrderThroughStation extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_single_order_through_station"; }
	function GetShortName()   { return "MSOT"; }
	function GetDescription() { return "GS metro scenario: trains whose only order is an underground through station leave it again with their consist in one piece."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-28"; }
	function CreateInstance() { return "MetroSingleOrderThroughStation"; }
}

RegisterGS(MetroSingleOrderThroughStation());
