// albert - a simple application launcher for linux
// Copyright (C) 2014-2016 Manuel Schneider
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <QStandardPaths>
#include <QSettings>
#include <QDirIterator>
#include <QThreadPool>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QDir>
#include "extension.h"
#include "configwidget.h"
#include "indexer.h"
#include "abstractquery.h"
#include "standardobjects.h"

const char* ChromeBookmarks::Extension::CFG_PATH       = "bookmarkfile";
const char* ChromeBookmarks::Extension::CFG_FUZZY      = "fuzzy";
const bool  ChromeBookmarks::Extension::DEF_FUZZY      = false;

/** ***************************************************************************/
ChromeBookmarks::Extension::Extension() : AbstractExtension("org.albert.extension.chromebookmarks") {

    // Load settings
    QSettings s(qApp->applicationName());
    s.beginGroup(id);
    offlineIndex_.setFuzzy(s.value(CFG_FUZZY, DEF_FUZZY).toBool());

    // Load and set a valid path
    QVariant v = s.value(CFG_PATH);
    if (v.isValid() && v.canConvert(QMetaType::QString) && QFileInfo(v.toString()).exists())
        setPath(v.toString());
    else
        restorePath();

    // If the path changed write it to the settings
    connect(this, &Extension::pathChanged, [this](const QString& path){
        QSettings(qApp->applicationName()).setValue(QString("%1/%2").arg(id, CFG_PATH), path);
    });

    s.endGroup();

    // Keep in sync with the bookmarkfile
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &Extension::updateIndex, Qt::QueuedConnection);
    connect(this, &Extension::pathChanged, this, &Extension::updateIndex, Qt::QueuedConnection);

    // Trigger an initial update
    updateIndex();
}



/** ***************************************************************************/
ChromeBookmarks::Extension::~Extension() {

    /*
     * Stop and wait for background indexer.
     * This should be thread safe since this thread is responisble to start the
     * indexer and, connections to this thread are disconnected in the QObject
     * destructor and all events for a deleted object are removed from the event
     * queue.
     */
    if (!indexer_.isNull()) {
        indexer_->abort();
        QEventLoop loop;
        connect(indexer_.data(), &Indexer::destroyed, &loop, &QEventLoop::quit);
        loop.exec();
    }
}



/** ***************************************************************************/
QWidget *ChromeBookmarks::Extension::widget(QWidget *parent) {
    if (widget_.isNull()){
        widget_ = new ConfigWidget(parent);

        // Paths
        widget_->ui.lineEdit_path->setText(bookmarksFile_);
        connect(widget_.data(), &ConfigWidget::requestEditPath, this, &Extension::setPath);
        connect(this, &Extension::pathChanged, widget_->ui.lineEdit_path, &QLineEdit::setText);

        // Fuzzy
        widget_->ui.checkBox_fuzzy->setChecked(fuzzy());
        connect(widget_->ui.checkBox_fuzzy, &QCheckBox::toggled, this, &Extension::setFuzzy);

        // Info
        widget_->ui.label_info->setText(QString("%1 bookmarks indexed.").arg(index_.size()));
        connect(this, &Extension::statusInfo, widget_->ui.label_info, &QLabel::setText);

        // If indexer is active connect its statusInfo to the infoLabel
        if (!indexer_.isNull())
            connect(indexer_.data(), &Indexer::statusInfo, widget_->ui.label_info, &QLabel::setText);
    }
    return widget_;
}



/** ***************************************************************************/
void ChromeBookmarks::Extension::handleQuery(AbstractQuery * query) {
    // Search for matches. Lock memory against indexer
    indexAccess_.lock();
    vector<shared_ptr<IIndexable>> indexables = offlineIndex_.search(query->searchTerm().toLower());
    indexAccess_.unlock();

    // Add results to query-> This cast is safe since index holds files only
    for (const shared_ptr<IIndexable> &obj : indexables)
        // TODO `Search` has to determine the relevance. Set to 0 for now
        query->addMatch(std::static_pointer_cast<StandardIndexItem>(obj), 0);
}



/** ***************************************************************************/
const QString &ChromeBookmarks::Extension::path() {
    return bookmarksFile_;
}



/** ***************************************************************************/
void ChromeBookmarks::Extension::setPath(const QString &path) {
    QFileInfo fi(path);

    if (!(fi.exists() && fi.isFile()))
        return;

    bookmarksFile_ = path;

    emit pathChanged(path);
}



/** ***************************************************************************/
void ChromeBookmarks::Extension::restorePath() {
    // Find a bookmark file (Take first one)
    for (const QString &browser : {"chromium","google-chrome"}){
        QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath(browser);
        QDirIterator it(root, {"Bookmarks"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            setPath(it.next());
            return;
        }
    }
}



/** ***************************************************************************/
void ChromeBookmarks::Extension::updateIndex() {
    // If thread is running, stop it and start this functoin after termination
    if (!indexer_.isNull()) {
        indexer_->abort();
        if (!widget_.isNull())
            widget_->ui.label_info->setText("Waiting for indexer to shut down ...");
        connect(indexer_.data(), &Indexer::destroyed, this, &Extension::updateIndex, Qt::QueuedConnection);
    } else {
        // Create a new scanning runnable for the threadpool
        indexer_ = new Indexer(this);

        //  Run it
        QThreadPool::globalInstance()->start(indexer_);

        // If widget is visible show the information in the status bat
        if (!widget_.isNull())
            connect(indexer_.data(), &Indexer::statusInfo, widget_->ui.label_info, &QLabel::setText);
    }
}



/** ***************************************************************************/
void ChromeBookmarks::Extension::setFuzzy(bool b) {
    indexAccess_.lock();
    QSettings(qApp->applicationName()).setValue(QString("%1/%2").arg(id, CFG_FUZZY), b);
    offlineIndex_.setFuzzy(b);
    indexAccess_.unlock();
}

