#pragma once

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

inline QString U(const char *text)
{
    return QString::fromUtf8(text);
}

inline QLabel *labelTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("cardTitle");
    return label;
}

inline QLabel *labelMuted(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("muted");
    label->setWordWrap(true);
    return label;
}

inline QLabel *labelHint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("hint");
    label->setWordWrap(true);
    return label;
}

inline QLabel *labelBadge(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("badge");
    return label;
}

inline QLabel *labelSection(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("section");
    return label;
}

inline void setStatus(QLabel *label, const QString &text, const QString &color = "#16A34A")
{
    label->setText(text);
    label->setStyleSheet(QString("color:%1; font-weight:600;").arg(color));
}

inline QFrame *makeCard(const QString &title, const QString &description, QWidget *content)
{
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(title));
    if (!description.isEmpty()) {
        layout->addWidget(labelMuted(description));
    }
    if (content) {
        layout->addWidget(content);
    }
    return card;
}

inline QWidget *makeFormRow(const QString &label, QWidget *field, const QString &hint = QString())
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *name = new QLabel(label);
    name->setMinimumWidth(96);
    name->setObjectName("muted");
    layout->addWidget(name, 0, Qt::AlignTop);
    layout->addWidget(field, 0, Qt::AlignLeft | Qt::AlignTop);
    if (!hint.isEmpty()) {
        auto *h = labelHint(hint);
        h->setMaximumWidth(260);
        layout->addWidget(h, 0, Qt::AlignLeft | Qt::AlignTop);
    }
    layout->addStretch(1);
    return row;
}

inline QLineEdit *makeCommandEdit()
{
    auto *edit = new QLineEdit;
    edit->setObjectName("command");
    edit->setReadOnly(true);
    edit->setPlaceholderText(QStringLiteral("-"));
    return edit;
}

inline QWidget *makeCommandWidget(QLineEdit *edit)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(edit, 1);

    auto *copy = new QPushButton(U("复制"));
    copy->setObjectName("secondary");
    copy->setCursor(Qt::PointingHandCursor);
    QObject::connect(copy, &QPushButton::clicked, edit, [edit]() {
        QApplication::clipboard()->setText(edit->text());
    });
    layout->addWidget(copy, 0);
    return row;
}

inline QPushButton *makeDisabledPrimary(const QString &text = U("执行"))
{
    auto *button = new QPushButton(text);
    button->setObjectName("primary");
    button->setEnabled(false);
    return button;
}

inline QPushButton *makeSecondary(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setObjectName("secondary");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

inline QScrollArea *makeScrollPage(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}
