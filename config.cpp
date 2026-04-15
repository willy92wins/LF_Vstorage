class CfgPatches
{
    class LF_VStorage
    {
        units[] = {
            "LFV_Barrel_Standard",
            "LFV_Barrel_Large"
        };
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] = {
            "DZ_Data",
            "DZ_Scripts",
            "DZ_Gear_Containers",
            "JM_CF_Scripts"
        };
    };
};

class CfgMods
{
    class LF_VStorage
    {
        dir = "LF_VStorage";
        picture = "";
        action = "";
        hideName = 0;
        hidePicture = 1;
        name = "LF_VStorage";
        credits = "Guillermo / LF_PowerGrid";
        author = "Guillermo";
        authorID = "";
        version = "1.1.0";
        extra = 0;
        type = "mod";

        dependencies[] = {"World"};

        class defs
        {
            class gameScriptModule
            {
                value = "";
                files[] = {
                    "LF_VStorage/Scripts/3_Game"
                };
            };
            class worldScriptModule
            {
                value = "";
                files[] = {
                    "LF_VStorage/Scripts/4_World"
                };
            };
            class missionScriptModule
            {
                value = "";
                files[] = {
                    "LF_VStorage/Scripts/5_Mission"
                };
            };
        };
    };
};

class CfgVehicles
{
    class Barrel_ColorBase;

    // -------------------------------------------------------
    // Base class — standard cargo size (vanilla barrel)
    // -------------------------------------------------------
    class LFV_Barrel_Base : Barrel_ColorBase
    {
        scope = 0;
        lfvVirtualStorage = 1;
        color = "Blue";
        // Disable liquid system inherited from Barrel_ColorBase
        liquidContainerType = 0;
        varQuantityInit = 0;
        varQuantityMin = 0;
        varQuantityMax = 0;
        varQuantityDestroyOnMin = 0;
        hiddenSelectionsTextures[] = {
            "\dz\gear\containers\data\barrel_blue_co.paa"
        };
        hologramMaterial = "barrel";
        hologramMaterialPath = "dz\gear\containers\data";
        class DamageSystem
        {
            class GlobalHealth
            {
                class Health
                {
                    hitpoints = 1300;
                    RefTexsMats[] = {
                        "dz\gear\containers\data\barrel_green.rvmat"
                    };
                    healthLevels[] = {
                        { 1,    { "dz\gear\containers\data\barrel_blue.rvmat" } },
                        { 0.7,  { "dz\gear\containers\data\barrel_blue.rvmat" } },
                        { 0.5,  { "dz\gear\containers\data\barrel_blue_damage.rvmat" } },
                        { 0.3,  { "dz\gear\containers\data\barrel_blue_damage.rvmat" } },
                        { 0,    { "dz\gear\containers\data\barrel_blue_destruct.rvmat" } }
                    };
                };
            };
            class GlobalArmor
            {
                class FragGrenade
                {
                    class Health
                    {
                        damage = 8;
                    };
                    class Blood
                    {
                        damage = 8;
                    };
                    class Shock
                    {
                        damage = 8;
                    };
                };
            };
        };
    };

    // -------------------------------------------------------
    // Base class — large cargo (1000 slots)
    // -------------------------------------------------------
    class LFV_Barrel_Base_1000 : LFV_Barrel_Base
    {
        scope = 0;
        itemsCargoSize[] = {10, 100};
    };

    // -------------------------------------------------------
    // Spawnable variants (scope=2)
    // -------------------------------------------------------
    class LFV_Barrel_Standard : LFV_Barrel_Base
    {
        scope = 2;
        displayName = "$STR_LFV_Barrel_Standard_Name";
        descriptionShort = "$STR_LFV_Barrel_Standard_Desc";
    };

    class LFV_Barrel_Large : LFV_Barrel_Base_1000
    {
        scope = 2;
        displayName = "$STR_LFV_Barrel_Large_Name";
        descriptionShort = "$STR_LFV_Barrel_Large_Desc";
    };
};
