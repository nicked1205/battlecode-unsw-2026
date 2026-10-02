// Local parity-test switches (keep all 0 for real games / submission).
#pragma once
inline constexpr int DET_MODE = 0;     // 1: every random draw reads 0.0 (no noise, pings always fire)
inline constexpr int ACTION_LOG = 0;   // 1: append "round id len action [SONARt] best_s" per turn to ACTION_LOG_PATH
inline constexpr const char* ACTION_LOG_PATH = "/tmp/v65cpp_actions.log";
inline constexpr int MILL_DBG = 0;    // 1: per-direction choose() breakdown to /tmp/mill_dbg.log (rounds 5..60, x in 14..23)
