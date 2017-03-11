/***************************************************************************
 *   Copyright (C) 2014-2015 by Eike Hein <hein@kde.org>                   *
 *   Copyright (C) 2016-2017 by Ivan Cukic <ivan.cukic@kde.org>                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "kastatsfavoritesmodel.h"
#include "appentry.h"
#include "contactentry.h"
#include "fileentry.h"
#include "actionlist.h"

#include <QDebug>
#include <QFileInfo>
#include <QTimer>

#include <KLocalizedString>
#include <KSharedConfig>
#include <KConfigGroup>

#include <KActivities/Consumer>
#include <KActivities/Stats/Terms>
#include <KActivities/Stats/Query>
#include <KActivities/Stats/ResultSet>
#include <KActivities/Stats/ResultModel>

namespace KAStats = KActivities::Stats;

using namespace KAStats;
using namespace KAStats::Terms;

KAStatsFavoritesModel::KAStatsFavoritesModel(QObject *parent)
: PlaceholderModel(parent)
, m_enabled(true)
, m_maxFavorites(-1)
, m_whereTheItemIsBeingDropped(-1)
, m_sourceModel(nullptr)
, m_activities(new KActivities::Consumer(this))
{
    auto query = LinkedResources
                    | Agent {
                        "org.kde.plasma.favorites.applications",
                        "org.kde.plasma.favorites.contacts"
                      }
                    | Type::any()
                    | Activity::current()
                    | Activity::global()
                    | Limit(15);

    m_sourceModel = new ResultModel(query, "org.kde.plasma.favorites", this);

    QModelIndex index;

    if (m_sourceModel->canFetchMore(index)) {
        m_sourceModel->fetchMore(index);
    }

    setSourceModel(m_sourceModel);
}

KAStatsFavoritesModel::~KAStatsFavoritesModel()
{
}

QString KAStatsFavoritesModel::description() const
{
    return i18n("Favorites");
}

QVariant KAStatsFavoritesModel::internalData(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return QVariant();
    }

    const QString id =
        sourceModel()->data(index, ResultModel::ResourceRole).toString();

    // const casts are bad, but we can not achieve this
    // with the standard 'mutable' members for lazy evaluation,
    // at least, not with the current design of the library
    const auto _this = const_cast<KAStatsFavoritesModel*>(this);

    if (m_whereTheItemIsBeingDropped != -1) {
        // If we are in the process of dropping an item, we need to wait
        // for it to get here. When it does, request it to be moved
        // to its desired location. We can not do it in a better way
        // because the model might be reset in the mean time
        if (m_whichIdIsBeingDropped == id) {
            // This needs to happen only once, we need to make the captured
            // variable independent of the member variable and we are not
            // in C++14
            const auto where = m_whereTheItemIsBeingDropped;
            m_whereTheItemIsBeingDropped = -1;
            _this->m_sourceModel->setResultPosition(id, where);

        }
    }

    const auto *entry = _this->favoriteFromId(id);

    if (!entry || !entry->isValid()) {
        // If the result is not valid, we need to unlink it -- to
        // remove it from the model
        const auto url = urlForId(id);

        if (!m_invalidUrls.contains(url)) {
            m_sourceModel->unlinkFromActivity(
                    url, Activity::any(),
                    Agent(agentForScheme(url.scheme()))
                );
            m_invalidUrls << url;
        }

        // return QVariant("NULL for '" + url + "'! - " + agentForScheme(scheme));

        return role == Qt::DecorationRole ? "unknown"
                                          : QVariant();
    }

    return role == Qt::DisplayRole ? entry->name()
         : role == Qt::DecorationRole ? entry->icon()
         : role == Kicker::DescriptionRole ? entry->description()
         : role == Kicker::FavoriteIdRole ? entry->id()
         : role == Kicker::UrlRole ? entry->url()
         : role == Kicker::HasActionListRole ? entry->hasActions()
         : role == Kicker::ActionListRole ? entry->actions()
         : QVariant();
}

bool KAStatsFavoritesModel::trigger(int row, const QString &actionId, const QVariant &argument)
{
    if (row < 0 || row >= rowCount()) {
        return false;
    }

    const QString id =
        PlaceholderModel::data(index(row, 0), ResultModel::ResourceRole).toString();

    return m_entries.contains(id)
                ? m_entries[id]->run(actionId, argument)
                : false;
}

bool KAStatsFavoritesModel::enabled() const
{
    return m_enabled;
}

int KAStatsFavoritesModel::maxFavorites() const
{
    return m_maxFavorites;
}

void KAStatsFavoritesModel::setMaxFavorites(int max)
{
    Q_UNUSED(max);
}

void KAStatsFavoritesModel::setEnabled(bool enable)
{
    if (m_enabled != enable) {
        m_enabled = enable;

        emit enabledChanged();
    }
}

QStringList KAStatsFavoritesModel::favorites() const
{
    return QStringList();
}

void KAStatsFavoritesModel::setFavorites(const QStringList& favorites)
{
    auto config = KSharedConfig::openConfig("kactivitymanagerd-statsrc");
    KConfigGroup group(config, "ResultModel-Custom-org.kde.plasma.favorites");

    bool alreadyImported = group.readEntry("oldFavoritesImported", false);

    if (alreadyImported) return;

    group.writeEntry("oldFavoritesImported", true);
    config->sync();

    for (const auto& favorite: favorites) {
        addFavoriteTo(favorite, Activity::global());
    }
}

void KAStatsFavoritesModel::removeOldCachedEntries() const
{
    QList<QUrl> knownUrls;
    for (int row = 0; row < rowCount(); ++row) {
        knownUrls <<
            urlForId(sourceModel()->data(index(row, 0), ResultModel::ResourceRole).toString());
    }

    QMutableHashIterator<QString, AbstractEntry*> i(m_entries);
    while (i.hasNext()) {
        i.next();

        const auto url = urlForId(i.key());

        if (!knownUrls.contains(url)) {
            delete i.value();
            i.remove();
        }
    }
}

bool KAStatsFavoritesModel::isFavorite(const QString &id) const
{
    removeOldCachedEntries();
    return m_entries.contains(id);
}

void KAStatsFavoritesModel::addFavorite(const QString &id, int index)
{
    addFavoriteTo(id, Activity::current(), index);
}

void KAStatsFavoritesModel::removeFavorite(const QString &id)
{
    removeFavoriteFrom(id, Activity::current());
}

void KAStatsFavoritesModel::addFavoriteTo(const QString &id, const QString &activityId, int index)
{
    addFavoriteTo(id, Activity(activityId), index);
}

void KAStatsFavoritesModel::removeFavoriteFrom(const QString &id, const QString &activityId)
{
    removeFavoriteFrom(id, Activity(activityId));
}

void KAStatsFavoritesModel::addFavoriteTo(const QString &id, const Activity &activity, int index)
{
    qDebug() << "";
    qDebug() << "Add favorite to" << id << activity << index << " <================###=";

    if (index == -1) {
        index = m_sourceModel->rowCount();
        qDebug() << "    changed to " << index << " <---------";
    }

    setDropPlaceholderIndex(-1);

    if (id.isEmpty()) return;

    const auto url = urlForId(id);

    qDebug() << "    url " << url << " <---------";

    // This is a file, we want to check that it exists
    if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) return;

    qDebug() << "Calling to link to the activity:"
             << url << activity << agentForScheme(url.scheme())
             << " <---------";
    m_sourceModel->linkToActivity(
            url, activity,
            Agent(agentForScheme(url.scheme()))
        );

    // Lets handle async repositioning of the item, see ::data
    qDebug() << "    index - where the item is being dropped" << m_whereTheItemIsBeingDropped << " <---------";
    m_whereTheItemIsBeingDropped = index;

    if (index != -1) {
        qDebug() << "    which item is being dropped" << url.toLocalFile() << " <---------";
        m_whichIdIsBeingDropped = url.toLocalFile();
    } else {
        m_whichIdIsBeingDropped.clear();
    }
}

void KAStatsFavoritesModel::removeFavoriteFrom(const QString &id, const Activity &activity)
{
    const auto url = urlForId(id);

    m_sourceModel->unlinkFromActivity(
            url, activity,
            Agent(agentForScheme(url.scheme()))
        );
}

void KAStatsFavoritesModel::moveRow(int from, int to)
{
    const QString id =
        data(index(from, 0), Kicker::UrlRole).toString();

    m_sourceModel->setResultPosition(urlForId(id).toLocalFile(), to);
}

AbstractModel *KAStatsFavoritesModel::favoritesModel()
{
    return this;
}

void KAStatsFavoritesModel::refresh()
{
}

AbstractEntry *KAStatsFavoritesModel::favoriteFromId(const QString &id) const
{
    auto _this = const_cast<KAStatsFavoritesModel*>(this);

    if (!m_entries.contains(id)) {
        const QUrl url(id);
        const QString &scheme = url.scheme();

        AbstractEntry *entry = nullptr;

        if (scheme == "ktp") {
            entry = new ContactEntry(_this, id);
        } else {
            entry = new AppEntry(_this, id);
        }

        m_entries[id] = entry;
    }

    return m_entries[id];
}

QUrl KAStatsFavoritesModel::urlForId(const QString &id) const
{
    const auto entry = favoriteFromId(id);

    const auto url = entry && entry->isValid() ? entry->url()
                                               : QUrl();

    // We want to resolve symbolic links not to have two paths
    // refer to the same .desktop file
    if (url.isLocalFile()) {
        QFileInfo file(url.toLocalFile());

        if (file.exists()) {
            return QUrl::fromLocalFile(file.canonicalFilePath());
        }
    }

    return url;
}

QString KAStatsFavoritesModel::agentForScheme(const QString &scheme) const
{
    return scheme ==
        QStringLiteral("ktp") ? "org.kde.plasma.favorites.contacts"
                              : "org.kde.plasma.favorites.applications";
}

QObject *KAStatsFavoritesModel::activities() const
{
    return m_activities;
}

QString KAStatsFavoritesModel::activityNameForId(const QString &activityId) const
{
    // It is safe to use a short-lived object here,
    // we are always synced with KAMD in plasma
    KActivities::Info info(activityId);
    return info.name();
}

QStringList KAStatsFavoritesModel::linkedActivitiesFor(const QString &id) const
{
    auto url = urlForId(id);

    if (!url.isValid()) {
        return {};
    }

    auto urlString =
        url.scheme() == "file" ?
            url.toLocalFile() : url.toString();

    auto query = LinkedResources
                    | Agent {
                        "org.kde.plasma.favorites.applications",
                        "org.kde.plasma.favorites.contacts"
                      }
                    | Type::any()
                    | Activity::any()
                    | Url(urlString);

    ResultSet results(query);

    for (const auto &result: results) {
        return result.linkedActivities();
    }

    return {};
}

