#include "WebKitProfile.hpp"

#import <Foundation/Foundation.h>
#import <WebKit/WKWebsiteDataStore.h>

#include <QSettings>
#include <QUuid>

WebKitProfile::WebKitProfile(const QString &name, QObject *parent)
    : QObject(parent), m_name(name) {
    WKWebsiteDataStore *ds = nil;

    if (name == QLatin1String("Default") || name.isEmpty()) {
        ds = [WKWebsiteDataStore defaultDataStore];
    } else if (@available(macOS 14.0, *)) {
        // Persist a stable UUID per profile name so subsequent launches get
        // the same on-disk data store.
        QSettings s;
        const QString key = QStringLiteral("profile-uuid/") + name;
        QString uuidStr = s.value(key).toString();
        if (uuidStr.isEmpty()) {
            uuidStr = QUuid::createUuid().toString(QUuid::WithoutBraces);
            s.setValue(key, uuidStr);
        }
        NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:uuidStr.toNSString()];
        if (uuid) {
            ds = [WKWebsiteDataStore dataStoreForIdentifier:uuid];
        } else {
            ds = [WKWebsiteDataStore defaultDataStore];
        }
    } else {
        ds = [WKWebsiteDataStore defaultDataStore];
    }

    m_dataStore = (__bridge_retained void *)ds;
}

WebKitProfile::~WebKitProfile() {
    if (m_dataStore) {
        CFRelease(m_dataStore);
        m_dataStore = nullptr;
    }
}
