#pragma once

#include <QObject>
#include <QString>

// Lightweight wrapper around a WKWebsiteDataStore (held opaquely as void *
// so this header stays plain C++). Use `dataStore()` to retrieve the
// underlying `WKWebsiteDataStore *` from .mm code.
class WebKitProfile final : public QObject {
    Q_OBJECT
public:
    explicit WebKitProfile(const QString &name, QObject *parent = nullptr);
    ~WebKitProfile() override;

    QString name() const { return m_name; }
    // Returns a `WKWebsiteDataStore *`. Cast in .mm: (__bridge WKWebsiteDataStore *).
    void *dataStore() const { return m_dataStore; }

private:
    QString m_name;
    void *m_dataStore = nullptr;  // CFRetain'd WKWebsiteDataStore
};
