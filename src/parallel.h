#pragma once
// 軽量スレッドプール (OpenMP の代替: MSVC の OpenMP は vcomp140.dll が必要で単一 exe にできないため)
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(unsigned n = 0) {
        if (n == 0) n = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned i = 1; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    int threadCount() const { return (int)workers_.size() + 1; }

    // fn(chunkIndex, begin, end) を chunks 個に分割して並列実行する (呼び出しスレッドも参加)
    void parallelFor(int n, int chunks, const std::function<void(int, int, int)>& fn) {
        if (n <= 0) return;
        chunks = std::max(1, std::min(chunks, n));
        const int per = (n + chunks - 1) / chunks;
        if (chunks == 1 || workers_.empty()) {
            for (int c = 0; c < chunks; ++c) {
                int b = c * per, e = std::min(n, b + per);
                if (b < e) fn(c, b, e);
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            n_ = n;
            chunks_ = chunks;
            per_ = per;
            next_.store(0);
            ++generation_;
        }
        cv_.notify_all();
        runChunks();
        std::unique_lock<std::mutex> lk(m_);
        fn_ = nullptr;  // 以後に起床したワーカーは何もしない
        doneCv_.wait(lk, [&] { return active_ == 0; });
    }

private:
    void runChunks() { runChunks(fn_, n_, chunks_, per_); }
    void runChunks(const std::function<void(int, int, int)>* fn, int n, int chunks, int per) {
        for (;;) {
            int c = next_.fetch_add(1);
            if (c >= chunks) break;
            int b = c * per, e = std::min(n, b + per);
            if (b < e) (*fn)(c, b, e);
        }
    }
    void workerLoop() {
        unsigned long long seen = 0;
        for (;;) {
            const std::function<void(int, int, int)>* fn;
            int n, chunks, per;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return quit_ || generation_ != seen; });
                if (quit_) return;
                seen = generation_;
                if (!fn_) continue;
                fn = fn_; n = n_; chunks = chunks_; per = per_;
                ++active_;
            }
            runChunks(fn, n, chunks, per);
            {
                std::lock_guard<std::mutex> lk(m_);
                if (--active_ == 0) doneCv_.notify_one();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    bool quit_ = false;
    unsigned long long generation_ = 0;
    int active_ = 0;
    const std::function<void(int, int, int)>* fn_ = nullptr;
    int n_ = 0, chunks_ = 0, per_ = 1;
    std::atomic<int> next_{0};
};
