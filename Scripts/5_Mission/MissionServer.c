// =========================================================
// LF_VStorage -- MissionServer (vanilla RPC pattern)
//
// Sends settings to each connecting client via RPCSingleParam.
// Same pattern as BaseBuildingPlus (InvokeOnConnect).
// =========================================================

modded class MissionServer
{
    override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
    {
        super.InvokeOnConnect(player, identity);

        if (!player || !identity)
            return;

        LFV_Module module = LFV_Module.GetModule();
        if (!module)
            return;

        LFV_Settings settings = module.GetSettings();
        if (!settings)
            return;

        // Strip server-only fields before sending to client.
        // Client only needs container lists for ActionCondition.
        ref array<string> savedAdminUIDs = settings.m_AdminUIDs;
        settings.m_AdminUIDs = new array<string>();

        auto param = new Param1<LFV_Settings>(settings);
        GetGame().RPCSingleParam(player, LFV_RPC.SYNC_SETTINGS, param, true, identity);

        // Restore original AdminUIDs on server
        settings.m_AdminUIDs = savedAdminUIDs;
    }
}
