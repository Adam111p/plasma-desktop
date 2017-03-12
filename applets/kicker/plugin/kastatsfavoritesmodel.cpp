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

QString agentForPath(const QString &url)
{
    return url.startsWith("ktp:") ? "org.kde.plasma.favorites.contacts"
                                  : "org.kde.plasma.favorites.applications";
}

class KAStatsFavoritesModel::Private: public ResultModel {
public:
    Private(const KAStats::Query &query, KAStatsFavoritesModel *parent)
        : ResultModel(query, "org.kde.plasma.favorites", parent)
        , q(parent)
    {
    }

    QString pathForId(const QString &id) const
    {
        const auto entry = favoriteFromId(id);

        const auto url = entry && entry->isValid() ? entry->url()
                                                   : QUrl();

        qDebug() << "pathForId - id = " << id << " url = " << url;

        // We want to resolve symbolic links not to have two paths
        // refer to the same .desktop file
        if (url.isLocalFile()) {
            QFileInfo file(url.toLocalFile());

            if (file.exists()) {
                qDebug() << "    <-----" << file.canonicalFilePath();
                return file.canonicalFilePath();
            }
        }

        Q_ASSERT(url.scheme() != "file");
        Q_ASSERT(!url.toString().isEmpty());
        return url.toString();
    }

    AbstractEntry *favoriteFromId(const QString &id) const
    {
        // IDs can be plain appname.desktop
        // Also, it can be a file:///... URL
        // Or a file path
        // Or it is an URL which is not a file
        if (!m_entries.contains(id)) {
            AbstractEntry *entry = nullptr;

            qDebug() << "Creating entry for: " << id;

            if (QUrl(id).scheme() == "ktp") {
                entry = new ContactEntry(q, id);
                qDebug() << "ContactEntry" << entry->id() << entry->url();
            } else {
                entry = new AppEntry(q, id);
                qDebug() << "AppEntry" << entry->id() << entry->url();
            }

            if (!entry->isValid()) return nullptr;

            const QUrl url = entry->url();

            // Registering the entry for the ID we got
            m_entries[id] = entry;

            // Registering the entry for the ID it thinks it has
            m_entries[entry->id()] = entry;

            // Registering the entry for the URL it thinks it has
            m_entries[url.toString()] = entry;

            // And if it is a local file, registering it for the path
            if (url.isLocalFile()) {
                m_entries[entry->url().toLocalFile()] = entry;
            }
        }

        return m_entries[id];
    }

    void removeOldCachedEntries() const
    {
        QSet<QString> knownIds;
        for (int row = 0; row < rowCount(); ++row) {
            const auto id = data(index(row, 0), Kicker::UrlRole).toString();
            // const auto id = data(index(row, 0), ResultModel::ResourceRole).toString();
            Q_ASSERT(!id.isEmpty());

            const auto favorite = favoriteFromId(id);

            knownIds << id
                     << favorite->id()
                     << favorite->url().toString()
                     << favorite->url().toLocalFile();
        }

        QSet<AbstractEntry*> entriesToDelete;
        QMutableHashIterator<QString, AbstractEntry*> i(m_entries);
        while (i.hasNext()) {
            i.next();

            if (!knownIds.contains(i.key())) {
                entriesToDelete << i.value();
                i.remove();
            }
        }

        qDeleteAll(entriesToDelete);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rowCount()) {
            return QVariant();
        }

        const auto _this = const_cast<Private*>(this);
        const auto id    = ResultModel::data(index, ResultModel::ResourceRole).toString();
        const auto entry = _this->favoriteFromId(id);

        if (!entry || !entry->isValid()) {
            // If the result is not valid, we need to unlink it -- to
            // remove it from the model
            const auto path = pathForId(id);

            if (!m_invalidPaths.contains(path)) {
                _this->unlinkFromActivity(QUrl(path), Activity::any(),
                                          Agent(agentForPath(path)));
                m_invalidPaths << path;
            }

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

    bool trigger(int row, const QString &actionId, const QVariant &argument)
    {
        if (row < 0 || row >= rowCount()) {
            return false;
        }

        const QString id = data(index(row, 0), Kicker::UrlRole).toString();

        return m_entries.contains(id)
                    ? m_entries[id]->run(actionId, argument)
                    : false;
    }

    KAStatsFavoritesModel *const q;
    mutable QList<QString> m_invalidPaths;

    // This can contain an entry multiple times - for the id, file and url
    mutable QHash<QString, AbstractEntry *> m_entries;
};





KAStatsFavoritesModel::KAStatsFavoritesModel(QObject *parent)
: PlaceholderModel(parent)
, d(new Private(
        LinkedResources
            | Agent {
                "org.kde.plasma.favorites.applications",
                "org.kde.plasma.favorites.contacts"
            }
            | Type::any()
            | Activity::current()
            | Activity::global()
            | Limit(15),
        this)
  )
, m_enabled(true)
, m_maxFavorites(-1)
, m_activities(new KActivities::Consumer(this))
{
    QModelIndex index;

    if (d->canFetchMore(index)) {
        d->fetchMore(index);
    }

    setSourceModel(d);
}

KAStatsFavoritesModel::~KAStatsFavoritesModel()
{
}

QString KAStatsFavoritesModel::description() const
{
    return i18n("Favorites");
}

bool KAStatsFavoritesModel::trigger(int row, const QString &actionId, const QVariant &argument)
{
    qDebug() << "Trigger: " << row << actionId << argument << "<@>---------< <";

    // Do not allow triggering an item while another one is being dropped
    if (m_dropPlaceholderIndex != -1) return false;

    return d->trigger(row, actionId, argument);
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

bool KAStatsFavoritesModel::isFavorite(const QString &id) const
{
    d->removeOldCachedEntries();
    return d->m_entries.contains(id);
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
    if (id.isEmpty()) return;

    qDebug() << "";
    qDebug() << "Add favorite to" << id << activity << index << " <================###=";

    setDropPlaceholderIndex(-1);

    const auto path = d->pathForId(id);

    // This is a file, we want to check that it exists
    // if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) return;

    qDebug() << "Calling to link to the activity:"
             << path << activity << agentForPath(path)
             << " <---------";
    d->linkToActivity(QUrl(path), activity,
                      Agent(agentForPath(path)));

    if (index != -1) {
        d->setResultPosition(path, index);
    }
}

void KAStatsFavoritesModel::removeFavoriteFrom(const QString &id, const Activity &activity)
{
    const auto path = d->pathForId(id);

    qDebug() << "Removing favorite from: " << id << path << activity;

    d->unlinkFromActivity(
            QUrl(path), activity,
            Agent(agentForPath(path))
        );
}

void KAStatsFavoritesModel::moveRow(int from, int to)
{
    const auto id = data(index(from, 0), Kicker::UrlRole).toString();

    d->setResultPosition(d->pathForId(id), to);
}

AbstractModel *KAStatsFavoritesModel::favoritesModel()
{
    return this;
}

void KAStatsFavoritesModel::refresh()
{
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
    const auto path = d->pathForId(id);

    if (path.isEmpty()) {
        return {};
    }

    auto query = LinkedResources
                    | Agent {
                        "org.kde.plasma.favorites.applications",
                        "org.kde.plasma.favorites.contacts"
                      }
                    | Type::any()
                    | Activity::any()
                    | Url(path);

    ResultSet results(query);

    for (const auto &result: results) {
        return result.linkedActivities();
    }

    return {};
}

