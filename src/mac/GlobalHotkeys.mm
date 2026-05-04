#include "GlobalHotkeys.hpp"

#ifdef __APPLE__
#import <Carbon/Carbon.h>
#include <QCoreApplication>
#include <QMetaObject>
#endif

namespace mac {

#ifdef __APPLE__
static std::function<void()> g_newLittleWindowHandler;
static EventHotKeyRef g_newLittleWindowHotKey = nullptr;
static EventHandlerRef g_newLittleWindowEventHandler = nullptr;

static OSStatus pocbHotKeyHandler(EventHandlerCallRef, EventRef event, void *) {
    EventHotKeyID hotKeyId;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotKeyId), nullptr, &hotKeyId) != noErr) return noErr;
    if (hotKeyId.signature == 'pocb' && hotKeyId.id == 1 && g_newLittleWindowHandler) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [] {
            if (g_newLittleWindowHandler) g_newLittleWindowHandler();
        }, Qt::QueuedConnection);
    }
    return noErr;
}
#endif

void installNewLittleWindowHotkey(std::function<void()> handler) {
#ifdef __APPLE__
    g_newLittleWindowHandler = std::move(handler);
    if (!g_newLittleWindowEventHandler) {
        EventTypeSpec eventType = {kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&pocbHotKeyHandler, 1, &eventType, nullptr, &g_newLittleWindowEventHandler);
    }
    if (g_newLittleWindowHotKey) UnregisterEventHotKey(g_newLittleWindowHotKey);
    EventHotKeyID hotKeyId = {'pocb', 1};
    RegisterEventHotKey(kVK_ANSI_N, cmdKey | optionKey, hotKeyId, GetApplicationEventTarget(), 0, &g_newLittleWindowHotKey);
#else
    (void)handler;
#endif
}

}  // namespace mac
