#pragma once

class ProfileStore;
class QWidget;

namespace mac {

bool showNativeProfilePopover(QWidget *anchor, ProfileStore &profiles);

}  // namespace mac
