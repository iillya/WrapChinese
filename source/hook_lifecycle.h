#pragma once

namespace WrapHookLifecycle {

constexpr int kApplicationWaitAttempts = 600;
constexpr int kApplicationWaitIntervalMs = 100;
constexpr int kDeferredScanIntervalMs = 2000;
constexpr int kDeferredScanWindowMs = 60000;

// New widgets request a coalesced module scan. Keep the bounded legacy polling
// implementation available without waking the GUI while the application idles.
constexpr bool kEnableDeferredPolling = false;

} // namespace WrapHookLifecycle
