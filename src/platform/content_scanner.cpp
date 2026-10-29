#include "platform/content_scanner.hpp"

#include "platform/directory_walker.hpp"
#include "platform/mapped_file.hpp"
#include "platform/win.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <utility>

namespace instaseek::platform {

namespace {

constexpr std::uint64_t kChunkBytes = 32ull << 20;
constexpr std::size_t kSniffBytes = 64 * 1024;
constexpr std::size_t kMaxQueuedTasks = 4096;

long long steadyMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct FileJob {
    ContentFile info;
    std::unique_ptr<MappedFile> mapped;
    core::DataExtent extent;
    std::vector<core::ChunkRange> chunks;
    std::mutex mutex;
    std::vector<std::optional<core::ChunkOutcome>> outcomes;
    std::size_t next_to_publish = 0;
    std::uint64_t newlines_published = 0;
    std::optional<std::uint32_t> session_file;
    bool failed = false;
};

struct Task {
    std::shared_ptr<FileJob> job;
    std::size_t chunk = 0;
    bool open_file = false;
    std::wstring path;
    std::uint64_t size = 0;
};

struct ChunkContext {
    const std::uint8_t* data = nullptr;
    core::ByteWindow window;
    core::ChunkRange chunk;
    core::DataExtent extent;
    const core::TextMatcher* matcher = nullptr;
    std::size_t max_hits = 0;
    const std::atomic<bool>* stop = nullptr;
    core::ChunkOutcome* outcome = nullptr;
};

void runChunkContext(void* raw) {
    auto* context = static_cast<ChunkContext*>(raw);
    *context->outcome = core::searchChunk(context->data, context->window, context->chunk, context->extent,
                                          *context->matcher, context->max_hits, context->stop);
}

class SearchRun {
public:
    SearchRun(const ContentRequest& request, const core::TextMatcher& matcher, ContentSession& session,
              const std::atomic<bool>& cancel, bool explicit_file)
        : request_(request), matcher_(matcher), session_(session), cancel_(cancel), explicit_file_(explicit_file) {}

    void start(unsigned threads) {
        for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    void enqueueFile(std::wstring path, std::uint64_t size) {
        std::unique_lock lock(mutex_);
        space_.wait(lock, [this] { return queue_.size() < kMaxQueuedTasks || stopping(); });
        if (stopping()) return;
        Task task;
        task.open_file = true;
        task.path = std::move(path);
        task.size = size;
        queue_.push_back(std::move(task));
        session_.files_total++;
        session_.bytes_total += size;
        work_.notify_one();
    }

    void finishProducing() {
        {
            std::lock_guard lock(mutex_);
            producing_ = false;
        }
        work_.notify_all();
    }

    void join() {
        for (auto& worker : workers_) worker.join();
    }

    bool stopping() const {
        if (cancel_.load(std::memory_order_relaxed)) stop_ = true;
        return stop_.load(std::memory_order_relaxed);
    }

private:
    void workerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock lock(mutex_);
                work_.wait(lock, [this] { return !queue_.empty() || !producing_ || stopping(); });
                if (stopping()) {
                    queue_.clear();
                    space_.notify_all();
                    return;
                }
                if (queue_.empty()) {
                    if (!producing_ && active_ == 0) {
                        work_.notify_all();
                        return;
                    }
                    work_.wait(lock, [this] { return !queue_.empty() || active_ == 0 || stopping(); });
                    if (queue_.empty()) continue;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
                ++active_;
                space_.notify_one();
            }
            if (task.open_file) openFile(task);
            else searchChunk(task.job, task.chunk);
            {
                std::lock_guard lock(mutex_);
                --active_;
            }
            work_.notify_all();
        }
    }

    void openFile(const Task& task) {
        auto job = std::make_shared<FileJob>();
        job->info.path = task.path;
        job->info.display = toUtf8(withoutExtendedPrefix(task.path));
        job->info.size = task.size;

        unsigned long error = 0;
        job->mapped = MappedFile::open(task.path, error);
        if (!job->mapped) {
            session_.files_skipped++;
            completeFile(task.size);
            return;
        }
        const std::uint64_t size = job->mapped->size();
        job->info.size = size;
        if (size == 0) {
            completeFile(task.size);
            return;
        }

        MappedView sniff;
        if (!job->mapped->map(0, kSniffBytes, sniff, error)) {
            session_.files_skipped++;
            completeFile(task.size);
            return;
        }
        core::EncodingGuess guess = core::detectEncoding(sniff.data(), sniff.size());
        if (request_.forced_encoding) {
            guess.encoding = *request_.forced_encoding;
            guess.bom_length = core::bomLength(sniff.data(), sniff.size(), guess.encoding);
        } else if (guess.looks_binary && request_.skip_binary && !explicit_file_) {
            session_.files_skipped++;
            completeFile(task.size);
            return;
        }
        if (size != task.size) session_.bytes_total += size - std::min(size, task.size);

        job->info.encoding = guess.encoding;
        job->extent = {guess.bom_length, size, guess.encoding};
        job->chunks = core::planChunks(job->extent, kChunkBytes);
        if (job->chunks.empty()) {
            completeFile(size);
            return;
        }
        job->outcomes.resize(job->chunks.size());
        session_.bytes_done += guess.bom_length;

        if (job->chunks.size() > 1) {
            std::lock_guard lock(mutex_);
            for (std::size_t i = job->chunks.size(); i-- > 1;) {
                Task chunk_task;
                chunk_task.job = job;
                chunk_task.chunk = i;
                queue_.push_front(std::move(chunk_task));
            }
            work_.notify_all();
        }
        searchChunk(job, 0);
    }

    void searchChunk(const std::shared_ptr<FileJob>& job, std::size_t index) {
        core::ChunkOutcome outcome;
        const core::ChunkRange& chunk = job->chunks[index];
        if (!stopping()) {
            const core::ByteWindow window = core::windowForChunk(chunk, job->extent);
            MappedView view;
            unsigned long error = 0;
            if (job->mapped->map(window.offset, static_cast<std::size_t>(window.length), view, error) &&
                view.size() == window.length) {
                const std::size_t published = session_.rowCount();
                const std::size_t remaining = published >= request_.max_hits ? 0 : request_.max_hits - published;
                ChunkContext context;
                context.data = view.data();
                context.window = window;
                context.chunk = chunk;
                context.extent = job->extent;
                context.matcher = &matcher_;
                context.max_hits = remaining;
                context.stop = &stop_;
                context.outcome = &outcome;
                if (!runGuardedAgainstPageErrors(&runChunkContext, &context)) {
                    outcome = core::ChunkOutcome();
                    outcome.bytes_covered = chunk.end - chunk.begin;
                    std::lock_guard lock(job->mutex);
                    job->failed = true;
                }
            } else {
                outcome.bytes_covered = chunk.end - chunk.begin;
                std::lock_guard lock(job->mutex);
                job->failed = true;
            }
        } else {
            outcome.bytes_covered = chunk.end - chunk.begin;
        }
        session_.bytes_done += chunk.end - chunk.begin;
        publish(job, index, std::move(outcome));
    }

    void publish(const std::shared_ptr<FileJob>& job, std::size_t index, core::ChunkOutcome&& outcome) {
        bool file_complete = false;
        {
            std::lock_guard lock(job->mutex);
            job->outcomes[index] = std::move(outcome);
            while (job->next_to_publish < job->outcomes.size() && job->outcomes[job->next_to_publish]) {
                core::ChunkOutcome& ready = *job->outcomes[job->next_to_publish];
                if (!ready.hits.empty() && !stopping()) {
                    for (auto& hit : ready.hits) hit.line_number = 1 + job->newlines_published + hit.line_number;
                    if (!job->session_file) {
                        job->session_file = session_.registerFile(job->info);
                        session_.files_matched++;
                    }
                    const std::size_t room = request_.max_hits > session_.rowCount() ? request_.max_hits - session_.rowCount() : 0;
                    if (ready.hits.size() > room) ready.hits.resize(room);
                    session_.appendRows(*job->session_file, std::move(ready.hits));
                    if (session_.rowCount() >= request_.max_hits) {
                        session_.limit_reached = true;
                        stop_ = true;
                    }
                }
                job->newlines_published += ready.newline_count;
                job->outcomes[job->next_to_publish].reset();
                ++job->next_to_publish;
            }
            file_complete = job->next_to_publish == job->outcomes.size();
            if (file_complete && job->failed) session_.files_skipped++;
        }
        if (file_complete) completeFile(0);
        session_.notify(false);
    }

    void completeFile(std::uint64_t skipped_bytes) {
        session_.files_done++;
        session_.bytes_done += skipped_bytes;
        session_.notify(false);
    }

    const ContentRequest& request_;
    const core::TextMatcher& matcher_;
    ContentSession& session_;
    const std::atomic<bool>& cancel_;
    const bool explicit_file_;
    mutable std::atomic<bool> stop_{false};

    std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable space_;
    std::deque<Task> queue_;
    bool producing_ = true;
    int active_ = 0;
    std::vector<std::thread> workers_;
};

}

ContentSession::ContentSession(std::function<void()> notify) : notify_(std::move(notify)) {}

std::uint32_t ContentSession::registerFile(ContentFile file) {
    std::lock_guard lock(mutex_);
    files_.push_back(std::move(file));
    return static_cast<std::uint32_t>(files_.size() - 1);
}

void ContentSession::appendRows(std::uint32_t file, std::vector<core::TextHit>&& hits) {
    std::lock_guard lock(mutex_);
    rows_.reserve(rows_.size() + hits.size());
    for (auto& hit : hits) rows_.push_back({file, std::move(hit)});
}

void ContentSession::finish(bool was_cancelled) {
    {
        std::lock_guard lock(mutex_);
        finished_at_ = Clock::now();
    }
    cancelled = was_cancelled;
    finished = true;
    notify(true);
}

void ContentSession::setError(ContentError error) {
    std::lock_guard lock(mutex_);
    error_ = error;
}

void ContentSession::notify(bool force) {
    if (!notify_) return;
    const long long now = steadyMilliseconds();
    if (!force && now - last_notify_ms_.load() < 100) return;
    last_notify_ms_ = now;
    notify_();
}

std::size_t ContentSession::rowCount() const {
    std::lock_guard lock(mutex_);
    return rows_.size();
}

std::optional<ContentRow> ContentSession::row(std::size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= rows_.size()) return std::nullopt;
    return rows_[index];
}

std::optional<ContentFile> ContentSession::file(std::uint32_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= files_.size()) return std::nullopt;
    return files_[index];
}

ContentError ContentSession::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

double ContentSession::elapsedSeconds() const {
    std::lock_guard lock(mutex_);
    const Clock::time_point end = finished.load() ? finished_at_ : Clock::now();
    return std::chrono::duration<double>(end - started_).count();
}

void runContentSearch(const ContentRequest& request, const core::TextMatcher& matcher, ContentSession& session,
                      const std::atomic<bool>& cancel) {
    const PathKind kind = pathKind(request.location);
    if (kind == PathKind::Missing) {
        session.setError(ContentError::LocationMissing);
        session.enumeration_done = true;
        session.finish(false);
        return;
    }

    unsigned threads = request.threads;
    if (threads == 0) threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));

    SearchRun run(request, matcher, session, cancel, kind == PathKind::File);
    run.start(threads);

    const std::wstring root = toExtendedPath(request.location);
    if (kind == PathKind::File) {
        run.enqueueFile(root, 0);
    } else {
        std::vector<std::wstring> stack{root};
        std::vector<DirectoryEntry> entries;
        while (!stack.empty() && !run.stopping()) {
            const std::wstring directory = std::move(stack.back());
            stack.pop_back();
            if (listDirectory(directory, entries) != 0) {
                session.files_skipped++;
                continue;
            }
            for (const DirectoryEntry& entry : entries) {
                if (run.stopping()) break;
                const std::wstring path = joinPath(directory, entry.name);
                if (entry.isDirectory()) {
                    if (!entry.isReparsePoint()) stack.push_back(path);
                    continue;
                }
                run.enqueueFile(path, entry.size);
            }
        }
    }
    session.enumeration_done = true;
    run.finishProducing();
    run.join();
    if (session.files_total.load() == 0 && session.error() == ContentError::None)
        session.setError(ContentError::NothingToSearch);
    session.finish(cancel.load());
}

}
