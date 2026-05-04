#include "DefaultBrowser.hpp"

#ifdef __APPLE__
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#endif

namespace mac {

bool setAsDefaultBrowser() {
#ifdef __APPLE__
    CFStringRef bundleId = CFBundleGetIdentifier(CFBundleGetMainBundle());
    if (!bundleId) return false;
    OSStatus http = LSSetDefaultHandlerForURLScheme(CFSTR("http"), bundleId);
    OSStatus https = LSSetDefaultHandlerForURLScheme(CFSTR("https"), bundleId);
    return http == noErr && https == noErr;
#else
    return false;
#endif
}

bool isDefaultBrowser() {
#ifdef __APPLE__
    CFStringRef bundleId = CFBundleGetIdentifier(CFBundleGetMainBundle());
    if (!bundleId) return false;
    CFStringRef http = LSCopyDefaultHandlerForURLScheme(CFSTR("http"));
    CFStringRef https = LSCopyDefaultHandlerForURLScheme(CFSTR("https"));
    const bool ok = http && https && CFStringCompare(http, bundleId, 0) == kCFCompareEqualTo && CFStringCompare(https, bundleId, 0) == kCFCompareEqualTo;
    if (http) CFRelease(http);
    if (https) CFRelease(https);
    return ok;
#else
    return false;
#endif
}

}  // namespace mac
