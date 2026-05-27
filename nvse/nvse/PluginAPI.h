#pragma once

// xNVSE Plugin API Header
// Based on xNVSE 6.4.2 source from https://github.com/xNVSE/NVSE
// Struct layouts MUST match exactly or function calls will crash

#include <cstdint>

// Forward declarations - we don't need full definitions
struct CommandInfo;
struct ExpressionEvaluatorUtils;
class TESObjectREFR;
class Actor;
class PlayerCharacter;

// Type aliases to match xNVSE
using UInt32 = uint32_t;
using PluginHandle = UInt32;

// Command return type enum
enum CommandReturnType : UInt32 {
    kRetnType_Default = 0,
    kRetnType_Form,
    kRetnType_String,
    kRetnType_Array,
    kRetnType_ArrayIndex,
    kRetnType_Ambiguous
};

// Plugin info structure passed to NVSEPlugin_Query
struct PluginInfo {
    enum { kInfoVersion = 1 };

    UInt32 infoVersion;
    const char* name;
    UInt32 version;
};

// Main NVSE interface passed to plugin functions
// CRITICAL: Field order must match xNVSE exactly!
struct NVSEInterface {
    UInt32 nvseVersion;
    UInt32 runtimeVersion;
    UInt32 editorVersion;
    UInt32 isEditor;

    // Command registration (we don't use these but they must be here)
    bool (*RegisterCommand)(CommandInfo* info);
    void (*SetOpcodeBase)(UInt32 opcode);

    // Query for additional interfaces
    void* (*QueryInterface)(UInt32 id);

    // Plugin handle for this plugin
    PluginHandle (*GetPluginHandle)(void);

    // Typed command registration
    bool (*RegisterTypedCommand)(CommandInfo* info, CommandReturnType retnType);

    // Get runtime directory
    const char* (*GetRuntimeDirectory)(void);

    // Is nogore version
    UInt32 isNogore;

    // Expression evaluator utils
    void (*InitExpressionEvaluatorUtils)(ExpressionEvaluatorUtils* utils);

    // Versioned command registration
    bool (*RegisterTypedCommandVersion)(CommandInfo* info, CommandReturnType retnType,
                                        UInt32 requiredPluginVersion);
};

// Interface IDs for QueryInterface - order matters!
enum {
    kInterface_Serialization = 0,
    kInterface_Console,
    kInterface_Messaging,
    kInterface_CommandTable,
    kInterface_StringVar,
    kInterface_ArrayVar,
    kInterface_Script,
    kInterface_Data,
    kInterface_EventManager,
    kInterface_Logging,
    kInterface_PlayerControls,
    kInterface_Max
};

// Messaging interface for game lifecycle events
struct NVSEMessagingInterface {
    enum {
        kMessage_PostLoad = 0,          // 0
        kMessage_ExitGame,              // 1
        kMessage_ExitToMainMenu,        // 2
        kMessage_LoadGame,              // 3
        kMessage_SaveGame,              // 4
        kMessage_ScriptPrecompile,      // 5
        kMessage_PreLoadGame,           // 6
        kMessage_ExitGame_Console,      // 7
        kMessage_PostLoadGame,          // 8
        kMessage_PostPostLoad,          // 9
        kMessage_RuntimeScriptError,    // 10
        kMessage_DeleteGame,            // 11
        kMessage_RenameGame,            // 12
        kMessage_RenameNewGame,         // 13
        kMessage_NewGame,               // 14
        kMessage_DeleteGameName,        // 15
        kMessage_RenameGameName,        // 16
        kMessage_RenameNewGameName,     // 17
        kMessage_DeferredInit,          // 18
        kMessage_ClearScriptDataCache,  // 19
        kMessage_MainGameLoop,          // 20
        kMessage_ScriptCompile,         // 21
        kMessage_EventListDestroyed,    // 22
        kMessage_PostQueryPlugins,      // 23
        kMessage_Max
    };

    struct Message {
        const char* sender;
        UInt32 type;
        UInt32 dataLen;
        void* data;
    };

    typedef void (*EventCallback)(Message* msg);

    enum { kVersion = 4 };

    UInt32 version;
    bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
    bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen, const char* receiver);
};

// Console interface for running script commands
struct NVSEConsoleInterface {
    enum { kVersion = 3 };

    UInt32 version;
    bool (*RunScriptLine)(const char* buf, TESObjectREFR* object);
    bool (*RunScriptLine2)(const char* buf, TESObjectREFR* callingRefr, bool bSuppressConsoleOutput);
};

// NOTE: Console_Print is NOT available through NVSE interfaces
// The g_ConsolePrint function pointer is defined in plugin.h as nullptr
