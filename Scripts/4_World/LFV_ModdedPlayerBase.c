// =========================================================
// LF_VStorage -- Modded PlayerBase (vanilla RPC receiver)
//
// Receives server settings via RPCSingleParam.OnRPC.
// Same pattern as BaseBuildingPlus (PlayerBase.OnRPC).
// =========================================================

modded class PlayerBase
{
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        switch (rpc_type)
        {
            case LFV_RPC.SYNC_SETTINGS:
            {
                Param1<LFV_Settings> data;
                if (!ctx.Read(data))
                    return;

                LFV_Module module = LFV_Module.GetModule();
                if (module)
                    module.ApplyServerSettings(data.param1);
                break;
            }

            case LFV_RPC.ADMIN_COMMAND:
            {
                #ifdef SERVER
                Param1<string> cmdData;
                if (!ctx.Read(cmdData))
                    return;

                LFV_Module module2 = LFV_Module.GetModule();
                if (module2)
                    module2.HandleAdminCommandFromRPC(cmdData.param1, sender);
                #endif
                break;
            }
        }
    }
}
