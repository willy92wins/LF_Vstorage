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
    protected static ref set<string> s_VirtualContainers;   // informational (or whitelist when strict)
    protected static ref set<string> s_ActionTriggered;     // action-triggered (legacy flag source)
    protected static ref set<string> s_NeverVirtualize;     // PRIMARY filter (blacklist-first)
    protected static ref set<string> s_ItemBlacklist;       // items purged on virtualize
    protected static ref set<string> s_NonContainers;       // negative cache (speedup)

    // --- Filter mode ---
    protected static bool s_StrictMode;

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
        s_ConfigChecked = new set<string>();
        s_StrictMode = settings.m_StrictMode;

        // Load from settings (Via 1)
        if (settings.m_VirtualContainers)
        {
            for (int i = 0; i < settings.m_VirtualContainers.Count(); i++)
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
            for (int at = 0; at < settings.m_ActionTriggeredContainers.Count(); at++)
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
            for (int j = 0; j < settings.m_NeverVirtualize.Count(); j++)
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
            for (int k = 0; k < settings.m_ItemBlacklist.Count(); k++)
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

        // Blacklist primary -- always wins
        if (s_NeverVirtualize && s_NeverVirtualize.Find(classname) > -1)
            return false;

        // Strict mode: only whitelisted classes are virtual
        if (s_StrictMode)
        {
            if (s_NonContainers.Find(classname) > -1)
                return false;

            if (s_VirtualContainers.Find(classname) > -1)
                return true;

            TryRegisterFromConfig(classname);
            if (s_VirtualContainers.Find(classname) > -1)
                return true;

            s_NonContainers.Insert(classname);
            return false;
        }

        // Default mode: anything not blacklisted is virtual.
        // Hookable containers reach this path implicitly (action hook
        // / LFV_API is the de-facto whitelist).
        return true;
    }

    // -----------------------------------------------------------
    // Strict-whitelist query, independent of s_StrictMode.
    //
    // Use this in code paths that run for EVERY ItemBase-derived
    // entity (persistence hooks at modded class ItemBase level,
    // EEInit registration). The default-true branch of
    // IsVirtualContainer was designed for action hooks where the
    // caller already established "this is a container" intent; it
    // must NOT leak into persistence / registration paths, where
    // returning true for codelocks, weapons, magazines, BBP
    // structures, etc. (which is every non-blacklisted classname)
    // causes:
    //   - m_ContainerStates flooded with non-container entities
    //   - LFV persistence bytes injected between vanilla ItemBase
    //     bytes and child-class bytes (CombinationLock.m_Combination,
    //     Magazine state, BBP.m_IsHologram, etc.), causing read
    //     misalignment and data loss.
    //
    // This function mirrors strict mode: only explicit whitelist
    // entries (settings.json m_VirtualContainers, or config property
    // lfvVirtualStorage=1) return true. Blacklist still wins.
    // -----------------------------------------------------------
    static bool IsExplicitVirtualContainer(string classname)
    {
        if (!s_VirtualContainers)
            return false;

        // Blacklist primary -- always wins
        if (s_NeverVirtualize && s_NeverVirtualize.Find(classname) > -1)
            return false;

        // Negative cache (populated by prior failed lookups)
        if (s_NonContainers && s_NonContainers.Find(classname) > -1)
            return false;

        // Explicit whitelist match
        if (s_VirtualContainers.Find(classname) > -1)
            return true;

        // Config property fallback (lfvVirtualStorage=1 in CfgVehicles)
        TryRegisterFromConfig(classname);
        if (s_VirtualContainers.Find(classname) > -1)
            return true;

        // Cache miss so we don't re-query the config every call
        if (s_NonContainers)
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

        // DayZ splits configs by category: weapons live in CfgWeapons,
        // magazines/ammo in CfgMagazines, everything else in CfgVehicles.
        // LocationCreateEntity resolves all three, so our existence check
        // must too -- otherwise we wrongly discard entire subtrees (e.g.
        // a holster's pistol) during restore.
        string vehiclePath = "CfgVehicles " + classname;
        if (GetGame().ConfigIsExisting(vehiclePath))
        {
            s_ClassnameCache.Insert(classname);
            return true;
        }

        string weaponPath = "CfgWeapons " + classname;
        if (GetGame().ConfigIsExisting(weaponPath))
        {
            s_ClassnameCache.Insert(classname);
            return true;
        }

        string magazinePath = "CfgMagazines " + classname;
        if (GetGame().ConfigIsExisting(magazinePath))
        {
            s_ClassnameCache.Insert(classname);
            return true;
        }

        s_ClassnameMissing.Insert(classname);
        return false;
    }
}
