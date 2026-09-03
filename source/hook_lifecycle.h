#pragma once

namespace WrapHookLifecycle {

constexpr int kApplicationWaitAttempts = 600;
constexpr int kApplicationWaitIntervalMs = 100;
constexpr int kDeferredScanIntervalMs = 2000;
constexpr int kDeferredScanWindowMs = 60000;

// Wrap loads some image/media plug-ins after QApplication construction. A
// bounded rescan lets their Qt drawText import slots join the display layer.
constexpr bool kEnableDeferredPolling = true;

} // namespace WrapHookLifecycle
