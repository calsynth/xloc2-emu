#include "CoreLoader.h"

#include "EmuEngine.h"  // stateDir()

#if JUCE_LINUX || JUCE_MAC
#include <dlfcn.h>
#endif

namespace {
juce::String lastDlError() {
#if JUCE_LINUX || JUCE_MAC
  if (const char* e = dlerror()) return juce::String::fromUTF8(e);
#endif
  return {};
}
}  // namespace

const char* CoreLoader::coreExtension() {
#if JUCE_MAC
  return ".dylib";
#else
  return ".so";
#endif
}

juce::File CoreLoader::coresDir() {
  return EmuEngine::stateDir().getChildFile("cores");
}

juce::File CoreLoader::findDefaultCore() {
  const auto dir = coresDir();

  // 1. cores/active — symlink (juce::File follows it on use) or plain file
  auto active = dir.getChildFile("active");
  if (active.exists()) {
    auto target = active.isSymbolicLink() ? active.getLinkedTarget() : active;
    if (target.existsAsFile()) return target;
  }

  // 2. cores/active.txt — first non-empty line names the core
  auto activeTxt = dir.getChildFile("active.txt");
  if (activeTxt.existsAsFile()) {
    auto name = activeTxt.loadFileAsString()
                    .upToFirstOccurrenceOf("\n", false, false)
                    .trim();
    if (name.isNotEmpty()) {
      auto f = juce::File::isAbsolutePath(name) ? juce::File(name)
                                                : dir.getChildFile(name);
      if (f.existsAsFile()) return f;
    }
  }

  // 3. core bundled with the app
  const juce::String coreName = juce::String("phz_core") + coreExtension();
#if JUCE_MAC
  auto bundled = juce::File::getSpecialLocation(
                     juce::File::currentApplicationFile)
                     .getChildFile("Contents/Resources")
                     .getChildFile(coreName);
#else
  auto bundled = juce::File::getSpecialLocation(
                     juce::File::currentExecutableFile)
                     .getSiblingFile(coreName);
#endif
  if (bundled.existsAsFile()) return bundled;

  return {};
}

bool CoreLoader::load(const juce::File& coreFile, juce::String& error) {
  unload();

  if (!coreFile.existsAsFile()) {
    error = "Core not found: " + coreFile.getFullPathName();
    return false;
  }

  // Copy to a unique temp file so each load maps a fresh image (and the
  // original can be rebuilt/overwritten while loaded).
  auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("xloc2-core-" +
                                juce::Uuid().toString().substring(0, 8) +
                                coreExtension());
  if (!coreFile.copyFileTo(temp)) {
    error = "Cannot copy core to temp file: " + temp.getFullPathName();
    return false;
  }

  if (!lib_.open(temp.getFullPathName())) {
    error = "dlopen failed: " + coreFile.getFileName();
    const auto dle = lastDlError();
    if (dle.isNotEmpty()) error << "\n" << dle;
    temp.deleteFile();
    return false;
  }

  auto getApi = (Xloc2CoreGetApiFn)lib_.getFunction(XLOC2_CORE_ENTRY_POINT);
  if (getApi == nullptr) {
    error = coreFile.getFileName() +
            " is not an XLOC2 core (missing " XLOC2_CORE_ENTRY_POINT ")";
    lib_.close();
    temp.deleteFile();
    return false;
  }

  const auto* api = getApi();
  if (api == nullptr || api->api_version != XLOC2_CORE_API_VERSION) {
    error = "Core API version mismatch: " + coreFile.getFileName() + " has v" +
            juce::String(api != nullptr ? (int)api->api_version : 0) +
            ", this app needs v" + juce::String((int)XLOC2_CORE_API_VERSION) +
            ". Rebuild the core against the current app.";
    lib_.close();
    temp.deleteFile();
    return false;
  }

  api_ = api;
  sourceFile_ = coreFile;
  tempCopy_ = temp;
  sourceTime_ = coreFile.getLastModificationTime();
  return true;
}

void CoreLoader::unload() {
  if (api_ == nullptr && tempCopy_ == juce::File()) return;
  api_ = nullptr;
  // Best-effort: with -fno-gnu-unique and RTLD_LOCAL the module really
  // unloads on Linux; if the runtime keeps it resident anyway, the next
  // load still gets a fresh image (unique temp copy).
  lib_.close();
  tempCopy_.deleteFile();
  tempCopy_ = juce::File();
  sourceFile_ = juce::File();
  sourceTime_ = juce::Time();
}
