class MetroPassengerAcceptanceViaExit extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_passenger_acceptance_via_exit"; }
	function GetShortName()   { return "MPVE"; }
	function GetDescription() { return "GS metro regression: passengers must reach an underground station via an explicit surface exit and be delivered to a surface town."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroPassengerAcceptanceViaExit"; }
}

RegisterGS(MetroPassengerAcceptanceViaExit());
