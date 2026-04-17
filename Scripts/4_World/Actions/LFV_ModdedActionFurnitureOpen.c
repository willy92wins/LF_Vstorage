// =========================================================
// LF_VStorage -- Modded ActionFurnitureOpen (Phase 2)
//
// Hooks Paragon's open action (separate from close).
// super.OnStartServer() calls building.Open() -> IsOpen()=true.
//
// Sequence: super FIRST (Open), then LFV restore.
// Matches LFV_ModdedActionOpenRaGItem pattern.
// =========================================================

#ifdef Paragon_Storage
modded class ActionFurnitureOpen
{
    override void OnStartServer(ActionData action_data)
    {
        super.OnStartServer(action_data);

        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        if (container && LFV_Registry.IsVirtualContainer(container.GetType()))
        {
            PlayerBase player = PlayerBase.Cast(action_data.m_Player);
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                if (!module.CheckRateLimit(player, container))
                    return;
                module.OnOpenContainer(container, player);
            }
        }
    }
}
#endif
