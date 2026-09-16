#pragma once

#include <QString>

#include <functional>

struct DownloadItem;

// On-device (Apple Foundation Models) filename suggestions for finished
// downloads. Everything here is optional: when the Swift side was not
// compiled in, or the system model is unavailable, isAvailable() is false
// and suggestName() completes with an empty string.
namespace DownloadIntelligence {

bool isAvailable();

// Asks the model for a human-readable, filesystem-safe name that keeps the
// original extension. `done` is always invoked on the Qt main thread,
// exactly once, with an empty string when there is no suggestion.
void suggestName(const DownloadItem &item, std::function<void(QString)> done);

// Exposed for tests / reuse: strips path separators, control characters,
// re-applies `originalExtension`, caps the result at 120 characters.
QString sanitizeSuggestion(const QString &suggestion, const QString &originalName);

}  // namespace DownloadIntelligence
