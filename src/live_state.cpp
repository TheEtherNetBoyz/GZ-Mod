// Experimental live state. A refused load writes nothing.
#include "core.hpp"
#include "loading.hpp"
#include "native_pose.hpp"
#include "d/d_com_inf_game.h"
#include "d/d_camera.h"
#include "d/d_lib.h"
#include "d/actor/d_a_alink.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_actor_iter.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"
#include "m_Do/m_Do_dvd_thread.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRFileLoader.h"
#include "JSystem/JUtility/JUTGamePad.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include <dolphin/ar.h>
#include <algorithm>
#include <array>
#include <chrono>
#include "gz_font.hpp"
#include "presentation.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
namespace gz {
bool gzMenuOpen();
bool practiceLoadPending();
ModResult initHostPin();
void hostPinSet(const std::vector<uintptr_t>& blocks);
void resetArchiveCaches();
void hostPinAdopt();
size_t hostPinHeld();
void hostPinRelease();
namespace {
enum class Request { None, Save, Load };

struct StaticRange { const char* anchor; int32_t offset; uint32_t size; const char* check; int32_t checkDelta; };
#include "static_ranges.inc"

struct Block {
 uint8_t* ptr = nullptr; size_t size = 0;
 std::unique_ptr<uint8_t[]> data;
};
struct Run { size_t firstPage, pages, offset; };
struct HeapCopy : Block {
 JKRExpHeap* heap = nullptr; bool scan = true;
 std::vector<Run> runs;
 std::unique_ptr<uint8_t[]> packed; size_t packedSize = 0;
};
struct Snapshot {
 bool valid = false;
 char stage[8] = {};
 int room = -1, layer = -1;
 OSTime osAtSave = 0;
 const void* player = nullptr;
 uint32_t processId = 0;
 std::vector<HeapCopy> heaps;
 std::vector<Block> actors;
 std::unique_ptr<uint8_t[]> statics; size_t staticsSize = 0;
 std::vector<const void*> archives;
 std::vector<std::string> archiveNames;
 struct HostRef { uintptr_t ptr; size_t size; int region; size_t offset; uint64_t owner; size_t ownerDist; };
 std::vector<HostRef> hostRefs;
 const void* currentHeap = nullptr;
 cXyz position, speed;
 csXyz shapeAngle;
 s16 currentAngleY = 0;
 float speedF = 0;
 bool camera = false;
 cXyz eye, center;
};
Snapshot slot;
Request pending = Request::None;
uint32_t framesSinceLoad = 0xFFFFFFFF;

#ifdef _WIN32
HANDLE logFile = INVALID_HANDLE_VALUE;
PVOID crashHandler = nullptr;
thread_local bool probingHostBlock = false;
uint8_t* exeBase() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }
size_t exeImageSize() {
 auto* base = exeBase();
 auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
 return nt->OptionalHeader.SizeOfImage;
}
void writeLog(const char* text) {
 if (logFile == INVALID_HANDLE_VALUE) return;
 DWORD n = 0; WriteFile(logFile, text, DWORD(std::strlen(text)), &n, nullptr);
}
LONG CALLBACK onException(PEXCEPTION_POINTERS info) {
 if (probingHostBlock || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
 char line[256];
 const auto base = reinterpret_cast<uintptr_t>(exeBase());
 std::snprintf(line, sizeof(line), "CRASH rip=%llx rva=%llx addr=%llx framesSinceLoad=%u\n",
  (unsigned long long)info->ContextRecord->Rip, (unsigned long long)(info->ContextRecord->Rip - base),
  (unsigned long long)info->ExceptionRecord->ExceptionInformation[1], framesSinceLoad);
 writeLog(line);
 {
  const CONTEXT& c = *info->ContextRecord;
  const unsigned long long regs[] = {c.Rax, c.Rcx, c.Rdx, c.Rbx, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15};
  const char* names[] = {"rax", "rcx", "rdx", "rbx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 14; ++i) {
   std::snprintf(line, sizeof(line), "  %s=%llx", names[i], regs[i]);
   writeLog(line);
  }
  writeLog("\n");
  for (int i = 0; i < 14; ++i) {
   const auto* p = reinterpret_cast<const unsigned char*>(regs[i]);
   if (regs[i] < 0x10000 || IsBadReadPtr(p, 64)) continue;
   std::snprintf(line, sizeof(line), "  [%s]", names[i]);
   writeLog(line);
   for (int k = 0; k < 64; ++k) { std::snprintf(line, sizeof(line), "%s%02x", k % 8 ? "" : " ", p[k]); writeLog(line); }
   writeLog("\n");
  }
 }
 const auto* sp = reinterpret_cast<const uintptr_t*>(info->ContextRecord->Rsp);
 const size_t imageSize = exeImageSize();
 int found = 0;
 for (int i = 0; i < 512 && found < 24; ++i) {
  uintptr_t v = 0;
  if (IsBadReadPtr(sp + i, sizeof(v))) break;
  v = sp[i];
  if (v >= base && v < base + imageSize) {
   std::snprintf(line, sizeof(line), "  stack+%d rva=%llx\n", i * 8, (unsigned long long)(v - base));
   writeLog(line); ++found;
  }
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
#else
uint8_t* exeBase() { return nullptr; }
void writeLog(const char*) {}
#endif

struct Notice { std::string text; int ttl = 0; uint32_t rgba = 0; };
std::array<Notice, 8> notices;
void showNotice(const std::string& text, bool refused = false) {
 for (size_t i = notices.size() - 1; i > 0; --i) notices[i] = notices[i - 1];
 notices[0] = {text, 120, refused ? 0xff606000u : 0xffffff00u};
}

template<class... A> void say(const char* fmt, A... a) {
 char buf[320]; std::snprintf(buf, sizeof(buf), fmt, a...);
 notify(buf);
 writeLog(buf); writeLog("\n");
}

using LockFn = void (*)(void*);
LockFn lockEnter = nullptr, lockLeave = nullptr;

struct AudioLock {
 char storage = 0;
 AudioLock() { if (lockEnter && lockLeave) lockEnter(this); }
 ~AudioLock() { if (lockEnter && lockLeave) lockLeave(this); }
};

void resolveAudioLock() {
 const char* enter[] = {"JASCriticalSection::JASCriticalSection", "??0JASCriticalSection@@QEAA@XZ", "_ZN18JASCriticalSectionC1Ev"};
 const char* leave[] = {"JASCriticalSection::~JASCriticalSection", "??1JASCriticalSection@@QEAA@XZ", "_ZN18JASCriticalSectionD1Ev"};
 auto find = [](const char* const* names, LockFn& out) {
  for (int i = 0; i < 3 && !out; ++i) {
   void* addr = nullptr; HookSymbolFlags flags{};
   if (svc_hook->resolve(mod_ctx, names[i], &addr, &flags) == MOD_OK && addr) out = reinterpret_cast<LockFn>(addr);
  }
 };
 find(enter, lockEnter); find(leave, lockLeave);
 if (!lockEnter || !lockLeave) { lockEnter = lockLeave = nullptr; notify("[live-state] audio lock not found; copies are not synchronised with the audio thread"); }
}


struct ResolvedRange { uint8_t* ptr; uint32_t size; };
std::vector<ResolvedRange> resolvedStatics;
bool staticsResolved = false;
const char* staticsFailure = nullptr;

// Looks each range up by symbol name on the running build and checks the distance between its two anchors.
const char* resolveStatics() {
#ifdef _WIN32
 if (staticsResolved) return staticsFailure;
 staticsResolved = true;
 const uint8_t* lo = exeBase();
 const uint8_t* hi = lo + exeImageSize();
 size_t total = 0, skipped = 0, mismatched = 0;
 for (auto& r : kStaticRanges) {
  total += r.size;
  void* a = nullptr; HookSymbolFlags flags{};
  if (svc_hook->resolve(mod_ctx, r.anchor, &a, &flags) != MOD_OK || !a) { skipped += r.size; continue; }
  if (r.check) {
   void* b = nullptr;
   if (svc_hook->resolve(mod_ctx, r.check, &b, &flags) != MOD_OK || !b ||
       int64_t(reinterpret_cast<uintptr_t>(b)) - int64_t(reinterpret_cast<uintptr_t>(a)) != r.checkDelta) { skipped += r.size; ++mismatched; continue; }
  }
  auto* start = static_cast<uint8_t*>(a) + r.offset;
  if (start < lo || start + r.size > hi) { skipped += r.size; continue; }
  resolvedStatics.push_back({start, r.size});
 }
 say("[live-state] statics: %zu of %zu ranges resolved, %zu bytes skipped (%zu with a changed layout)",
     resolvedStatics.size(), sizeof(kStaticRanges) / sizeof(kStaticRanges[0]), skipped, mismatched);
 if (skipped * 10 > total) staticsFailure = "too many game statics could not be found on this Dusklight build";
 return staticsFailure;
#else
 return "live state is Windows-only";
#endif
}

constexpr size_t kHeapObjectSize = ALIGN_NEXT(sizeof(JKRExpHeap), 0x10);

bool collectHeaps(std::vector<HeapCopy>& out, const char** why) {
 JKRExpHeap* heaps[] = {mDoExt_getGameHeap(), mDoExt_getArchiveHeap(), mDoExt_getJ2dHeap(), static_cast<JKRExpHeap*>(JKRHeap::getSystemHeap())};
 for (auto* h : heaps) {
  if (!h) { *why = "a required heap is missing"; return false; }
  auto* objectStart = static_cast<uint8_t*>(h->getStartAddr()) - kHeapObjectSize;
  if (reinterpret_cast<uint8_t*>(h) != objectStart) { *why = "unexpected heap layout"; return false; }
  HeapCopy c; c.heap = h; c.ptr = objectStart;
  c.size = static_cast<uint8_t*>(h->getEndAddr()) - objectStart;
  out.push_back(std::move(c));
 }
 if (auto* aram = static_cast<uint8_t*>(ARGetStorageAddress())) {
  if (ARGetSize()) {
   HeapCopy c; c.ptr = aram; c.size = ARGetSize(); c.scan = false;
   out.push_back(std::move(c));
  }
 }
 for (size_t i = 0; i < out.size(); ++i)
  for (size_t j = i + 1; j < out.size(); ++j)
   if (out[i].ptr < out[j].ptr + out[j].size && out[j].ptr < out[i].ptr + out[i].size) { *why = "heap ranges overlap"; return false; }
 return true;
}

struct ActorList { std::vector<uint8_t*> ptrs; };
int gatherActor(void* actor, void* data) { static_cast<ActorList*>(data)->ptrs.push_back(static_cast<uint8_t*>(actor)); return 1; }

bool collectActors(const std::vector<HeapCopy>& heaps, std::vector<Block>& out, const char** why) {
 ActorList list; fopAcIt_Executor(gatherActor, &list);
 for (auto* p : list.ptrs) {
  bool inside = false;
  for (auto& h : heaps) if (p >= h.ptr && p < h.ptr + h.size) inside = true;
  if (inside) continue;
  auto* block = JKRExpHeap::CMemBlock::getBlock(p);
  if (!block->isValid() || block->getContent() != p || block->getSize() == 0 || block->getSize() > 0x100000) { *why = "an actor is not at the start of a heap block"; return false; }
  Block b; b.ptr = p; b.size = block->getSize();
  out.push_back(std::move(b));
 }
 std::sort(out.begin(), out.end(), [](const Block& a, const Block& b) { return a.ptr < b.ptr; });
 return true;
}

struct LoaderPeek : JKRFileLoader {
 static const char* name(JKRFileLoader* l) { return l->*(&LoaderPeek::mVolumeName); }
};

void collectArchiveNames(std::vector<std::string>& out) {
 auto& list = JKRFileLoader::getVolumeList();
 for (auto* l = list.getFirst(); l; l = l->getNext()) {
  const char* n = LoaderPeek::name(l->getObject());
  out.push_back(n ? n : "");
 }
}

void collectArchives(std::vector<const void*>& out) {
 auto& list = JKRFileLoader::getVolumeList();
 for (auto* l = list.getFirst(); l; l = l->getNext()) out.push_back(l->getObject());
}

#ifdef _WIN32
bool hostBlockSize(uintptr_t p, size_t& size) {
 bool ok = false;
 probingHostBlock = true;
 __try {
  HANDLE heap = GetProcessHeap();
  if (HeapValidate(heap, 0, reinterpret_cast<void*>(p))) {
   const SIZE_T n = HeapSize(heap, 0, reinterpret_cast<void*>(p));
   if (n != SIZE_T(-1)) { size = n; ok = true; }
  }
 } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
 probingHostBlock = false;
 return ok;
}

// Host (CRT heap) blocks that the saved memory points at. Game objects own some of these and free them
// when destroyed, so a restore is only safe while they are still live.
void collectHostRefs(const uint8_t* data, size_t bytes, std::unordered_set<uint64_t>& seen,
                     const std::vector<HeapCopy>& heaps, std::vector<Snapshot::HostRef>& out, int region) {
 MEMORY_BASIC_INFORMATION mbi{}; uint64_t lo = 1, hi = 0; bool regionOk = false;
 const uint64_t exeLo = uint64_t(uintptr_t(exeBase())), exeHi = exeLo + exeImageSize();
 for (size_t i = 0; i + 8 <= bytes && out.size() < 400000; i += 8) {
  uint64_t v; std::memcpy(&v, data + i, 8);
  if (v < 0x10000 || v > 0x00007FFFFFFFFFFFull || (v & 0xF) || (v >= exeLo && v < exeHi)) continue;
  bool inHeap = false;
  for (auto& h : heaps) if (v >= uint64_t(uintptr_t(h.ptr)) && v < uint64_t(uintptr_t(h.ptr)) + h.size) { inHeap = true; break; }
  if (inHeap || !seen.insert(v).second) continue;
  if (v < lo || v >= hi) {
   regionOk = VirtualQuery(reinterpret_cast<void*>(v), &mbi, sizeof(mbi)) != 0;
   if (regionOk) { lo = uint64_t(uintptr_t(mbi.BaseAddress)); hi = lo + mbi.RegionSize; }
  }
  if (!regionOk || mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || v < lo + 16) continue;
  size_t size = 0;
  if (hostBlockSize(uintptr_t(v), size)) {
   uint64_t owner = 0; size_t dist = 0;
   for (size_t k = 1; k <= 256 && k * 8 <= i; ++k) {
    uint64_t w; std::memcpy(&w, data + i - k * 8, 8);
    if (w >= exeLo && w < exeHi) { owner = w - exeLo; dist = k * 8; break; }
   }
   out.push_back({uintptr_t(v), size, region, i, owner, dist});
  }
 }
}

void reportHostRefs(const Snapshot& snap) {
 size_t bad = 0;
 for (auto& r : snap.hostRefs) {
  size_t size = 0;
  const bool live = hostBlockSize(r.ptr, size);
  if (live && size == r.size) continue;
  ++bad;
  if (bad > 6) continue;
  say("[live-state]   host block %p: saved size %zu, now %s%zu; held in region %d +0x%zx; nearest vtable rva %llx at -0x%zx",
      (void*)r.ptr, r.size, live ? "" : "gone/", live ? size : size_t(0), r.region, r.offset, (unsigned long long)r.owner, r.ownerDist);
 }
 if (bad) say("[live-state] warning: %zu of %zu saved host blocks changed; loading anyway", bad, snap.hostRefs.size());
}
#else
void collectHostRefs(const uint8_t*, size_t, std::unordered_set<uint64_t>&, const std::vector<HeapCopy>&, std::vector<Snapshot::HostRef>&, int) {}
void reportHostRefs(const Snapshot&) {}
#endif

size_t staticsTotal() { size_t n = 0; for (auto& r : resolvedStatics) n += r.size; return n; }

const char* refuse() {
 if (!playable()) return "not in gameplay";
 if (sceneLoading() || practiceLoadPending()) return "loading";
 if (!daAlink_getAlinkActorClass()) return "no player";
 if (fopAcM_SearchByName(fpcNm_TITLE_e)) return "title screen";
 if (mDoDvdThd::l_param.getFirstCommand()) return "loading";

 auto& res = g_dComIfG_gameInfo.mResControl;
 for (auto& i : res.mObjectInfo) if (i.getDMCommand()) return "loading";
 for (auto& i : res.mStageInfo) if (i.getDMCommand()) return "loading";
 return nullptr;
}

constexpr size_t kPage = 4096;

bool allZero(const uint8_t* p, size_t len) {
 size_t i = 0;
 for (; i + 8 <= len; i += 8) { uint64_t v; std::memcpy(&v, p + i, 8); if (v) return false; }
 for (; i < len; ++i) if (p[i]) return false;
 return true;
}

// Keep only the pages that are not entirely zero, grouped into runs of consecutive pages.
bool pack(HeapCopy& h) {
 const size_t pages = (h.size + kPage - 1) / kPage;
 size_t stored = 0;
 h.runs.clear();
 for (size_t p = 0; p < pages; ++p) {
  const size_t len = std::min(kPage, h.size - p * kPage);
  if (allZero(h.data.get() + p * kPage, len)) continue;
  if (!h.runs.empty() && h.runs.back().firstPage + h.runs.back().pages == p) ++h.runs.back().pages;
  else h.runs.push_back({p, 1, stored});
  stored += len;
 }
 h.packed.reset(new (std::nothrow) uint8_t[stored ? stored : 1]);
 if (!h.packed) return false;
 h.packedSize = stored;
 for (auto& r : h.runs) {
  const size_t start = r.firstPage * kPage, end = std::min(h.size, (r.firstPage + r.pages) * kPage);
  std::memcpy(h.packed.get() + r.offset, h.data.get() + start, end - start);
 }
 return true;
}

// The packed form must reproduce the original exactly: every gap zero, every run identical.
bool verifyPacked(const HeapCopy& h) {
 size_t pos = 0;
 for (auto& r : h.runs) {
  const size_t start = r.firstPage * kPage, end = std::min(h.size, (r.firstPage + r.pages) * kPage);
  if (!allZero(h.data.get() + pos, start - pos)) return false;
  if (std::memcmp(h.data.get() + start, h.packed.get() + r.offset, end - start) != 0) return false;
  pos = end;
 }
 return allZero(h.data.get() + pos, h.size - pos);
}

void restoreRegion(const HeapCopy& h) {
 size_t pos = 0;
 for (auto& r : h.runs) {
  const size_t start = r.firstPage * kPage, end = std::min(h.size, (r.firstPage + r.pages) * kPage);
  if (start > pos) std::memset(h.ptr + pos, 0, start - pos);
  std::memcpy(h.ptr + start, h.packed.get() + r.offset, end - start);
  pos = end;
 }
 if (h.size > pos) std::memset(h.ptr + pos, 0, h.size - pos);
}

const char* capture(Snapshot& s) {
 if (auto* why = resolveStatics()) return why;
 const char* why = nullptr;
 if (!collectHeaps(s.heaps, &why) || !collectActors(s.heaps, s.actors, &why)) return why;
 for (auto& h : s.heaps) {
  h.data.reset(new (std::nothrow) uint8_t[h.size]);
  if (!h.data) return "out of memory";
 }
 for (auto& a : s.actors) {
  a.data.reset(new (std::nothrow) uint8_t[a.size]);
  if (!a.data) return "out of memory";
 }
 s.staticsSize = staticsTotal();
 s.statics.reset(new (std::nothrow) uint8_t[s.staticsSize]);
 if (!s.statics) return "out of memory";
 collectArchives(s.archives);
 collectArchiveNames(s.archiveNames);
 s.currentHeap = JKRHeap::getCurrentHeap();
 {
  AudioLock audioLock;
  uint8_t* cursor = s.statics.get();
  for (auto& r : resolvedStatics) { std::memcpy(cursor, r.ptr, r.size); cursor += r.size; }
  for (auto& h : s.heaps) std::memcpy(h.data.get(), h.ptr, h.size);
  for (auto& a : s.actors) std::memcpy(a.data.get(), a.ptr, a.size);
 }
 {
  std::unordered_set<uint64_t> seen;
  collectHostRefs(s.statics.get(), s.staticsSize, seen, s.heaps, s.hostRefs, -1);
  for (size_t i = 0; i < s.heaps.size(); ++i) if (s.heaps[i].scan) collectHostRefs(s.heaps[i].data.get(), s.heaps[i].size, seen, s.heaps, s.hostRefs, int(i));
 }
 for (auto& h : s.heaps) {
  if (!pack(h)) return "out of memory";
  if (!verifyPacked(h)) return "copy mismatch";
  h.data.reset();
 }
 return nullptr;
}

const char* validate() {
 if (!slot.valid) return "no state saved";
 if (auto* why = refuse()) return why;
 if (auto* why = resolveStatics()) return why;
 std::vector<HeapCopy> heaps; const char* why = nullptr;
 if (!collectHeaps(heaps, &why)) return why;
 for (size_t i = 0; i < heaps.size(); ++i)
  if (heaps[i].ptr != slot.heaps[i].ptr || heaps[i].size != slot.heaps[i].size || heaps[i].heap != slot.heaps[i].heap) return "memory layout changed";
 reportHostRefs(slot);
 return nullptr;
}

bool doSave() {
 if (auto* why = refuse()) { say("[live-state] save refused: %s", why); showNotice(std::string("State refused: ") + why, true); return false; }
 auto* link = daAlink_getAlinkActorClass();
 const auto t0 = std::chrono::steady_clock::now();
 if (auto* sound = Z2GetSoundMgr()) sound->stopSync();
 Snapshot s;
 if (auto* why = capture(s)) { say("[live-state] save refused: %s", why); showNotice(std::string("State refused: ") + why, true); return false; }
 std::strncpy(s.stage, dComIfGp_getStartStageName(), sizeof(s.stage) - 1);
 s.room = dComIfGp_roomControl_getStayNo();
 s.layer = dComIfGp_getStartStageLayer();
 s.osAtSave = OSGetTime();
 s.player = link;
 s.processId = fopAcM_GetID(link);
 s.position = link->current.pos; s.speed = link->speed;
 s.shapeAngle = link->shape_angle; s.currentAngleY = link->current.angle.y;
 s.speedF = link->speedF;
 if (auto* cam = dCam_getBody()) { s.camera = true; s.eye = cam->iEye(); s.center = cam->iCenter(); }
 s.valid = true;
 slot = std::move(s);
 {
  std::vector<uintptr_t> blocks;
  for (auto& r : slot.hostRefs) blocks.push_back(r.ptr);
  hostPinSet(blocks);
 }
 const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
 say("[live-state] saved stage=%s room=%d heaps=%zu actors=%zu archives=%zu statics=%zu hostBlocks=%zu held=%zu bytes in %lld us",
     slot.stage, slot.room, slot.heaps.size(), slot.actors.size(), slot.archives.size(), slot.staticsSize, slot.hostRefs.size(), hostPinHeld(), (long long)us);
 size_t total = 0, stored = 0;
 for (auto& h : slot.heaps) { total += h.size; stored += h.packedSize; }
 say("[live-state]   kept %zu of %zu MiB (empty pages skipped)", stored >> 20, total >> 20);
 showNotice("State saved");
 return true;
}

bool doLoad() {
 if (auto* why = validate()) { say("[live-state] load refused: %s", why); showNotice(std::string("State refused: ") + why, true); return false; }
 auto* link = daAlink_getAlinkActorClass();
 const auto t0 = std::chrono::steady_clock::now();
 if (auto* sound = Z2GetSoundMgr()) sound->stopSync();
 {
  AudioLock audioLock;
  const uint8_t* cursor = slot.statics.get();
  for (auto& r : resolvedStatics) { std::memcpy(r.ptr, cursor, r.size); cursor += r.size; }
  for (auto& h : slot.heaps) restoreRegion(h);
  for (auto& a : slot.actors) std::memcpy(a.ptr, a.data.get(), a.size);
 }
 resetArchiveCaches();
 hostPinAdopt();
 JKRHeap::setCurrentHeap(const_cast<JKRHeap*>(static_cast<const JKRHeap*>(slot.currentHeap)));
 GXInvalidateTexAll();
 for (int port = 0; port < 4; ++port) JUTGamePad::CRumble::stopMotorHard(port);
 mDoGph_gInf_c::offFade();
 mDoGph_gInf_c::setFadeRate(0.0f);
 if (auto* fader = mDoGph_gInf_c::getFader()) fader->setStatus(JUTFader::Wait, 0);
 const OSTime now = OSGetTime();
 if (dLib_time_c::m_timeStopped) dLib_time_c::m_stopTime = now;
 else dLib_time_c::m_diffTime += now - slot.osAtSave;
 placeNativeLink(slot.position, slot.shapeAngle.y);
 link->shape_angle = slot.shapeAngle;
 link->current.angle.y = slot.currentAngleY;
 link->speed = slot.speed;
 link->speedF = slot.speedF;
 if (slot.camera) placeNativeCamera(slot.center, slot.eye);
 framesSinceLoad = 0;
 const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
 say("[live-state] loaded in %lld us", (long long)us);
 showNotice("State loaded");
 return true;
}

}

bool liveStateNoticeVisible() { for (auto& n : notices) if (n.ttl) return true; return false; }
void drawLiveStateNotices() {
 if (gzMenuOpen()) return;
 const auto p = spritePosition(Fifo);
 for (size_t i = 0; i < notices.size(); ++i) {
  const auto& n = notices[i];
  if (!n.ttl) continue;
  const uint32_t alpha = n.ttl < 30 ? uint32_t(n.ttl * 8.5f) : 255;
  const float y = p.y - float(i) * 14;
  if (on("drop_shadows")) drawGzTextPlain(n.text, p.x + 1, y + 1, alpha, 17);
  drawGzTextPlain(n.text, p.x, y, n.rgba | alpha, 17);
 }
}
void liveStateSave() { pending = Request::Save; }
void liveStateLoad() { pending = Request::Load; }

void liveStateTick() {
 if (framesSinceLoad != 0xFFFFFFFF) ++framesSinceLoad;
 if (!gzMenuOpen()) for (auto& n : notices) if (n.ttl > 0) --n.ttl;
 const auto request = pending; pending = Request::None;
 if (request == Request::Save) doSave();
 else if (request == Request::Load) doLoad();
}

void initLiveState() {
#ifdef _WIN32
 const char* dir = nullptr;
 if (svc_host && svc_host->data_dir(mod_ctx, &dir) == MOD_OK && dir) {
  const std::string path = std::string(dir) + "/live_state.log";
  logFile = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
 }
 crashHandler = AddVectoredExceptionHandler(1, onException);
#endif
 resolveAudioLock();
 toggle("live_state", "Tools", "Save States", "L + R + Right to save, R + Right to load.", true);
 if (initHostPin() != MOD_OK) notify("[live-state] texture hook unavailable; host blocks are not kept alive");
}

void shutdownLiveState() {
#ifdef _WIN32
 if (crashHandler) RemoveVectoredExceptionHandler(crashHandler);
 crashHandler = nullptr;
 if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
 logFile = INVALID_HANDLE_VALUE;
#endif
 hostPinRelease();
 slot = Snapshot{};
}
}
