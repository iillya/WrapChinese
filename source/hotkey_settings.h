#pragma once

#include "plugin_config.h"

#include <QtCore/QEvent>
#include <QtCore/QSet>
#include <QtGui/QFont>
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
        setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
        setStyleSheet(QStringLiteral(
            "QDialog#wrap_chinese_hotkey_settings { background:#292b2e; }"
            "QDialog#wrap_chinese_hotkey_settings QLabel { color:#d2d5d9; background:transparent; font-size:13px; }"
            "QDialog#wrap_chinese_hotkey_settings QLabel#hotkeyInstruction { color:#eef0f2; }"
            "QDialog#wrap_chinese_hotkey_settings QLabel#hotkeyHint { color:#a5abb2; }"
            "QDialog#wrap_chinese_hotkey_settings QLabel#hotkeyError { color:#ff9f96; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton { color:#e6e8ea; background:#383b3f; border:1px solid #50545a; border-radius:5px; padding:3px 10px; min-height:22px; font-size:13px; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton:hover { background:#45494e; border-color:#62676e; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton:pressed { background:#303337; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton:focus { border-color:#6ba7df; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton#hotkeyCapture { color:#f2f4f6; background:#202225; border-color:#5b6067; min-height:24px; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton#hotkeyCapture:focus { border-color:#5b9bd5; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton#hotkeyReset { color:#c3c8cd; background:transparent; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton#hotkeyAccept { color:white; background:#3979b8; border-color:#5597d2; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton#hotkeyAccept:hover { background:#4488c7; }"
            "QDialog#wrap_chinese_hotkey_settings QPushButton:disabled { background:#34373a; color:#777c82; border-color:#464a4f; }"));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(5);

        auto* instruction = new QLabel(
            QStringLiteral("点击下方按钮，然后按下新的快捷键。"), this);
        instruction->setObjectName(QStringLiteral("hotkeyInstruction"));
        layout->addWidget(instruction);

        capture_ = new QPushButton(this);
        capture_->setObjectName(QStringLiteral("hotkeyCapture"));
        capture_->setAutoDefault(false);
        capture_->installEventFilter(this);
        layout->addWidget(capture_);

        auto* hint = new QLabel(
            QStringLiteral("F1–F12 可直接使用；组合键支持 Ctrl / Shift / Alt。"),
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
                "普通按键需配合 Ctrl、Shift 或 Alt；也可以直接使用功能键。"));
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
        capture_->setText(QStringLiteral("当前：") +
            pendingShortcut_.toString(QKeySequence::NativeText));
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
