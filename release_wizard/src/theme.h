#pragma once

// Shared visual tokens for the release wizard. Keep it simple and readable.

static const char *kAppStyle = R"QSS(
QWidget {
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
    color: #1F2937;
}
QMainWindow, QWidget#root {
    background: #F3F5F9;
}
QWidget#sidebar {
    background: #FFFFFF;
    border-right: 1px solid #E5E7EB;
}
QLabel#appTitle {
    font-size: 14pt;
    font-weight: 600;
    color: #111827;
}
QLabel#appSubtitle {
    color: #6B7280;
    font-size: 8pt;
}
QLabel#pageTitle {
    font-size: 16pt;
    font-weight: 600;
    color: #111827;
}
QLabel#pageSubtitle {
    color: #6B7280;
}
QLabel#section {
    font-size: 11pt;
    font-weight: 600;
    color: #111827;
    margin-top: 4px;
}
QLabel#cardTitle {
    font-size: 11pt;
    font-weight: 600;
    color: #111827;
}
QLabel#repoTitle {
    font-size: 13pt;
    font-weight: 600;
    color: #111827;
}
QLabel#subTitle {
    font-size: 9pt;
    font-weight: 600;
    color: #6B7280;
}
QLabel#muted {
    color: #6B7280;
}
QLabel#hint {
    color: #9CA3AF;
    font-size: 8pt;
}
QLabel#badge {
    background: #EFF6FF;
    color: #1D4ED8;
    border: 1px solid #BFDBFE;
    border-radius: 9px;
    padding: 2px 9px;
    font-size: 8pt;
}
QLabel#tagPreview {
    font-family: Consolas, "Courier New", monospace;
    color: #1D4ED8;
    font-size: 10pt;
}
QFrame#card {
    background: #FFFFFF;
    border: 1px solid #E5E7EB;
    border-radius: 8px;
}
QFrame#subcard {
    background: #F8FAFC;
    border: 1px solid #EEF2F7;
    border-radius: 6px;
}
QFrame#overviewBar {
    background: rgba(243, 245, 249, 238);
    border: none;
    border-bottom: 1px solid #E5E7EB;
}
QLabel#statusDot {
    color: #16A34A;
    font-size: 10pt;
}
QLabel#hintLabel {
    color: #6B7280;
    font-size: 8pt;
    font-weight: 600;
}
QLabel#hintTime {
    color: #111827;
    font-size: 10pt;
}
QFrame#repoCard {
    background: #FFFFFF;
    border: 1px solid #E5E7EB;
    border-radius: 8px;
}
QFrame#repoCard QLabel {
    font-size: 10pt;
}
QFrame#repoCard QLabel#repoTitle {
    font-size: 13pt;
    font-weight: 600;
    color: #111827;
}
QFrame#repoCard QLabel#subTitle {
    font-size: 9pt;
    font-weight: 600;
    color: #6B7280;
}
QFrame#repoCard QPushButton {
    font-size: 10pt;
}
QFrame#mainCard {
    background: #EFF6FF;
    border: 1px solid #BFDBFE;
    border-radius: 8px;
}
QFrame#mainCard QLabel {
    font-size: 10pt;
}
QFrame#mainCard QLabel#repoTitle {
    font-size: 13pt;
    font-weight: 600;
    color: #1E3A8A;
}
QFrame#mainCard QLabel#subTitle {
    font-size: 9pt;
    font-weight: 600;
    color: #3B82F6;
}
QFrame#mainCard QPushButton {
    font-size: 10pt;
}
QFrame#mainCard QFrame#subcard {
    background: #FFFFFF;
    border: 1px solid #DBEAFE;
}
QFrame#logPanel {
    background: #0F172A;
    border-radius: 8px;
}
QLabel#logTitle {
    color: #E2E8F0;
    font-weight: 600;
}
QPushButton {
    border-radius: 6px;
    padding: 6px 14px;
}
QPushButton#primary {
    background: #2563EB;
    color: #FFFFFF;
    border: none;
}
QPushButton#primary:hover {
    background: #1D4ED8;
}
QPushButton#primary:disabled {
    background: #BFDBFE;
    color: #EFF6FF;
}
QPushButton#secondary {
    background: #FFFFFF;
    border: 1px solid #D1D5DB;
    color: #374151;
}
QPushButton#secondary:hover {
    background: #F9FAFB;
}
QPushButton#secondary:disabled {
    color: #9CA3AF;
    border-color: #E5E7EB;
}
QPushButton#accordionHeader {
    background: transparent;
    border: none;
    text-align: left;
    padding: 2px 0;
    color: #334155;
    font-weight: 600;
}
QPushButton#accordionHeader:hover {
    color: #1D4ED8;
}
QPushButton#logClear {
    background: transparent;
    border: 1px solid #334155;
    color: #CBD5E1;
    padding: 2px 10px;
}
QLineEdit, QComboBox, QSpinBox {
    background: #FFFFFF;
    border: 1px solid #D1D5DB;
    border-radius: 6px;
    padding: 5px 8px;
    min-height: 18px;
}
QLineEdit:focus, QComboBox:focus {
    border-color: #2563EB;
}
QLineEdit:disabled, QComboBox:disabled {
    background: #F9FAFB;
    color: #9CA3AF;
}
QLineEdit#command {
    background: #F8FAFC;
    font-family: Consolas, "Courier New", monospace;
    color: #334155;
}
QListWidget#nav {
    border: none;
    background: transparent;
    outline: none;
}
QListWidget#nav::item {
    padding: 10px 14px;
    margin: 2px 8px;
    border-radius: 6px;
    color: #374151;
}
QListWidget#nav::item:selected {
    background: #EFF6FF;
    color: #1D4ED8;
    font-weight: 600;
}
QListWidget#nav::item:hover {
    background: #F3F4F6;
}
QTableWidget {
    border: none;
    gridline-color: transparent;
    background: #FFFFFF;
    alternate-background-color: #FAFBFD;
    selection-background-color: #EFF6FF;
    selection-color: #1F2937;
}
QProgressBar {
    border: none;
    background: #E5E7EB;
    border-radius: 2px;
}
QProgressBar::chunk {
    background: #2563EB;
    border-radius: 2px;
}
QHeaderView::section {
    background: #F9FAFB;
    border: none;
    border-bottom: 1px solid #E5E7EB;
    padding: 6px;
    color: #6B7280;
    font-weight: 600;
}
QTableWidget::item {
    padding: 4px;
}
QPlainTextEdit#log {
    background: #0F172A;
    color: #E2E8F0;
    border: none;
    font-family: Consolas, "Courier New", monospace;
    font-size: 9pt;
}
QPlainTextEdit#bom {
    background: #F8FAFC;
    border: 1px solid #E5E7EB;
    border-radius: 6px;
    font-family: Consolas, "Courier New", monospace;
    color: #334155;
}
QTextBrowser#mdPreview {
    background: #FFFFFF;
    border: 1px solid #E5E7EB;
    border-radius: 6px;
}
QTabWidget::pane {
    border: 1px solid #E5E7EB;
    border-radius: 8px;
    background: #FFFFFF;
    top: -1px;
}
QTabBar::tab {
    padding: 7px 16px;
    color: #6B7280;
    background: transparent;
}
QTabBar::tab:selected {
    color: #1D4ED8;
    border-bottom: 2px solid #2563EB;
}
QScrollArea {
    border: none;
    background: transparent;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #CBD5E1;
    border-radius: 4px;
    min-height: 30px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QCheckBox {
    spacing: 8px;
}
QRadioButton {
    spacing: 8px;
}
QStatusBar {
    background: #FFFFFF;
    color: #6B7280;
    border-top: 1px solid #E5E7EB;
}
)QSS";
