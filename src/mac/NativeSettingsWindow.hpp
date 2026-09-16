#pragma once

#include <QString>

class ProfileStore;
class QWidget;

namespace mac {

struct SettingsOutcome {
    bool saved = false;
    bool importPasswords = false;
    bool pageColorSchemeChanged = false;
};

SettingsOutcome showNativeSettingsWindow(QWidget *parent,
                                         ProfileStore &profiles,
                                         QString &homePage,
                                         QString &searchEngine,
                                         bool &showFullUrl,
                                         bool &closeWindowWithLastTab);

}  // namespace mac
