// =========================================================
// LF_VStorage -- ContainerFile model (v1.0)
//
// Represents the full container data structure that maps
// to a single .lfv file on disk. Header + item tree.
// All item properties are captured via explicit fields (FORMAT 3+).
// =========================================================

class LFV_ContainerFile
{
    int         m_MagicNumber;       // 0x4C4656
    int         m_FormatVersion;
    string      m_StorageId;
    string      m_ContainerClass;
    int         m_State;
    int         m_Timestamp;
    vector      m_Position;
    string      m_OwnerUID;
    string      m_PersistentId;      // v2: "b1_b2_b3_b4" for container lookup after move
    string      m_Manifest;
    int         m_TotalItemCount;
    ref array<ref LFV_ItemRecord> m_Items;   // direct children

    void LFV_ContainerFile()
    {
        m_MagicNumber = LFV_Magic.LFV_FILE;
        m_FormatVersion = LFV_Version.FORMAT;
        m_StorageId = "";
        m_ContainerClass = "";
        m_State = LFV_State.IDLE;
        m_Timestamp = 0;
        m_Position = vector.Zero;
        m_OwnerUID = "";
        m_PersistentId = "";
        m_Manifest = "";
        m_TotalItemCount = 0;
        m_Items = new array<ref LFV_ItemRecord>();
    }
}

// -----------------------------------------------------------
// JSON-safe mirror of LFV_ContainerFile + LFV_ItemRecord.
//
// JsonFileLoader hard-crashes on native engine types
// (ItemBase, EntityAI, etc.) even when protected/null. The crash
// is in C++ reflection, not Enforce Script -- it sees the FIELD TYPE
// and tries to serialize it, causing a native segfault.
//
// This class contains ONLY JSON-safe types (int, float, bool, string,
// ref array). No vector (serialized as 3 floats), no ItemBase.
// Built from LFV_ContainerFile via static FromContainerFile().
// -----------------------------------------------------------
class LFV_ContainerFileJson
{
    string m_StorageId;
    string m_ContainerClass;
    int m_State;
    int m_Timestamp;
    float m_PosX;
    float m_PosY;
    float m_PosZ;
    string m_OwnerUID;
    string m_PersistentId;
    string m_Manifest;
    int m_TotalItemCount;
    ref array<ref LFV_ItemRecordJson> m_Items;

    void LFV_ContainerFileJson()
    {
        m_Items = new array<ref LFV_ItemRecordJson>();
    }

    static LFV_ContainerFileJson FromContainerFile(LFV_ContainerFile src)
    {
        LFV_ContainerFileJson dst = new LFV_ContainerFileJson();
        dst.m_StorageId = src.m_StorageId;
        dst.m_ContainerClass = src.m_ContainerClass;
        dst.m_State = src.m_State;
        dst.m_Timestamp = src.m_Timestamp;
        dst.m_PosX = src.m_Position[0];
        dst.m_PosY = src.m_Position[1];
        dst.m_PosZ = src.m_Position[2];
        dst.m_OwnerUID = src.m_OwnerUID;
        dst.m_PersistentId = src.m_PersistentId;
        dst.m_Manifest = src.m_Manifest;
        dst.m_TotalItemCount = src.m_TotalItemCount;
        ConvertItems(src.m_Items, dst.m_Items);
        return dst;
    }

    static void ConvertItems(array<ref LFV_ItemRecord> srcItems, array<ref LFV_ItemRecordJson> dstItems)
    {
        if (!srcItems) return;
        for (int i = 0; i < srcItems.Count(); i++)
        {
            LFV_ItemRecord src = srcItems[i];
            if (!src) continue;
            dstItems.Insert(LFV_ItemRecordJson.FromItemRecord(src));
        }
    }
}

class LFV_ItemRecordJson
{
    // All fields from LFV_ItemRecord EXCEPT m_ItemRef (native engine type)
    string m_Classname;
    int m_InvType;
    int m_Row;
    int m_Col;
    int m_Idx;
    int m_SlotId;
    bool m_Flipped;
    float m_Health;
    int m_Quantity;
    int m_LiquidType;
    int m_FoodStage;
    int m_AmmoCount;
    float m_Temperature;
    float m_Wetness;
    float m_Energy;
    bool m_HasEnergy;
    int m_Agents;
    int m_Combination;
    int m_Cleanness;
    int m_ChamberCount;
    int m_InternalMagCount;
    int m_MagCount;
    ref array<ref LFV_ItemRecordJson> m_Attachments;
    ref array<ref LFV_ItemRecordJson> m_Cargo;

    void LFV_ItemRecordJson()
    {
        m_Attachments = new array<ref LFV_ItemRecordJson>();
        m_Cargo = new array<ref LFV_ItemRecordJson>();
    }

    static LFV_ItemRecordJson FromItemRecord(LFV_ItemRecord src)
    {
        LFV_ItemRecordJson dst = new LFV_ItemRecordJson();
        dst.m_Classname = src.m_Classname;
        dst.m_InvType = src.m_InvType;
        dst.m_Row = src.m_Row;
        dst.m_Col = src.m_Col;
        dst.m_Idx = src.m_Idx;
        dst.m_SlotId = src.m_SlotId;
        dst.m_Flipped = src.m_Flipped;
        dst.m_Health = src.m_Health;
        dst.m_Quantity = src.m_Quantity;
        dst.m_LiquidType = src.m_LiquidType;
        dst.m_FoodStage = src.m_FoodStage;
        dst.m_AmmoCount = src.m_AmmoCount;
        dst.m_Temperature = src.m_Temperature;
        dst.m_Wetness = src.m_Wetness;
        dst.m_Energy = src.m_Energy;
        dst.m_HasEnergy = src.m_HasEnergy;
        dst.m_Agents = src.m_Agents;
        dst.m_Combination = src.m_Combination;
        dst.m_Cleanness = src.m_Cleanness;
        // Cartridge counts (simplified for admin inspection)
        dst.m_ChamberCount = src.m_ChamberRounds.Count();
        dst.m_InternalMagCount = src.m_InternalMagRounds.Count();
        dst.m_MagCount = src.m_MagRounds.Count();
        // Recursive children
        LFV_ContainerFileJson.ConvertItems(src.m_Attachments, dst.m_Attachments);
        LFV_ContainerFileJson.ConvertItems(src.m_Cargo, dst.m_Cargo);
        return dst;
    }
}

// -----------------------------------------------------------
// Container state tracking -- runtime only, not serialized
// One per tracked container in LFV_Module.m_ContainerStates
// -----------------------------------------------------------
class LFV_ContainerState
{
    string  m_StorageId;
    int     m_State;
    bool    m_HasItems;
    bool    m_IsProcessing;
    string  m_Manifest;
    int     m_LastActivity;
    bool    m_IsLFVBarrel;      // true if LFV_Barrel_Base, false if vanilla
    bool    m_IsActionTriggered; // true if in ActionTriggeredContainers list
    // Phase 5.5: set true while VirtualizeQueue is actively writing .lfv.
    // Blocks OnEntityDestroyed from purging state mid-serialization, which
    // would leave a corrupt blob + lose the in-flight DropQueue.
    bool    m_IsVirtualizing;
    ref map<string, int> m_PlayerActionTimestamps;  // rate limiting per-player

    void LFV_ContainerState()
    {
        m_StorageId = "";
        m_State = LFV_State.IDLE;
        m_HasItems = false;
        m_IsProcessing = false;
        m_Manifest = "";
        m_LastActivity = 0;
        m_IsLFVBarrel = false;
        m_IsActionTriggered = false;
        m_IsVirtualizing = false;
        m_PlayerActionTimestamps = new map<string, int>();
    }
}
