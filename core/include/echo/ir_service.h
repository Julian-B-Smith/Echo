// echo/ir_service.h — the render service: the one IR renderer (ir_render.h) on
// a worker thread, behind a small cache (ROADMAP T-031; M4 gates 3 and 4).
//
// WHO CALLS WHAT. The UI (the editor) calls request(), pause(), resume(),
// version() and read(). The worker thread renders. The AUDIO THREAD NEVER
// CALLS ANY OF IT: request() and read() take a lock, which the audio thread
// may not (RS-7.7), and the renderer allocates nothing only because start()
// preallocated everything. The live engine and this service share nothing:
// the patch reaches request() as plain values copied from the host's
// parameter model, never read from the live instance (rack-slot contract §8).
//
// HEAD FIRST (gate 4: a redraw within 100 ms of a change). A whole window can
// take 200-300 ms at 192 kHz (ROADMAP M4, found 5), so a render publishes its
// first headLength samples as frame 1, then extends the SAME render by chunk
// samples per frame (IrRenderer's resumable form: no re-rendering of the
// head). A newer request cancels the render in flight at its next chunk.
//
// THE CACHE. `slots` complete renders, keyed by irHash (every input the render
// reads), least recently used out. A hit is published at once with no render.
// A render cancelled or paused part-way is never a hit (its tail is missing).
//
// FED ONLY WHILE OPEN (B432: 0 % CPU with the editor closed). pause() stops
// the worker at its next chunk and holds new requests until resume(), which
// renders the newest one.
//
// MEMORY. Everything is allocated in start(): one prepared renderer (the
// clone's lines, ~18 MiB at 192 kHz) and 2 x slots x maxLength doubles.
#pragma once
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "echo/ir_render.h"
#include "echo/plants.h"

namespace echo {

struct IrServiceConfig {
  double sampleRate = 48000.0;
  std::int64_t maxLength = 0;   // the longest window a request may ask for, in samples (slots are sized to it)
  std::int64_t headLength = 0;  // frame 1 covers this many samples (T-031: 2 s)
  std::int64_t chunk = 0;       // each later frame extends the render by this many
  int slots = 8;                // complete renders kept
};

// What the UI draws. Valid only inside IrService::read's callback.
struct IrFrame {
  std::uint64_t hash = 0;       // irHash of the request drawn
  std::uint64_t serial = 0;     // the request() call it answers
  std::int64_t ready = 0;       // samples [0, ready) are final
  std::int64_t length = 0;      // the request's length
  bool complete = false;
  IrInfo info;                  // valid once ready > 0
  std::span<const double> L, R; // [0, ready)
};

class IrService {
 public:
  IrService() = default;
  IrService(const IrService&) = delete;
  IrService& operator=(const IrService&) = delete;
  ~IrService() { stop(); }

  // Allocates everything and starts the worker. False on a nonsensical config
  // or a rate the engine would not keep.
  bool start(const IrServiceConfig& c) {
    stop();
    if (c.maxLength <= 0 || c.headLength <= 0 || c.chunk <= 0 || c.slots < 1) return false;
    cfg_ = c;
    ren_.prepare(c.sampleRate);
    if (!ren_.preparedFor(c.sampleRate)) return false;
    if (plant::irRenderInline) inline_.prepare(c.sampleRate);
    slots_.assign(static_cast<std::size_t>(c.slots), Slot{});
    for (auto& s : slots_) {
      s.L.assign(static_cast<std::size_t>(c.maxLength), 0.0);
      s.R.assign(static_cast<std::size_t>(c.maxLength), 0.0);
    }
    current_ = -1;
    quit_ = paused_ = pending_ = false;
    serial_ = version_ = tick_ = 0;
    stats_ = Stats{};
    worker_ = std::thread([this] { run(); });
    return true;
  }

  void stop() {
    if (!worker_.joinable()) return;
    {
      std::lock_guard<std::mutex> lk(m_);
      quit_ = true;
    }
    cv_.notify_all();
    worker_.join();
  }

  // UI thread. Queues the request (the newest wins) and returns its serial at
  // once, or 0 if refused (another rate, longer than maxLength, empty).
  std::uint64_t request(const IrRequest& r) {
    if (r.sampleRate != cfg_.sampleRate || r.length <= 0 || r.length > cfg_.maxLength) return 0;
    std::unique_lock<std::mutex> lk(m_);
    const std::uint64_t serial = ++serial_;
    if (plant::irRenderInline) {  // the plant: the caller's thread renders the whole window, here
      Slot& s = slots_[static_cast<std::size_t>(pickSlot())];
      claim(s, r, serial);
      if (inline_.start(r)) inline_.renderNext(s.L, s.R, r.length);
      noteThread();
      stats_.firstReady = inline_.position();
      s.ready = inline_.position();
      s.info = inline_.info();
      s.complete = true;
      ++version_;
      return serial;
    }
    pendingReq_ = r;
    pendingSerial_ = serial;
    pending_ = true;
    lk.unlock();
    cv_.notify_all();
    return serial;
  }

  // UI thread: the editor closed / opened.
  void pause() {
    std::lock_guard<std::mutex> lk(m_);
    paused_ = true;
  }
  void resume() {
    {
      std::lock_guard<std::mutex> lk(m_);
      paused_ = false;
      // A render the pause cut short is not drawable to its end: render it again.
      if (!pending_ && current_ >= 0 && !slots_[static_cast<std::size_t>(current_)].complete && haveLast_) {
        pendingReq_ = lastReq_;
        pendingSerial_ = slots_[static_cast<std::size_t>(current_)].serial;
        pending_ = true;
      }
    }
    cv_.notify_all();
  }

  // UI thread: bumps on every published change. Poll it per display frame and
  // read() only when it moved.
  std::uint64_t version() const {
    std::lock_guard<std::mutex> lk(m_);
    return version_;
  }

  // UI thread: calls f(const IrFrame&) with the current frame, under the lock.
  // Returns false (f not called) if nothing is drawable yet.
  template <class F>
  bool read(F&& f) const {
    std::lock_guard<std::mutex> lk(m_);
    if (current_ < 0) return false;
    const Slot& s = slots_[static_cast<std::size_t>(current_)];
    IrFrame fr;
    fr.hash = s.hash;
    fr.serial = s.serial;
    fr.ready = s.ready;
    fr.length = s.length;
    fr.complete = s.complete;
    fr.info = s.info;
    fr.L = std::span<const double>(s.L.data(), static_cast<std::size_t>(s.ready));
    fr.R = std::span<const double>(s.R.data(), static_cast<std::size_t>(s.ready));
    f(fr);
    return true;
  }

  // ---- reads for the tests (gate 3: who rendered; the cache; cancellation) --
  struct Stats {
    std::uint64_t renders = 0, chunks = 0, hits = 0, cancelled = 0;
    std::int64_t firstReady = 0;  // `ready` at the first publication of the latest render (frame 1's extent)
    std::array<std::thread::id, 4> threads{};  // distinct threads seen rendering (the first 4)
    int nThreads = 0;
  };
  Stats stats() const {
    std::lock_guard<std::mutex> lk(m_);
    return stats_;
  }
  std::thread::id workerId() const { return worker_.get_id(); }

 private:
  struct Slot {
    std::vector<double> L, R;
    std::uint64_t hash = 0, serial = 0, used = 0;
    std::int64_t ready = 0, length = 0;
    bool complete = false, valid = false;
    IrInfo info;
  };

  // Under m_. The least recently used slot that is not on screen.
  int pickSlot() {
    int best = -1;
    for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
      if (i == current_ && slots_.size() > 1) continue;
      if (best < 0 || !slots_[static_cast<std::size_t>(i)].valid ||
          (slots_[static_cast<std::size_t>(best)].valid && slots_[static_cast<std::size_t>(i)].used < slots_[static_cast<std::size_t>(best)].used))
        best = i;
      if (!slots_[static_cast<std::size_t>(best)].valid) break;
    }
    return best;
  }
  // Under m_. Makes s the frame on screen for request r.
  void claim(Slot& s, const IrRequest& r, std::uint64_t serial) {
    s.hash = irHash(r);
    s.serial = serial;
    s.used = ++tick_;
    s.ready = 0;
    s.length = r.length;
    s.complete = false;
    s.valid = true;
    s.info = IrInfo{};
    current_ = static_cast<int>(&s - slots_.data());
    lastReq_ = r;
    haveLast_ = true;
    ++stats_.renders;
    ++version_;
  }
  // Under m_. Records the thread doing the rendering (gate 3's evidence).
  void noteThread() {
    const auto id = std::this_thread::get_id();
    for (int i = 0; i < stats_.nThreads; ++i)
      if (stats_.threads[static_cast<std::size_t>(i)] == id) return;
    if (stats_.nThreads < static_cast<int>(stats_.threads.size())) stats_.threads[static_cast<std::size_t>(stats_.nThreads++)] = id;
  }

  void run() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
      cv_.wait(lk, [this] { return quit_ || (pending_ && (!paused_ || plant::irRendersWhilePaused)); });
      if (quit_) return;
      const IrRequest r = pendingReq_;
      const std::uint64_t serial = pendingSerial_;
      pending_ = false;
      const std::uint64_t h = irHash(r);
      // A hit: a complete render of exactly this request.
      int hit = -1;
      if (!plant::irCacheIgnored)
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
          const Slot& s = slots_[static_cast<std::size_t>(i)];
          if (s.valid && s.complete && s.hash == h) hit = i;
        }
      if (hit >= 0) {
        Slot& s = slots_[static_cast<std::size_t>(hit)];
        s.serial = serial;
        s.used = ++tick_;
        current_ = hit;
        lastReq_ = r;
        haveLast_ = true;
        ++stats_.hits;
        ++version_;
        continue;
      }
      Slot& s = slots_[static_cast<std::size_t>(pickSlot())];
      claim(s, r, serial);
      lk.unlock();
      const bool ok = ren_.start(r);
      lk.lock();
      if (!ok) {  // refused by the engine (a value out of range): nothing to draw
        s.valid = false;
        current_ = -1;
        ++version_;
        continue;
      }
      std::int64_t target = plant::irWholeTailFirst ? r.length : std::min(cfg_.headLength, r.length);
      for (;;) {
        lk.unlock();
        // The slot's samples past `ready` belong to the worker alone: read()
        // shows only [0, ready), and `ready` moves under the lock.
        ren_.renderNext(s.L, s.R, target);
        lk.lock();
        noteThread();
        if (s.ready == 0) stats_.firstReady = ren_.position();
        ++stats_.chunks;
        s.ready = ren_.position();
        s.info = ren_.info();
        s.complete = ren_.done();
        ++version_;
        if (s.complete || quit_) break;
        if ((pending_ && !plant::irNoCancel) || (paused_ && !plant::irRendersWhilePaused)) {
          ++stats_.cancelled;  // cut short: never a cache hit (complete stays false)
          break;
        }
        target = std::min(target + cfg_.chunk, r.length);
      }
      if (quit_) return;
    }
  }

  IrServiceConfig cfg_;
  IrRenderer ren_, inline_;
  std::vector<Slot> slots_;
  int current_ = -1;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::thread worker_;
  bool quit_ = false, paused_ = false, pending_ = false, haveLast_ = false;
  IrRequest pendingReq_, lastReq_;
  std::uint64_t pendingSerial_ = 0, serial_ = 0, version_ = 0, tick_ = 0;
  Stats stats_;
};

}  // namespace echo
