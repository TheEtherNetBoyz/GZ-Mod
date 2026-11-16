// Keeps host blocks that a saved live state points at alive until the state is replaced.
#include "core.hpp"
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define private public
#define protected public
#include "d/d_com_inf_game.h"
#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRFileLoader.h"
#undef protected
#undef private
namespace gz {
namespace {
std::mutex lock;
std::unordered_set<uintptr_t> protectedBlocks;

struct Held { void* ptr; int kind; };
std::vector<Held> held;

#ifdef __APPLE__
DEFINE_HOOK_SYMBOL("_ZN10J3DTextureD1Ev", void(J3DTexture*), TextureDtorHook);
#else
DEFINE_HOOK_SYMBOL("J3DTexture::~J3DTexture", void(J3DTexture*), TextureDtorHook);
#endif

void release(const Held& h) {
 switch (h.kind) {
 case 0: delete[] static_cast<TGXTexObj*>(h.ptr); break;
 case 1: delete[] static_cast<GXTlutObj*>(h.ptr); break;
 default: delete[] static_cast<u8**>(h.ptr); break;
 }
}

template<class T> void pin(T*& p, int kind) {
 if (!p || !protectedBlocks.count(reinterpret_cast<uintptr_t>(p))) return;
 held.push_back({p, kind});
 p = nullptr;
}

HookAction onTextureDtor(ModContext*, void* args, void*, void*) {
 std::lock_guard<std::mutex> guard(lock);
 if (protectedBlocks.empty()) return HOOK_CONTINUE;
 auto* t = mods::arg<J3DTexture*>(args, 0);
 if (!t) return HOOK_CONTINUE;
 pin(t->mpTexObj, 0);
 pin(t->mpTlutObj, 1);
 pin(t->mpImgDataPtr, 2);
 pin(t->mpTlutDataPtr, 2);
 return HOOK_CONTINUE;
}
}

ModResult initHostPin() {
 return guardedPre<TextureDtorHook>(onTextureDtor);
}

void hostPinSet(const std::vector<uintptr_t>& blocks) {
 std::lock_guard<std::mutex> guard(lock);
 protectedBlocks.clear();
 protectedBlocks.insert(blocks.begin(), blocks.end());
 std::vector<Held> keep;
 for (auto& h : held) {
  if (protectedBlocks.count(reinterpret_cast<uintptr_t>(h.ptr))) keep.push_back(h);
  else release(h);
 }
 held.swap(keep);
}

// Archives keep lazily built path caches in host containers. They rebuild on demand, so after a restore
// give every mounted archive fresh empty ones instead of the saved internals (which may have been freed).
// Containers that hold real overlay resources are left alone.
void resetArchiveCaches() {
 auto& list = JKRFileLoader::getVolumeList();
 for (auto* l = list.getFirst(); l; l = l->getNext()) {
  auto* loader = l->getObject();
  if (!loader || loader->getVolumeType() != 'RARC') continue;
  auto* a = static_cast<JKRArchive*>(loader);
  if (!a->mArcOverlayResources.empty()) continue;
  new (&a->mArcOverlayResources) std::unordered_map<void*, JKRArchive::ArcOverlayResource>();
  new (&a->mActiveArcOverlayResources) std::unordered_map<u32, void*>();
  new (&a->mIdxToPathMap) std::unordered_map<u32, std::string>();
  new (&a->mArcOverlaysPath) std::string();
  a->mArcOverlaysPathResolved = false;
 }
}

// After a restore the revived objects own these blocks again.
void hostPinAdopt() {
 std::lock_guard<std::mutex> guard(lock);
 held.clear();
}

size_t hostPinHeld() {
 std::lock_guard<std::mutex> guard(lock);
 return held.size();
}

void hostPinRelease() {
 std::lock_guard<std::mutex> guard(lock);
 protectedBlocks.clear();
 for (auto& h : held) release(h);
 held.clear();
}
}
