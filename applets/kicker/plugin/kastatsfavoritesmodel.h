/***************************************************************************
 *   Copyright (C) 2014-2015 by Eike Hein <hein@kde.org>                   *
 *   Copyright (C) 2016-2015 by Ivan Cukic <ivan.cukic@kde.org>            *
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

#ifndef FAVORITESMODEL_H
#define FAVORITESMODEL_H

#include "forwardingmodel.h"

#include <QPointer>

#include <KService>
#include <KConfig>

namespace KActivities {
    class Consumer;
namespace Stats {
    class ResultModel;
namespace Terms {
    class Activity;
} // namespace Terms
} // namespace Stats
} // namespace KActivities

class KAStatsFavoritesModel : public ForwardingModel
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QStringList favorites READ favorites WRITE setFavorites NOTIFY favoritesChanged)
    Q_PROPERTY(int maxFavorites READ maxFavorites WRITE setMaxFavorites NOTIFY maxFavoritesChanged)
    Q_PROPERTY(int dropPlaceholderIndex READ dropPlaceholderIndex WRITE setDropPlaceholderIndex NOTIFY dropPlaceholderIndexChanged)

    Q_PROPERTY(QObject* activities READ activities CONSTANT)

    public:
        explicit KAStatsFavoritesModel(QObject *parent = 0);
        ~KAStatsFavoritesModel();

        QString description() const;

        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;

        // int rowCount(const QModelIndex &parent = QModelIndex()) const;

        Q_INVOKABLE bool trigger(int row, const QString &actionId, const QVariant &argument);

        bool enabled() const;
        void setEnabled(bool enable);

        QStringList favorites() const;
        void setFavorites(const QStringList &favorites);

        int maxFavorites() const;
        void setMaxFavorites(int max);

        Q_INVOKABLE bool isFavorite(const QString &id) const;

        Q_INVOKABLE void addFavorite(const QString &id, int index = -1);
        Q_INVOKABLE void removeFavorite(const QString &id);

        Q_INVOKABLE void addFavoriteTo(const QString &id, const QString &activityId, int index = -1);
        Q_INVOKABLE void removeFavoriteFrom(const QString &id, const QString &activityId);

        Q_INVOKABLE QStringList linkedActivitiesFor(const QString &id) const;

        Q_INVOKABLE void moveRow(int from, int to);

        QObject *activities() const;
        Q_INVOKABLE QString activityNameForId(const QString &activityId) const;

        int dropPlaceholderIndex() const;
        void setDropPlaceholderIndex(int index);

        AbstractModel* favoritesModel();

    public Q_SLOTS:
        virtual void refresh();

    Q_SIGNALS:
        void enabledChanged() const;
        void favoritesChanged() const;
        void maxFavoritesChanged() const;
        void dropPlaceholderIndexChanged();

    private:
        AbstractEntry *favoriteFromId(const QString &id) const;

        QString validateUrl(const QString &url, QString * scheme = nullptr) const;
        QString agentForScheme(const QString &scheme) const;

        void addFavoriteTo(const QString &id, const KActivities::Stats::Terms::Activity &activityId, int index = -1);
        void removeFavoriteFrom(const QString &id, const KActivities::Stats::Terms::Activity &activityId);

        bool m_enabled;

        // QList<AbstractEntry *> m_entryList;
        mutable QHash<QString, AbstractEntry *> m_entries;
        void removeOldCachedEntries() const;
        int m_maxFavorites;

        int m_dropPlaceholderIndex;
        KActivities::Stats::ResultModel *m_sourceModel;
        KActivities::Consumer *m_activities;
        mutable QStringList m_invalidUrls;

        KConfig m_config;
};

#endif
