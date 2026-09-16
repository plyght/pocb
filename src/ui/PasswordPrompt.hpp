#pragma once

#include "Theme.hpp"

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QTimer;
class QVBoxLayout;

// Liquid Glass suggestion popover shown below the address pill for the three
// Apple Passwords moments: pick a saved login (Touch ID), use a generated
// strong password, and save a just-submitted login. Only one prompt is on
// screen at a time; showing another dismisses the previous one. The prompt
// never activates its window so typing in the page keeps working.
class PasswordPrompt final : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Saved, Generate, Save };

    static PasswordPrompt *showSaved(QWidget *anchor, const QString &site, const QStringList &accounts, const Theme &theme = Theme());
    static PasswordPrompt *showGenerate(QWidget *anchor, const QString &site, const QString &password, const Theme &theme = Theme());
    static PasswordPrompt *showSave(QWidget *anchor, const QString &site, const QString &user, const Theme &theme = Theme());

    // Currently visible prompt, if any.
    static PasswordPrompt *current();
    // Hides and deletes the current prompt (call on navigation / tab switch).
    static void dismissCurrent();

    Kind kind() const { return m_kind; }
    QString site() const { return m_site; }

public slots:
    void dismiss();

signals:
    void useSavedChosen(const QString &user);
    void useGeneratedChosen(const QString &password);
    void saveChosen();
    void neverForSiteChosen();
    void dismissed();

protected:
    void showEvent(QShowEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    PasswordPrompt(Kind kind, QWidget *anchor, const QString &site, const Theme &theme);
    void buildSaved(const QStringList &accounts);
    void buildGenerate(const QString &password);
    void buildSave(const QString &user);
    QWidget *makeHeader(const QString &symbol, const QString &title, const QString &subtitle);
    QWidget *makeButton(const QString &text, bool primary);
    void reposition();
    void finish(std::function<void()> emitChoice);

    Kind m_kind;
    Theme m_theme;
    QString m_site;
    QString m_password;
    QPointer<QWidget> m_anchor;
    QVBoxLayout *m_col = nullptr;
    QTimer *m_autoHide = nullptr;
    bool m_done = false;
};
