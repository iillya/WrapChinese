#pragma once

#include "plugin_config.h"

#include <QtCore/QEvent>
#include <QtCore/QSet>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

// Keyboard capture belongs only to the large middle button. Dialog clicks and
// its other controls therefore never leak into the global toggle handler.
class WrapChineseHotkeyDialog final : public QDialog {
public:
    WrapChineseHotkeyDialog(QWidget* parent, QKeySequence& currentShortcut,
                            const QString& configPath)
        : QDialog(parent), currentShortcut_(currentShortcut),
          configPath_(configPath), pendingShortcut_(currentShortcut) {
        setWindowTitle(QStringLiteral("中英切换快捷键"));
        setObjectName(QStringLiteral("wrap_chinese_hotkey_settings"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setFixedWidth(400);
        if (parent) {
            setFont(parent->font());
            setWindowIcon(parent->windowIcon());
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(8);

        auto* instruction = new QLabel(
            QStringLiteral("点击中间按钮，然后按下新的快捷键。"), this);
        instruction->setObjectName(QStringLiteral("hotkeyInstruction"));
        layout->addWidget(instruction);

        capture_ = new QPushButton(this);
        capture_->setObjectName(QStringLiteral("hotkeyCapture"));
        capture_->setAutoDefault(false);
        capture_->installEventFilter(this);
        layout->addWidget(capture_);

        auto* hint = new QLabel(
            QStringLiteral("可直接使用 F1–F12；组合快捷键支持 Ctrl、Shift 和 Alt。"),
            this);
        hint->setObjectName(QStringLiteral("hotkeyHint"));
        layout->addWidget(hint);

        error_ = new QLabel(this);
        error_->setObjectName(QStringLiteral("hotkeyError"));
        error_->setWordWrap(true);
        error_->hide();
        layout->addWidget(error_);

        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(8);
        layout->addLayout(buttons);
        auto* reset = new QPushButton(QStringLiteral("恢复默认"), this);
        reset->setObjectName(QStringLiteral("hotkeyReset"));
        auto* cancel = new QPushButton(QStringLiteral("取消"), this);
        ok_ = new QPushButton(QStringLiteral("保存"), this);
        ok_->setObjectName(QStringLiteral("hotkeyAccept"));
        reset->setAutoDefault(false);
        cancel->setAutoDefault(false);
        ok_->setDefault(true);
        for (QPushButton* button : {reset, cancel, ok_})
            button->setMinimumWidth(84);
        buttons->addWidget(reset);
        buttons->addStretch();
        buttons->addWidget(cancel);
        buttons->addWidget(ok_);

        connect(capture_, &QPushButton::clicked, this,
                [this] { beginCapture(); });
        connect(reset, &QPushButton::clicked, this, [this] {
            pendingShortcut_ = WrapChineseConfig::defaultToggleShortcut();
            finishCapture();
            clearError();
        });
        connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
        connect(ok_, &QPushButton::clicked, this, [this] { accept(); });
        finishCapture();
    }

protected:
    void accept() override {
        if (capturing_) return;
        QString error;
        if (!WrapChineseConfig::saveToggleShortcut(
                configPath_, pendingShortcut_, &error)) {
            showError(QStringLiteral("保存失败：") + error);
            return;
        }
        currentShortcut_ = pendingShortcut_;
        QDialog::accept();
    }

    void reject() override {
        capturing_ = false;
        swallowedReleases_.clear();
        QDialog::reject();
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (object != capture_) return QDialog::eventFilter(object, event);
        if (event->type() != QEvent::ShortcutOverride &&
            event->type() != QEvent::KeyPress &&
            event->type() != QEvent::KeyRelease) {
            return QDialog::eventFilter(object, event);
        }
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (event->type() == QEvent::KeyRelease) {
            if (capturing_ || swallowedReleases_.remove(keyEvent->key())) {
                event->accept();
                return true;
            }
            return QDialog::eventFilter(object, event);
        }
        if (!capturing_) return QDialog::eventFilter(object, event);
        if (event->type() == QEvent::ShortcutOverride ||
            keyEvent->isAutoRepeat()) {
            event->accept();
            return true;
        }
        const int key = keyEvent->key();
        if (key == Qt::Key_Escape) {
            swallowedReleases_.insert(key);
            finishCapture();
            clearError();
            event->accept();
            return true;
        }
        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_AltGr) {
            capture_->setText(QStringLiteral("等待组合键…"));
            event->accept();
            return true;
        }
        if (key == Qt::Key_Meta) {
            swallowedReleases_.insert(key);
            showError(QStringLiteral("不支持 Win 键，请使用 Ctrl、Shift 或 Alt。"));
            event->accept();
            return true;
        }
        const QKeySequence candidate(
            keyEvent->key() | static_cast<int>(keyEvent->modifiers()));
        if (!WrapChineseConfig::isSupportedShortcut(candidate)) {
            showError(QStringLiteral(
                "普通按键需配合 Ctrl、Shift 或 Alt，也可以直接使用 F1–F12。"));
            event->accept();
            return true;
        }
        pendingShortcut_ = candidate;
        swallowedReleases_.insert(key);
        finishCapture();
        clearError();
        event->accept();
        return true;
    }

private:
    void beginCapture() {
        capturing_ = true;
        swallowedReleases_.clear();
        clearError();
        ok_->setEnabled(false);
        capture_->setText(QStringLiteral("等待按键…"));
        capture_->setFocus(Qt::MouseFocusReason);
    }

    void finishCapture() {
        capturing_ = false;
        ok_->setEnabled(true);
        capture_->setText(QStringLiteral("当前：%1").arg(
            pendingShortcut_.toString(QKeySequence::NativeText)));
    }

    void clearError() {
        error_->clear();
        error_->hide();
    }

    void showError(const QString& message) {
        error_->setText(message);
        error_->show();
    }

    QKeySequence& currentShortcut_;
    const QString configPath_;
    QKeySequence pendingShortcut_;
    bool capturing_ = false;
    QSet<int> swallowedReleases_;
    QPushButton* capture_ = nullptr;
    QPushButton* ok_ = nullptr;
    QLabel* error_ = nullptr;
};
