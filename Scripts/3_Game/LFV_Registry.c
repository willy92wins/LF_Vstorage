// =========================================================
// LF_VStorage -- Registry (v1.0)
//
// Central registry of which containers are virtual-storage
// enabled. Two sources merged at startup:
//   1. VirtualContainers array in settings.json (admin)
//   2. Config property lfvVirtualStorage=1 (modders)
//
// Match is ALWAYS exact: entity.GetType() == classname.
// No IsKindOf. This prevents accidental inclusion of
// child classes the admin didn't intend.
//
// 3_Game layer -- shared between server and client.
// =========================================================

class LFV_Registry
{
    // --- Core sets ---
    protected static ref set<string> s_VirtualContainers;   // positive match
    protected static ref set<string> s_ActionTriggered;     // action-triggered (non-barrel open/close)
    protected static ref set<string> s_NeverVirtualize;     // hardcoded + settings
    protected static ref set<string> s_ItemBlacklist;       // items purged on virtualize
    protected static ref set<string> s_NonContainers;       // negative cache (speedup)

    // --- Components (for extensibility -- Sprint 5) ---
    protected static ref map<string, typename> s_Components;

    // --- Config property cache ---
    protected static ref set<string> s_ConfigChecked;       // classnames already checked via ConfigGetInt

    // -----------------------------------------------------------
    // Init -- called from LFV_Module.OnMissionStart
    // -----------------------------------------------------------
    static void Init(LFV_Settings settings)
    {
        s_VirtualContainers = new set<string>();
        s_ActionTriggered = new set<string>();
        s_NeverVirtualize = new set<string>();
        s_ItemBlacklist = new set<string>();
        s_NonContainers = new set<string>();
        s_Components = new map<string, typename>();
        s_ConfigChecked = new set<string>();

        // Load from settings (Via 1)
        if (settings.m_VirtualContainers)
        {
            for (int i = 0; i < settings.m_VirtualContainers.Count(); i = i + 1)
            {
                string vc = settings.m_VirtualContainers[i];
                if (vc != "" && s_VirtualContainers.Find(vc) == -1)
                {
                    s_VirtualContainers.Insert(vc);
                }
            }
        }

        // ActionTriggeredContainers from settings
        if (settings.m_ActionTriggeredContainers)
        {
            for (int at = 0; at < settings.m_ActionTriggeredContainers.Count(); at = at + 1)
            {
                string atc = settings.m_ActionTriggeredContainers[at];
                if (atc != "" && s_ActionTriggered.Find(atc) == -1)
                {
                    s_ActionTriggered.Insert(atc);
                }
            }
        }

        // NeverVirtualize from settings
        if (settings.m_NeverVirtualize)
        {
            for (int j = 0; j < settings.m_NeverVirtualize.Count(); j = j + 1)
            {
                string nv = settings.m_NeverVirtualize[j];
                if (nv != "" && s_NeverVirtualize.Find(nv) == -1)
                {
                    s_NeverVirtualize.Insert(nv);
                }
            }
        }

        // ItemBlacklist from settings
        if (settings.m_ItemBlacklist)
        {
            for (int k = 0; k < settings.m_ItemBlacklist.Count(); k = k + 1)
            {
                string bl = settings.m_ItemBlacklist[k];
                if (bl != "" && s_ItemBlacklist.Find(bl) == -1)
                {
                    s_ItemBlacklist.Insert(bl);
                }
            }
        }

        string countStr = s_VirtualContainers.Count().ToString();
        string atStr = s_ActionTriggered.Count().ToString();
        string neverStr = s_NeverVirtualize.Count().ToString();
        string blackStr = s_ItemBlacklist.Count().ToString();
        string initMsg = "Registry init -- ";
        initMsg = initMsg + countStr;
        initMsg = initMsg + " containers, ";
        initMsg = initMsg + atStr;
        initMsg = initMsg + " action-triggered, ";
        initMsg = initMsg + neverStr;
        initMsg = initMsg + " never-virtualize, ";
        initMsg = initMsg + blackStr;
        initMsg = initMsg + " blacklisted items";
        LFV_Log.Info(initMsg);
    }

    // -----------------------------------------------------------
    // Safety check -- prevents null reference if queried before Init
    // -----------------------------------------------------------
    static bool IsInitialized()
    {
        return s_VirtualContainers != null;
    }

    // -----------------------------------------------------------
    // Config property check (Via 2) -- lazy, called per classname
    // -----------------------------------------------------------
    static void TryRegisterFromConfig(string classname)
    {
        if (!s_ConfigChecked)
            return;

        if (s_ConfigChecked.Find(classname) > -1)
            return;

        s_ConfigChecked.Insert(classname);

        string cfgPath = "CfgVehicles ";
        cfgPath = cfgPath + classname;
        cfgPath = cfgPath + " lfvVirtualStorage";
        if (GetGame().ConfigIsExisting(cfgPath))
        {
            int val = GetGame().ConfigGetInt(cfgPath);
            if (val == 1)
            {
                if (s_VirtualContainers.Find(classname) == -1)
                {
                    s_VirtualContainers.Insert(classname);
                    string cfgMsg = "Registered from config: ";
                    cfgMsg = cfgMsg + classname;
                    LFV_Log.Info(cfgMsg);
                }
            }
        }
    }

    // -----------------------------------------------------------
    // Query methods -- exact match only
    // -----------------------------------------------------------
    static bool IsVirtualContainer(string classname)
    {
        if (!s_VirtualContainers)
            return false;

        // Fast negative cache
        if (s_NonContainers.Find(classname) > -1)
            return false;

        // Positive set
        if (s_VirtualContainers.Find(classname) > -1)
            return true;

        // Lazy config check (first encounter only)
        TryRegisterFromConfig(classname);
        if (s_VirtualContainers.Find(classname) > -1)
            return true;

        // Cache negative result
        s_NonContainers.Insert(classname);
        return false;
    }

    static bool IsNeverVirtualize(string classname)
    {
        if (!s_NeverVirtualize)
            return false;
        return s_NeverVirtualize.Find(classname) > -1;
    }

    static bool IsBlacklistedItem(string classname)
    {
        if (!s_ItemBlacklist)
            return false;
        return s_ItemBlacklist.Find(classname) > -1;
    }

    static bool IsActionTriggered(string classname)
    {
        if (!s_ActionTriggered)
            return false;
        return s_ActionTriggered.Find(classname) > -1;
    }

    // -----------------------------------------------------------
    // Barrel type detection -- uses IsKindOf (inheritance check)
    // This is the ONE place where inheritance is used, because
    // we need to detect the barrel open/close mechanic regardless
    // of exact classname.
    // -----------------------------------------------------------
    static bool IsBarrelType(EntityAI entity)
    {
        if (!entity)
            return false;
        string kBarrel = "Barrel_ColorBase";
        return entity.IsKindOf(kBarrel);
    }

    // -----------------------------------------------------------
    // Classname existence check (for spawn validation)
    // -----------------------------------------------------------
    protected static ref set<string> s_ClassnameCache;
    protected static ref set<string> s_ClassnameMissing;

    static bool ClassnameExists(string classname)
    {
        if (!s_ClassnameCache)
            s_ClassnameCache = new set<string>();
        if (!s_ClassnameMissing)
            s_ClassnameMissing = new set<string>();

        if (s_ClassnameCache.Find(classname) > -1)
            return true;
        if (s_ClassnameMissing.Find(classname) > -1)
            return false;

        string cfgPath = "CfgVehicles ";
        cfgPath = cfgPath + classname;
        if (GetGame().ConfigIsExisting(cfgPath))
        {
            s_ClassnameCache.Insert(classname);
            return true;
        }

        s_ClassnameMissing.Insert(classname);
        return false;
    }

    // -----------------------------------------------------------
    // Component registration (Sprint 5 -- extensibility)
    // -----------------------------------------------------------
    static void RegisterComponent(string name, typename componentType)
    {
        if (!s_Components)
            return;
        s_Components.Set(name, componentType);
        string compMsg = "Component registered: ";
        compMsg = compMsg + name;
        LFV_Log.Info(compMsg);
    }

    static map<string, typename> GetComponents()
    {
        return s_Components;
    }
}
