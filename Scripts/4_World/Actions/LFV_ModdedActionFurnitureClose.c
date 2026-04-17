// =========================================================
// LF_VStorage -- Modded ActionFurnitureClose (Phase 2)
//
// Hooks Paragon's close action. Virtualize BEFORE super so
// items are still in the container when captured.
// =========================================================

#ifdef Paragon_Storage
modded class ActionFurnitureClose
{
    override void OnStartServer(ActionData action_data)
    {
        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        if (container && LFV_Registry.IsVirtualContainer(container.GetType()))
        {
            PlayerBase player = PlayerBase.Cast(action_data.m_Player);
            LFV_Module module = LFV_Module.GetModule();
            if (module)
                module.OnCloseContainer(container, player);
        }

        super.OnStartServer(action_data);
    }
}
#endif
