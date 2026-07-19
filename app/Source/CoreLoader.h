// CoreLoader — loads/unloads a firmware core module (phz_core.so/.dylib)
// and resolves its versioned C API.
//
// Load strategy: the module file is first copied to a unique temp file and
// THAT copy is dlopen'd. This guarantees every load maps a genuinely fresh
// image even if a previous instance could not be fully unloaded (dlclose is
// best-effort for C++ libraries), and lets the user overwrite the original
// file while it is "in use" (the basis of auto-reload).
#pragma once

#include <juce_core/juce_core.h>

#include "../../core/xloc2_core_api.h"

class CoreLoader {
 public:
  CoreLoader() = default;
  ~CoreLoader() { unload(); }

  // Loads `coreFile` (via a temp copy) and resolves the API. On failure the
  // loader is left empty and `error` describes what went wrong (including an
  // api_version mismatch).
  bool load(const juce::File& coreFile, juce::String& error);

  // Best-effort dlclose + temp copy cleanup. See header comment: statics of
  // the module may survive in memory; a subsequent load is fresh regardless.
  void unload();

  bool isLoaded() const { return api_ != nullptr; }
  const Xloc2CoreApi* api() const { return api_; }
  const juce::File& loadedFile() const { return sourceFile_; }
  juce::Time loadedFileTime() const { return sourceTime_; }

  // Platform extension for core modules (".so" / ".dylib").
  static const char* coreExtension();
  // <stateDir>/cores — user-managed folder of switchable cores.
  static juce::File coresDir();
  // Default core search order:
  //   1. <coresDir>/active            (symlink or file)
  //   2. <coresDir>/active.txt        (text file naming a core, relative to
  //                                    coresDir or absolute)
  //   3. core bundled with the app    (macOS: Contents/Resources/,
  //                                    elsewhere: next to the executable)
  // Returns a non-existent File if nothing was found.
  static juce::File findDefaultCore();

 private:
  juce::DynamicLibrary lib_;
  juce::File sourceFile_;   // the file the user selected
  juce::File tempCopy_;     // the unique copy actually dlopen'd
  juce::Time sourceTime_;   // sourceFile_ mtime at load (for auto-reload)
  const Xloc2CoreApi* api_ = nullptr;

  JUCE_DECLARE_NON_COPYABLE(CoreLoader)
};
