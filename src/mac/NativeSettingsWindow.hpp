#pragma once

#include <QString>

class ProfileStore;
class QWidget;

namespace mac {

bool showNativeSettingsWindow(QWidget *parent,
                              ProfileStore &profiles,
                              QString &homePage,
                              QString &searchEngine,
                              bool &showFullUrl,
                              bool &closeWindowWithLastTab);

}  // namespace mac
