#pragma once

#include <QDateTime>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

struct DownloadItem {
    enum State { Running, Completed, Failed, Cancelled };

    QString id;
    QUrl url;
    QString fileName;
    QString path;
    qint64 received = 0;
    qint64 total = -1;
    State state = Running;
    QDateTime started;
    QDateTime finished;
    QString suggestedName;
    QString mimeType;
    QString pageTitle;
    QString error;
    double bytesPerSecond = 0.0;

    bool isActive() const { return state == Running; }
    bool isDone() const { return state != Running; }
};

// Owns every WKDownload produced by any WebView (via
// WebView::addNativeWebViewHook + WebView::downloadStarted), writes the files
// to ~/Downloads and keeps a small persistent history.
class DownloadManager final : public QObject {
    Q_OBJECT
public:
    static DownloadManager *instance();

    QVector<DownloadItem> items() const;
    bool item(const QString &id, DownloadItem *out) const;
    int activeCount() const;
    double aggregateProgress() const;

    void cancel(const QString &id);
    void remove(const QString &id);
    void clearFinished();
    void revealInFinder(const QString &id);
    void open(const QString &id);
    bool rename(const QString &id, const QString &newName);

    // Accept or dismiss a pending smart-rename suggestion.
    void applySuggestion(const QString &id);
    void dismissSuggestion(const QString &id);

    QString downloadsDirectory() const;

    // Finder-style icon for a file (by path, or by extension when the file
    // does not exist yet).
    static QIcon fileIcon(const QString &path, int pointSize);

signals:
    void itemAdded(const QString &id);
    void itemUpdated(const QString &id);
    void itemFinished(const QString &id, bool ok);
    void activeCountChanged(int active, double aggregateProgress);
    // A smart-rename suggestion is ready and awaits the user's decision.
    void renameSuggested(const QString &id, const QString &suggestion);

private:
    DownloadManager();
    ~DownloadManager() override;

    struct Impl;
    Impl *m_impl;
    friend struct DownloadManagerBridge;
};

QString humanSize(qint64 bytes);
