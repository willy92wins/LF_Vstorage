// =========================================================
// LF_VStorage -- Stats (v1.0)
//
// Server-side counters and timing metrics. All static fields
// for global access. LogSummary() writes to RPT on shutdown.
// =========================================================

class LFV_Stats
{
    // --- Counters ---
    static int s_TotalVirtualizedItems;
    static int s_TotalContainers;
    static int s_OperationsThisRestart;
    static int s_FailedRestores;
    static int s_SkippedItems;
    static int s_PurgedItems;
    static int s_OrphanFilesCleaned;

    // --- Timing ---
    static float s_TotalVirtualizeMs;
    static float s_TotalRestoreMs;
    static int   s_VirtualizeCount;
    static int   s_RestoreCount;
    static float s_MaxVirtualizeMs;
    static float s_MaxRestoreMs;

    static void Reset()
    {
        s_TotalVirtualizedItems = 0;
        s_TotalContainers = 0;
        s_OperationsThisRestart = 0;
        s_FailedRestores = 0;
        s_SkippedItems = 0;
        s_PurgedItems = 0;
        s_OrphanFilesCleaned = 0;

        s_TotalVirtualizeMs = 0;
        s_TotalRestoreMs = 0;
        s_VirtualizeCount = 0;
        s_RestoreCount = 0;
        s_MaxVirtualizeMs = 0;
        s_MaxRestoreMs = 0;
    }

    static float GetAvgVirtualizeMs()
    {
        if (s_VirtualizeCount == 0) return 0;
        return s_TotalVirtualizeMs / s_VirtualizeCount;
    }

    static float GetAvgRestoreMs()
    {
        if (s_RestoreCount == 0) return 0;
        return s_TotalRestoreMs / s_RestoreCount;
    }

    static void RecordVirtualize(float durationMs)
    {
        s_VirtualizeCount++;
        s_TotalVirtualizeMs = s_TotalVirtualizeMs + durationMs;
        s_OperationsThisRestart++;
        if (durationMs > s_MaxVirtualizeMs)
            s_MaxVirtualizeMs = durationMs;
    }

    static void RecordRestore(float durationMs)
    {
        s_RestoreCount++;
        s_TotalRestoreMs = s_TotalRestoreMs + durationMs;
        s_OperationsThisRestart++;
        if (durationMs > s_MaxRestoreMs)
            s_MaxRestoreMs = durationMs;
    }

    // Return a compact summary string for admin commands
    static string GetSummary()
    {
        string r = "Ops: ";
        r = r + s_OperationsThisRestart.ToString();
        r = r + " | Items virt: ";
        r = r + s_TotalVirtualizedItems.ToString();
        r = r + " | Virt avg: ";
        r = r + GetAvgVirtualizeMs().ToString();
        r = r + "ms | Rest avg: ";
        r = r + GetAvgRestoreMs().ToString();
        r = r + "ms | Failed: ";
        r = r + s_FailedRestores.ToString();
        r = r + " | Skipped: ";
        r = r + s_SkippedItems.ToString();
        r = r + " | Purged: ";
        r = r + s_PurgedItems.ToString();
        return r;
    }

    static void LogSummary()
    {
        string statsHeader = "=== SESSION STATS ===";
        LFV_Log.Info(statsHeader);

        string line1 = "Containers tracked: ";
        line1 = line1 + s_TotalContainers.ToString();
        LFV_Log.Info(line1);

        string line2 = "Items virtualized: ";
        line2 = line2 + s_TotalVirtualizedItems.ToString();
        LFV_Log.Info(line2);

        string line3 = "Operations: ";
        line3 = line3 + s_OperationsThisRestart.ToString();
        LFV_Log.Info(line3);

        string line4 = "Failed restores: ";
        line4 = line4 + s_FailedRestores.ToString();
        LFV_Log.Info(line4);

        string line5 = "Skipped items: ";
        line5 = line5 + s_SkippedItems.ToString();
        LFV_Log.Info(line5);

        string line6 = "Purged items: ";
        line6 = line6 + s_PurgedItems.ToString();
        LFV_Log.Info(line6);

        string line7 = "Orphans cleaned: ";
        line7 = line7 + s_OrphanFilesCleaned.ToString();
        LFV_Log.Info(line7);

        string line8 = "Avg virtualize: ";
        line8 = line8 + GetAvgVirtualizeMs().ToString();
        line8 = line8 + "ms (max ";
        line8 = line8 + s_MaxVirtualizeMs.ToString();
        line8 = line8 + "ms)";
        LFV_Log.Info(line8);

        string line9 = "Avg restore: ";
        line9 = line9 + GetAvgRestoreMs().ToString();
        line9 = line9 + "ms (max ";
        line9 = line9 + s_MaxRestoreMs.ToString();
        line9 = line9 + "ms)";
        LFV_Log.Info(line9);

        string statsFooter = "=== END STATS ===";
        LFV_Log.Info(statsFooter);
    }
}
