/*
 * AcreetionOS Media Writer
 * Copyright (C) 2026 Natalie <natalie@acreetionos.org>
 * Copyright (C) 2016 Martin Bříza <mbriza@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "releasemanager.h"
#include "drivemanager.h"

#include "isomd5/libcheckisomd5.h"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QFileInfo>
#include <QtQml>

#include <QJsonDocument>
#include <QRegularExpression>

#include <utility>

using namespace Qt::Literals::StringLiterals;

ReleaseManager::ReleaseManager(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_sourceModel(new ReleaseListModel(this))
{
    mDebug() << this->metaObject()->className() << "construction";
    setSourceModel(m_sourceModel);

    qmlRegisterUncreatableType<Release>("MediaWriter", 1, 0, "Release", "");
    qmlRegisterUncreatableType<ReleaseVersion>("MediaWriter", 1, 0, "Version", "");
    qmlRegisterUncreatableType<ReleaseVariant>("MediaWriter", 1, 0, "Variant", "");
    qmlRegisterUncreatableType<ReleaseArchitecture>("MediaWriter", 1, 0, "Architecture", "");
    qmlRegisterUncreatableType<Progress>("MediaWriter", 1, 0, "Progress", "");

    QFile releases(":/releases.json");
    if (releases.open(QIODevice::ReadOnly)) {
        onStringDownloaded(releases.readAll());
        releases.close();
    }

    connect(this, SIGNAL(selectedChanged()), this, SLOT(variantChangedFilter()));
    QTimer::singleShot(0, this, SLOT(fetchReleases()));
}

bool ReleaseManager::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    Q_UNUSED(source_parent)
    auto r = get(source_row);

    if (r->source() != m_filterSource) {
        return false;
    } else {
        bool containsArch = false;
        for (auto version : r->versionList()) {
            for (auto variant : version->variantList()) {
                if (variant->arch()->index() == m_filterArchitecture) {
                    containsArch = true;
                    break;
                }
            }
            if (containsArch)
                break;
        }
        return r->isLocal() || (containsArch && (r->name().contains(m_filterText, Qt::CaseInsensitive) || r->summary().contains(m_filterText, Qt::CaseInsensitive)));
    }
}

Release *ReleaseManager::get(int index) const
{
    return m_sourceModel->get(index);
}

void ReleaseManager::fetchReleases()
{
    m_beingUpdated = true;
    Q_EMIT beingUpdatedChanged();

    m_fetchAttempts++;
    m_lastFetchUrl = options.releasesUrl;
    mDebug() << this->metaObject()->className() << "Fetching release data from" << m_lastFetchUrl;
    DownloadManager::instance()->fetchPageAsync(this, m_lastFetchUrl);
}

void ReleaseManager::variantChangedFilter()
{
    Q_EMIT variantChanged();
}

bool ReleaseManager::beingUpdated() const
{
    return m_beingUpdated;
}

bool ReleaseManager::frontPage() const
{
    return m_frontPage;
}

void ReleaseManager::setFrontPage(bool o)
{
    if (m_frontPage != o) {
        m_frontPage = o;
        Q_EMIT frontPageChanged();
        // Qt >= 6.10: beginFilterChange()/endFilterChange() replace invalidateFilter().
        // Older Qt (6.6-6.9: Fedora 41, KDE flatpak runtime) only has invalidateFilter().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange();
#else
        invalidateFilter();
#endif
    }
}

QString ReleaseManager::filterText() const
{
    return m_filterText;
}

void ReleaseManager::setFilterText(const QString &o)
{
    if (m_filterText != o) {
        m_filterText = o;
        Q_EMIT filterTextChanged();
        // Qt >= 6.10: beginFilterChange()/endFilterChange() replace invalidateFilter().
        // Older Qt (6.6-6.9: Fedora 41, KDE flatpak runtime) only has invalidateFilter().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange();
#else
        invalidateFilter();
#endif
    }
}

int ReleaseManager::filterSource() const
{
    return m_filterSource;
}

void ReleaseManager::setFilterSource(int source)
{
    if (m_filterSource != source) {
        m_filterSource = source;
        Q_EMIT filterSourceChanged();
        Q_EMIT firstSourceChanged();
        // Qt >= 6.10: beginFilterChange()/endFilterChange() replace invalidateFilter().
        // Older Qt (6.6-6.9: Fedora 41, KDE flatpak runtime) only has invalidateFilter().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange();
#else
        invalidateFilter();
#endif
    }
}

int ReleaseManager::firstSource() const
{
    for (int i = 0; i < m_sourceModel->rowCount(); i++) {
        Release *r = m_sourceModel->get(i);
        if (r->source() == m_filterSource)
            return i;
    }
    return 0;
}

void ReleaseManager::selectLocalFile(const QString &path)
{
    mDebug() << this->metaObject()->className() << "Setting local file to" << path;
    for (int i = 0; i < m_sourceModel->rowCount(); i++) {
        Release *r = m_sourceModel->get(i);
        if (r->source() == Release::LOCAL) {
            r->setLocalFile(path);
            setSelectedIndex(i);
            Q_EMIT localFileChanged();
        }
    }
}

ReleaseVariant *ReleaseManager::localFile() const
{
    for (int i = 0; i < m_sourceModel->rowCount(); i++) {
        Release *r = m_sourceModel->get(i);
        if (r->source() == Release::LOCAL) {
            return r->selectedVersion()->selectedVariant();
        }
    }

    return nullptr;
}

bool ReleaseManager::updateUrl(const QString &release,
                               int version,
                               const QString &status,
                               const QString &type,
                               const QString &category,
                               const QDateTime &releaseDate,
                               const QString &architecture,
                               const QString &url,
                               const QString &sha256,
                               int64_t size)
{
    if (!ReleaseArchitecture::isKnown(architecture)) {
        mDebug() << "Architecture" << architecture << "is not known!";
        return false;
    }

    // Prefer exact subvariant match to avoid ambiguity with generic names like "atomic"
    for (int i = 0; i < m_sourceModel->rowCount(); i++) {
        Release *r = get(i);
        if (r->subvariant().toLower() == release || r->subvariant().toLower() == category) {
            return r->updateUrl(version, status, type, releaseDate, architecture, url, sha256, size);
        }
    }

    // Fall back to fuzzy contains match
    for (int i = 0; i < m_sourceModel->rowCount(); i++) {
        Release *r = get(i);
        if (r->name().toLower().contains(release) || r->subvariant().toLower().contains(release)) {
            // Special case for Sway and Budgie
            if (release == "sway"_L1 || release == "budgie"_L1 || release == "cosmic"_L1) {
                if (r->source() == Release::EMERGING && (category != "sericea"_L1 && category != "onyx"_L1 && category != "cosmic-atomic"_L1))
                    continue;
            }
            return r->updateUrl(version, status, type, releaseDate, architecture, url, sha256, size);
        }
    }
    return false;
}

int ReleaseManager::filterArchitecture() const
{
    return m_filterArchitecture;
}

void ReleaseManager::setFilterArchitecture(int o)
{
    if (m_filterArchitecture != o && m_filterArchitecture >= 0 && m_filterArchitecture < ReleaseArchitecture::_ARCHCOUNT) {
        m_filterArchitecture = o;
        Q_EMIT filterArchitectureChanged();
        for (int i = 0; i < m_sourceModel->rowCount(); i++) {
            Release *r = get(i);
            for (auto v : r->versionList()) {
                int j = 0;
                for (auto variant : v->variantList()) {
                    if (variant->arch()->index() == o) {
                        v->setSelectedVariantIndex(j);
                        break;
                    }
                    j++;
                }
            }
        }
        // Qt >= 6.10: beginFilterChange()/endFilterChange() replace invalidateFilter().
        // Older Qt (6.6-6.9: Fedora 41, KDE flatpak runtime) only has invalidateFilter().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange();
#else
        invalidateFilter();
#endif
    }
}

Release *ReleaseManager::selected() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_sourceModel->rowCount())
        return m_sourceModel->get(m_selectedIndex);
    return nullptr;
}

int ReleaseManager::selectedIndex() const
{
    return m_selectedIndex;
}

void ReleaseManager::setSelectedIndex(int o)
{
    if (m_selectedIndex != o) {
        m_selectedIndex = o;
        Q_EMIT selectedChanged();
    }
}

ReleaseVariant *ReleaseManager::variant()
{
    if (selected()) {
        if (selected()->selectedVersion()) {
            if (selected()->selectedVersion()->selectedVariant()) {
                return selected()->selectedVersion()->selectedVariant();
            }
        }
    }
    return nullptr;
}

void ReleaseManager::onStringDownloaded(const QString &text)
{
    mDebug() << this->metaObject()->className() << "Received release data from" << m_lastFetchUrl;

    // 1. Structured JSON manifest - the preferred source (sha256 + size included)
    QJsonParseError jsonError;
    auto doc = QJsonDocument::fromJson(text.toUtf8(), &jsonError);
    if (jsonError.error == QJsonParseError::NoError && doc.isArray()) {
        processJson(text);
        resetFetchState();
        return;
    }

    // 2. SHA256SUMS sidecar response - enrich the stored directory listing with
    //    real checksums, then process it
    if (m_lastFetchUrl.endsWith("SHA256SUMS"_L1) && !m_pendingHtml.isEmpty()) {
        parseSha256Sums(text);
        processHtmlListing(m_pendingHtml);
        resetFetchState();
        return;
    }

    // 3. Non-JSON response. If it carries ISO links it IS the directory listing;
    //    otherwise it is an error page (e.g. a 404 for a missing manifest) and we
    //    fetch the actual listing. Either way, we then fetch SHA256SUMS so that
    //    downloads discovered from the listing can still be verified.
    if (text.contains(".iso"_L1) || m_lastFetchUrl == options.releasesDir) {
        m_pendingHtml = text;
        m_shaSums.clear();
        m_lastFetchUrl = options.releasesDir + "SHA256SUMS"_L1;
        mDebug() << this->metaObject()->className() << "Fetching" << m_lastFetchUrl;
        DownloadManager::instance()->fetchPageAsync(this, m_lastFetchUrl);
    } else {
        mWarning() << "Release data from" << m_lastFetchUrl << "is not a JSON manifest; falling back to the directory listing";
        m_lastFetchUrl = options.releasesDir;
        DownloadManager::instance()->fetchPageAsync(this, m_lastFetchUrl);
    }
}

void ReleaseManager::onDownloadError(const QString &message)
{
    mWarning() << "Was not able to fetch release data from" << m_lastFetchUrl << ":" << message;

    // SHA256SUMS fetch failed - process the stored listing without checksums
    // rather than losing the releases we already discovered
    if (m_lastFetchUrl.endsWith("SHA256SUMS"_L1) && !m_pendingHtml.isEmpty()) {
        mWarning() << "SHA256SUMS unavailable; processing directory listing without checksums";
        processHtmlListing(m_pendingHtml);
        resetFetchState();
        return;
    }

    // Manifest fetch failed - fall back to the HTML directory listing
    if (m_lastFetchUrl == options.releasesUrl && m_pendingHtml.isEmpty()) {
        mDebug() << "Manifest unavailable; falling back to the directory listing";
        m_lastFetchUrl = options.releasesDir;
        DownloadManager::instance()->fetchPageAsync(this, m_lastFetchUrl);
        return;
    }

    // The whole chain failed - retry a bounded number of times, then settle on
    // the embedded releases.json so the app stays usable offline
    if (m_fetchAttempts < MAX_FETCH_ATTEMPTS) {
        mWarning() << "Retrying release fetch in 10 seconds (attempt" << m_fetchAttempts << "of" << MAX_FETCH_ATTEMPTS << ")";
        QTimer::singleShot(10000, this, SLOT(fetchReleases()));
    } else {
        mWarning() << "Giving up on fetching releases after" << MAX_FETCH_ATTEMPTS << "attempts; using embedded release data";
        resetFetchState();
    }
}

void ReleaseManager::processJson(const QString &text)
{
    QJsonParseError jsonError;
    auto doc = QJsonDocument::fromJson(text.toUtf8(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isArray())
        return;

    QRegularExpression re("(\\d+)\\s?(\\S+)?");
    for (auto i : doc.array()) {
        QJsonObject obj = i.toObject();
        QString arch = obj["arch"].toString().toLower();
        QString url = obj["link"].toString();
        QString category = obj["variant"].toString().toLower();
        QString release = obj["subvariant"].toString().toLower();
        QString versionWithStatus = obj["version"].toString().toLower();
        QString sha256 = obj["sha256"].toString();
        QString type = "live";
        QDateTime releaseDate = QDateTime::fromString((obj["releaseDate"].toString()), "yyyy-MM-dd");
        int64_t size = obj["size"].toString().toLongLong();

        if (QStringList{"cloud", "cloud_base", "everything", "minimal", "docker", "docker_base"}.contains(release))
            continue;
        if (!url.endsWith("iso") || url.contains("-osb-") || url.contains("-provisioner-"))
            continue;
        if (!re.match(versionWithStatus).hasMatch())
            continue;

        int version = re.match(versionWithStatus).captured(1).toInt();
        QString status = re.match(versionWithStatus).captured(2);

        mDebug() << this->metaObject()->className() << "Adding" << release << versionWithStatus << arch;
        if (!release.isEmpty() && !url.isEmpty() && !arch.isEmpty())
            updateUrl(release, version, status, type, category, releaseDate, arch, url, sha256, size);
    }
}

void ReleaseManager::processHtmlListing(const QString &html)
{
    // Apache-style directory listing. Row layout:
    //   <td><a href="AcreetionOS-1.0-x86_64.iso">AcreetionOS-1.0-x86_64.iso</a></td>
    //   <td align="right">2026-08-09 14:34</td><td align="right">2.2G</td>
    // Enriched regex: captures filename, date cell, size cell.
    QRegularExpression isoRe("<a\\s+href=\"([^\"]+\\.iso)\"[^>]*>[^<]*</a>\\s*</td>\\s*<td[^>]*>\\s*([^<]*?)\\s*</td>\\s*<td[^>]*>\\s*([^<]*?)\\s*</td>");
    QRegularExpression looseRe("<a\\s+href=\"([^\"]+\\.iso)\"[^>]*>");
    const QRegularExpression fileRe("^(.+?)[-_]([\\d.]+)-([\\w_]+)\\.iso$");

    auto addIso = [&](const QString &filename, const QString &sha256, qint64 size, const QDateTime &releaseDate) {
        if (filename.contains('/') || filename.startsWith('?'))
            return;
        QString url = options.releasesDir + filename;

        auto fileMatch = fileRe.match(filename);
        if (!fileMatch.hasMatch())
            return;

        QString release = fileMatch.captured(1).toLower();
        QString versionStr = fileMatch.captured(2);
        QString arch = fileMatch.captured(3).toLower();
        if (release.isEmpty() || arch.isEmpty())
            return;

        int version = versionStr.split(".").first().toInt();

        // Official editions: Cinnamon, XL (XLibre)
        // Community editions: everything else (KDE, Xfce, Gnome spins, etc.)
        QString category = "product";
        if (release.contains("_kde") || release.contains("_xfce") || release.contains("_gnome") || release.contains("_spin") || release.contains("_community") || release.contains("_lxqt") || release.contains("_mate")
            || release.contains("_budgie") || release.contains("_sway"))
            category = "spins";

        mDebug() << this->metaObject()->className() << "Adding (from dir)" << release << versionStr << arch << "category:" << category << "sha256:" << (sha256.isEmpty() ? "none" : "present") << "size:" << size;
        updateUrl(release, version, QString(), "live", category, releaseDate, arch, url, sha256, size);
    };

    int enriched = 0;
    auto it = isoRe.globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        const QString filename = match.captured(1);
        const QDateTime releaseDate = QDateTime::fromString(match.captured(2).trimmed(), "yyyy-MM-dd HH:mm");
        const qint64 size = parseApacheSize(match.captured(3));
        addIso(filename, m_shaSums.value(filename), size, releaseDate);
        enriched++;
    }

    // Loose fallback for servers with a different row layout: never lose discovery
    if (enriched == 0) {
        mDebug() << this->metaObject()->className() << "Row layout not recognized; using loose ISO discovery";
        auto it2 = looseRe.globalMatch(html);
        while (it2.hasNext()) {
            const QString filename = it2.next().captured(1);
            addIso(filename, m_shaSums.value(filename), 0, QDateTime());
        }
    }
}

void ReleaseManager::parseSha256Sums(const QString &text)
{
    m_shaSums.clear();
    const QRegularExpression hex64("^[0-9a-fA-F]{64}$");
    for (const QString &rawLine : text.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() < 2)
            continue;
        // Support both "hash  filename" (sha256sum) and "filename  hash" (BSD) layouts
        QString hash = parts.value(0);
        QString name = parts.value(1);
        if (!hex64.match(hash).hasMatch() && hex64.match(name).hasMatch())
            std::swap(hash, name);
        if (!hex64.match(hash).hasMatch())
            continue;
        // Strip "sha256sum -c" prefixes such as "./" or "*" that some tools emit
        m_shaSums.insert(QFileInfo(name).fileName(), hash.toLower());
    }
    mDebug() << this->metaObject()->className() << "Parsed" << m_shaSums.size() << "checksum(s) from SHA256SUMS";
}

qint64 ReleaseManager::parseApacheSize(const QString &cell) const
{
    const QString s = cell.trimmed();
    if (s.isEmpty())
        return 0;
    const QRegularExpression re("^([\\d.]+)\\s*([KMG]?)$");
    auto m = re.match(s);
    if (!m.hasMatch())
        return s.toLongLong();
    double v = m.captured(1).toDouble();
    const QString unit = m.captured(2);
    if (unit == "K")
        v *= 1024;
    else if (unit == "M")
        v *= 1024 * 1024;
    else if (unit == "G")
        v *= 1024.0 * 1024 * 1024;
    return static_cast<qint64>(v);
}

void ReleaseManager::resetFetchState()
{
    m_lastFetchUrl.clear();
    m_pendingHtml.clear();
    m_shaSums.clear();
    m_fetchAttempts = 0;
    m_beingUpdated = false;
    Q_EMIT beingUpdatedChanged();
}

QStringList ReleaseManager::architectures() const
{
    return ReleaseArchitecture::listAllDescriptions();
}

QVariant ReleaseListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);

    if (role == ReleaseRole)
        return "release";
    if (role == SourceIndexRole)
        return "sourceIndex";
    if (role == Qt::DisplayRole)
        return "name";

    return QVariant();
}

QHash<int, QByteArray> ReleaseListModel::roleNames() const
{
    QHash<int, QByteArray> ret;
    ret.insert(ReleaseRole, "release");
    ret.insert(SourceIndexRole, "sourceIndex");
    ret.insert(Qt::DisplayRole, "name");
    return ret;
}

int ReleaseListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_releases.count();
}

QVariant ReleaseListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role == ReleaseRole)
        return QVariant::fromValue(m_releases[index.row()]);
    else if (role == SourceIndexRole)
        return QVariant::fromValue(QString::number(index.row()));
    else if (role == Qt::DisplayRole)
        return m_releases[index.row()]->name();

    return QVariant();
}

ReleaseListModel::ReleaseListModel(ReleaseManager *parent)
    : QAbstractListModel(parent)
{
    QFile metadata(":/metadata.json");
    if (!metadata.open(QIODevice::ReadOnly)) {
        return;
    }

    Release *custom = nullptr;

    auto doc = QJsonDocument::fromJson(metadata.readAll());
    for (auto i : doc.array()) {
        QJsonObject obj = i.toObject();
        QString subvariant = obj["subvariant"].toString();
        QString sourceString = obj["category"].toString();
        Release::Source source = sourceString == "product" ? Release::PRODUCT : sourceString == "spins" ? Release::SPINS : sourceString == "labs" ? Release::LABS : sourceString == "emerging" ? Release::EMERGING : Release::OTHER;
        QString name = obj["name"].toString();
        QString summary = obj["summary"].toString();
        QStringList description;
        for (auto j : obj["description"].toArray()) {
            description.append(j.toString());
        }
        QStringList screenshots;
        for (auto j : obj["screenshots"].toArray()) {
            screenshots.append(j.toString());
        }
        QString icon = "qrc:/logos/placeholder";
        if (obj.contains("icon"))
            icon = obj["icon"].toString();

        m_releases.append(new Release(manager(), m_releases.count(), name, summary, description, subvariant, source, icon, screenshots));
    }

    custom = new Release(manager(),
                         m_releases.count(),
                         tr("Custom image"),
                         QT_TRANSLATE_NOOP("Release", "Pick a file from your drive(s)"),
                         {QT_TRANSLATE_NOOP("Release", "<p>Here you can choose a OS image from your hard drive to be written to your flash disk</p><p>Currently it is only supported to write raw disk images (.iso or .bin)</p>")},
                         "Other"_L1,
                         Release::LOCAL,
                         "qrc:/logos/folder",
                         {});
    m_releases.append(custom);

    ReleaseVersion *customVersion = new ReleaseVersion(custom, 0);
    custom->addVersion(customVersion);
    customVersion->addVariant(new ReleaseVariant(customVersion, QString(), QString(), 0, ReleaseArchitecture::fromId(ReleaseArchitecture::X86_64)));
}

ReleaseManager *ReleaseListModel::manager()
{
    return qobject_cast<ReleaseManager *>(parent());
}

Release *ReleaseListModel::get(int index)
{
    if (index >= 0 && index < m_releases.count())
        return m_releases[index];
    return nullptr;
}

QString Release::sourceString()
{
    switch (m_source) {
    case LOCAL:
    case PRODUCT:
        return QString();
    case SPINS:
    case LABS:
    case EMERGING:
        return tr("Community Editions");
    default:
        return tr("Other");
    }
}

int Release::index() const
{
    return m_index;
}

Release::Release(ReleaseManager *parent, int index, const QString &name, const QString &summary, const QStringList &description, const QString &subvariant, Release::Source source, const QString &icon, const QStringList &screenshots)
    : QObject(parent)
    , m_index(index)
    , m_name(name)
    , m_summary(summary)
    , m_description(description)
    , m_subvariant(subvariant)
    , m_source(source)
    , m_icon(icon)
    , m_screenshots(screenshots)
{
    connect(this, SIGNAL(selectedVersionChanged()), parent, SLOT(variantChangedFilter()));
}

void Release::setLocalFile(const QString &path)
{
    if (m_source != LOCAL)
        return;
    QFileInfo info(QUrl(path).toLocalFile());

    // We might use an empty path to just reset local file
    if (!info.exists() && !path.isEmpty()) {
        mCritical() << path << "doesn't exist";
        return;
    }

    if (m_versions.count() == 1) {
        m_versions.first()->deleteLater();
        m_versions.removeFirst();
    }

    m_versions.append(new ReleaseVersion(this, QUrl(path).toLocalFile(), info.size()));
    Q_EMIT versionsChanged();
    Q_EMIT selectedVersionChanged();
}

bool Release::updateUrl(int version, const QString &status, const QString &type, const QDateTime &releaseDate, const QString &architecture, const QString &url, const QString &sha256, int64_t size)
{
    int finalVersions = 0;
    for (auto i : m_versions) {
        if (i->number() == version)
            return i->updateUrl(status, type, releaseDate, architecture, url, sha256, size);
        if (i->status() == ReleaseVersion::FINAL)
            finalVersions++;
    }
    ReleaseVersion::Status s = status == "alpha" ? ReleaseVersion::ALPHA : status == "beta" ? ReleaseVersion::BETA : ReleaseVersion::FINAL;
    auto ver = new ReleaseVersion(this, version, s, releaseDate);
    auto variant = new ReleaseVariant(ver, url, sha256, size, ReleaseArchitecture::fromAbbreviation(architecture));
    ver->addVariant(variant);
    addVersion(ver);
    if (ver->status() == ReleaseVersion::FINAL)
        finalVersions++;
    if (finalVersions > 2) {
        int min = INT32_MAX;
        ReleaseVersion *oldVer = nullptr;
        for (auto i : m_versions) {
            if (i->number() < min) {
                min = i->number();
                oldVer = i;
            }
        }
        removeVersion(oldVer);
    }
    return true;
}

ReleaseManager *Release::manager()
{
    return qobject_cast<ReleaseManager *>(parent());
}

QString Release::name() const
{
    return m_name;
}

QString Release::summary() const
{
    return tr(m_summary.toUtf8());
}

QString Release::description() const
{
    QString result;
    for (auto i : m_description) {
        // there is a %(rel)s formatting string in the translation texts, get rid of that
        // get rid of in-translation break tags too
        result.append(tr(i.toUtf8()).replace("\%(rel)s ", "").replace("<br />", ""));
    }
    return result;
}

QString Release::subvariant() const
{
    return m_subvariant;
}

Release::Source Release::source() const
{
    return m_source;
}

bool Release::isLocal() const
{
    return m_source == Release::LOCAL;
}

QString Release::icon() const
{
    return m_icon;
}

QStringList Release::screenshots() const
{
    return m_screenshots;
}

QString Release::prerelease() const
{
    if (m_versions.empty() || m_versions.first()->status() == ReleaseVersion::FINAL)
        return "";
    return m_versions.first()->name();
}

QQmlListProperty<ReleaseVersion> Release::versions()
{
    return QQmlListProperty<ReleaseVersion>(this, &m_versions);
}

QList<ReleaseVersion *> Release::versionList() const
{
    return m_versions;
}

QStringList Release::versionNames() const
{
    QStringList ret;
    for (auto i : m_versions) {
        ret.append(i->name());
    }
    return ret;
}

void Release::addVersion(ReleaseVersion *version)
{
    for (int i = 0; i < m_versions.count(); i++) {
        if (m_versions[i]->number() < version->number()) {
            m_versions.insert(i, version);
            Q_EMIT versionsChanged();
            if (version->status() != ReleaseVersion::FINAL && m_selectedVersion >= i) {
                m_selectedVersion++;
            }
            Q_EMIT selectedVersionChanged();
            return;
        }
    }
    m_versions.append(version);
    Q_EMIT versionsChanged();
    Q_EMIT selectedVersionChanged();
}

void Release::removeVersion(ReleaseVersion *version)
{
    int idx = m_versions.indexOf(version);
    if (!version || idx < 0)
        return;

    if (m_selectedVersion == idx) {
        m_selectedVersion = 0;
        Q_EMIT selectedVersionChanged();
    }
    m_versions.removeAt(idx);
    version->deleteLater();
    Q_EMIT versionsChanged();
}

ReleaseVersion *Release::selectedVersion() const
{
    if (m_selectedVersion >= 0 && m_selectedVersion < m_versions.count())
        return m_versions[m_selectedVersion];
    return nullptr;
}

int Release::selectedVersionIndex() const
{
    return m_selectedVersion;
}

void Release::setSelectedVersionIndex(int o)
{
    if (m_selectedVersion != o && m_selectedVersion >= 0 && m_selectedVersion < m_versions.count()) {
        m_selectedVersion = o;
        Q_EMIT selectedVersionChanged();
    }
}

ReleaseVersion::ReleaseVersion(Release *parent, int number, ReleaseVersion::Status status, QDateTime releaseDate)
    : QObject(parent)
    , m_number(number)
    , m_status(status)
    , m_releaseDate(releaseDate)
{
    if (status != FINAL)
        Q_EMIT parent->prereleaseChanged();
    connect(this, SIGNAL(selectedVariantChanged()), parent->manager(), SLOT(variantChangedFilter()));
}

ReleaseVersion::ReleaseVersion(Release *parent, const QString &file, int64_t size)
    : QObject(parent)
    , m_variants({new ReleaseVariant(this, file, size)})
{
    connect(this, SIGNAL(selectedVariantChanged()), parent->manager(), SLOT(variantChangedFilter()));
}

Release *ReleaseVersion::release()
{
    return qobject_cast<Release *>(parent());
}

const Release *ReleaseVersion::release() const
{
    return qobject_cast<const Release *>(parent());
}

bool ReleaseVersion::updateUrl(const QString &status, const QString &type, const QDateTime &releaseDate, const QString &architecture, const QString &url, const QString &sha256, int64_t size)
{
    // first determine and eventually update the current alpha/beta/final level of this version
    Status s = status == "alpha" ? ALPHA : status == "beta" ? BETA : FINAL;
    if (s <= m_status) {
        m_status = s;
        Q_EMIT statusChanged();
        if (s == FINAL)
            Q_EMIT release()->prereleaseChanged();
    } else {
        // return if it got downgraded in the meantime
        return false;
    }
    // update release date
    if (m_releaseDate != releaseDate && releaseDate.isValid()) {
        m_releaseDate = releaseDate;
        Q_EMIT releaseDateChanged();
    }
    // determine what type of release it is
    ReleaseVariant::Type t = type == "atomic" ? ReleaseVariant::ATOMIC : type == "netinst" || type == "netinstall" ? ReleaseVariant::NETINSTALL : type == "full" ? ReleaseVariant::FULL : ReleaseVariant::LIVE;
    for (auto i : m_variants) {
        if (i->arch() == ReleaseArchitecture::fromAbbreviation(architecture) && i->type() == t)
            return i->updateUrl(url, sha256, size);
    }
    // preserve the order from the ReleaseArchitecture::Id enum (to not have ARM first, etc.)
    // it's actually an array so comparing pointers is fine
    int order = 0;
    for (auto i : m_variants) {
        if (i->type() >= t && i->arch() > ReleaseArchitecture::fromAbbreviation(architecture))
            break;
        order++;
    }
    m_variants.insert(order, new ReleaseVariant(this, url, sha256, size, ReleaseArchitecture::fromAbbreviation(architecture), t));
    return true;
}

int ReleaseVersion::number() const
{
    return m_number;
}

QString ReleaseVersion::name() const
{
    switch (m_status) {
    case ALPHA:
        return tr("%1 Alpha").arg(m_number);
    case BETA:
        return tr("%1 Beta").arg(m_number);
    case RELEASE_CANDIDATE:
        return tr("%1 Release Candidate").arg(m_number);
    default:
        return QString("%1").arg(m_number);
    }
}

ReleaseVariant *ReleaseVersion::selectedVariant() const
{
    if (m_selectedVariant >= 0 && m_selectedVariant < m_variants.count())
        return m_variants[m_selectedVariant];
    return nullptr;
}

int ReleaseVersion::selectedVariantIndex() const
{
    return m_selectedVariant;
}

void ReleaseVersion::setSelectedVariantIndex(int o)
{
    if (m_selectedVariant != o && m_selectedVariant >= 0 && m_selectedVariant < m_variants.count()) {
        m_selectedVariant = o;
        Q_EMIT selectedVariantChanged();
    }
}

ReleaseVersion::Status ReleaseVersion::status() const
{
    return m_status;
}

QDateTime ReleaseVersion::releaseDate() const
{
    return m_releaseDate;
}

void ReleaseVersion::addVariant(ReleaseVariant *v)
{
    m_variants.append(v);
    Q_EMIT variantsChanged();
    if (m_variants.count() == 1)
        Q_EMIT selectedVariantChanged();
}

QQmlListProperty<ReleaseVariant> ReleaseVersion::variants()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    return QQmlListProperty<ReleaseVariant>(this, &m_variants);
#else
    return QQmlListProperty<ReleaseVariant>(this, m_variants);
#endif
}

QList<ReleaseVariant *> ReleaseVersion::variantList() const
{
    return m_variants;
}

ReleaseVariant::ReleaseVariant(ReleaseVersion *parent, QString url, QString shaHash, int64_t size, ReleaseArchitecture *arch, ReleaseVariant::Type type)
    : QObject(parent)
    , m_arch(arch)
    , m_type(type)
    , m_url(url)
    , m_shaHash(shaHash)
    , m_size(size)
{
    connect(this, &ReleaseVariant::sizeChanged, this, &ReleaseVariant::realSizeChanged);
}

ReleaseVariant::ReleaseVariant(ReleaseVersion *parent, const QString &file, int64_t size)
    : QObject(parent)
    , m_iso(file)
    , m_arch(ReleaseArchitecture::fromId(ReleaseArchitecture::X86_64))
    , m_size(size)
{
    connect(this, &ReleaseVariant::sizeChanged, this, &ReleaseVariant::realSizeChanged);
    m_status = READY;
}

bool ReleaseVariant::updateUrl(const QString &url, const QString &sha256, int64_t size)
{
    bool changed = false;
    if (!url.isEmpty() && m_url.toUtf8().trimmed() != url.toUtf8().trimmed()) {
        mWarning() << "Url" << m_url << "changed to" << url;
        m_url = url;
        Q_EMIT urlChanged();
        changed = true;
    }
    if (!sha256.isEmpty() && m_shaHash.trimmed() != sha256.trimmed()) {
        mWarning() << "SHA256 hash of" << url << "changed from" << m_shaHash << "to" << sha256;
        m_shaHash = sha256;
        Q_EMIT shaHashChanged();
        changed = true;
    }
    if (size != 0 && m_size != size) {
        m_size = size;
        Q_EMIT sizeChanged();
        changed = true;
    }
    return changed;
}

ReleaseVersion *ReleaseVariant::releaseVersion()
{
    return qobject_cast<ReleaseVersion *>(parent());
}

const ReleaseVersion *ReleaseVariant::releaseVersion() const
{
    return qobject_cast<const ReleaseVersion *>(parent());
}

Release *ReleaseVariant::release()
{
    return releaseVersion()->release();
}

const Release *ReleaseVariant::release() const
{
    return releaseVersion()->release();
}

ReleaseArchitecture *ReleaseVariant::arch() const
{
    return m_arch;
}

ReleaseVariant::Type ReleaseVariant::type() const
{
    return m_type;
}

QString ReleaseVariant::name() const
{
    if (type() == ATOMIC)
        return m_arch->description() + " - Atomic";
    else if (type() == FULL)
        return m_arch->description() + " - Full Image";
    else if (type() == NETINSTALL)
        return m_arch->description() + " - Net Install";
    else
        return m_arch->description();
}

QString ReleaseVariant::fullName()
{
    if (release()->isLocal())
        return QFileInfo(iso()).fileName();
    else
        return QString("%1 %2 %3").arg(release()->name()).arg(releaseVersion()->name()).arg(name());
}

QString ReleaseVariant::url() const
{
    return m_url;
}

QString ReleaseVariant::shaHash() const
{
    return m_shaHash;
}

QString ReleaseVariant::iso() const
{
    return m_iso;
}

QString ReleaseVariant::temporaryPath() const
{
    return m_temporaryIso;
}

qreal ReleaseVariant::size() const
{
    return m_size;
}

qreal ReleaseVariant::realSize() const
{
    if (m_realSize <= 0)
        return m_size;
    return m_realSize;
}

Progress *ReleaseVariant::progress()
{
    if (!m_progress)
        m_progress = new Progress(this, 0.0, size());

    return m_progress;
}

void ReleaseVariant::setRealSize(qint64 o)
{
    if (m_realSize != o) {
        m_realSize = o;
        Q_EMIT realSizeChanged();
    }
}

ReleaseVariant::Status ReleaseVariant::status() const
{
    if (m_status == READY && DriveManager::instance()->isBackendBroken())
        return WRITING_NOT_POSSIBLE;
    return m_status;
}

QString ReleaseVariant::statusString() const
{
    return m_statusStrings[status()];
}

void ReleaseVariant::onFileDownloaded(const QString &path, const QString &hash)
{
    m_temporaryIso = QString();

    if (m_progress)
        m_progress->setValue(size());
    setStatus(DOWNLOAD_VERIFYING);
    m_progress->setValue(0.0, 1.0);

    if (!shaHash().isEmpty() && shaHash() != hash) {
        mWarning() << "Computed SHA256 hash of" << path << " - " << hash << "does not match expected" << shaHash();
        setErrorString(tr("The downloaded image is corrupted"));
        setStatus(FAILED_DOWNLOAD);
        return;
    }
    mDebug() << this->metaObject()->className() << "SHA256 check passed";

    qApp->eventDispatcher()->processEvents(QEventLoop::AllEvents);

    int checkResult = mediaCheckFile(QDir::toNativeSeparators(path).toLocal8Bit(), &ReleaseVariant::staticOnMediaCheckAdvanced, this);
    if (checkResult == ISOMD5SUM_CHECK_FAILED) {
        mWarning() << "Internal MD5 media check of" << path << "failed with status" << checkResult;
        QFile::remove(path);
        setErrorString(tr("The downloaded image is corrupted"));
        setStatus(FAILED_DOWNLOAD);
        return;
    } else if (checkResult == ISOMD5SUM_FILE_NOT_FOUND) {
        setErrorString(tr("The downloaded file is not readable."));
        setStatus(FAILED_DOWNLOAD);
        return;
    } else {
        mDebug() << this->metaObject()->className() << "MD5 check passed";
        QString finalFilename(path);
        finalFilename = finalFilename.replace(QRegularExpression("[.]part$"), "");

        if (finalFilename != path) {
            mDebug() << this->metaObject()->className() << "Renaming from" << path << "to" << finalFilename;
            if (!QFile::rename(path, finalFilename)) {
                setErrorString(tr("Unable to rename the temporary file."));
                setStatus(FAILED_DOWNLOAD);
                return;
            }
        }

        m_iso = finalFilename;
        Q_EMIT isoChanged();

        mDebug() << this->metaObject()->className() << "Image is ready";
        setStatus(READY);

        if (QFile(m_iso).size() != m_size) {
            m_size = QFile(m_iso).size();
            Q_EMIT sizeChanged();
        }
    }
}

void ReleaseVariant::onDownloadError(const QString &message)
{
    setErrorString(message);
    setStatus(FAILED_DOWNLOAD);
}

int ReleaseVariant::staticOnMediaCheckAdvanced(void *data, long long offset, long long total)
{
    ReleaseVariant *v = static_cast<ReleaseVariant *>(data);
    return v->onMediaCheckAdvanced(offset, total);
}

int ReleaseVariant::onMediaCheckAdvanced(long long offset, long long total)
{
    qApp->eventDispatcher()->processEvents(QEventLoop::AllEvents);
    m_progress->setValue(offset, total);
    return 0;
}

void ReleaseVariant::download()
{
    if (url().isEmpty() && !iso().isEmpty()) {
        setStatus(READY);
    } else {
        resetStatus();
        setStatus(DOWNLOADING);
        if (m_size)
            progress()->setTo(m_size);
        QString ret = DownloadManager::instance()->downloadFile(this, url(), DownloadManager::dir(), progress());
        if (!ret.endsWith(".part")) {
            m_temporaryIso = QString();
            m_iso = ret;
            Q_EMIT isoChanged();

            mDebug() << this->metaObject()->className() << m_iso << "is already downloaded";
            setStatus(READY);

            if (QFile(m_iso).size() != m_size) {
                m_size = QFile(m_iso).size();
                Q_EMIT sizeChanged();
            }
        } else {
            m_temporaryIso = ret;
        }
    }
}

void ReleaseVariant::resetStatus()
{
    if (!m_iso.isEmpty()) {
        setStatus(READY);
    } else {
        setStatus(PREPARING);
        if (m_progress)
            m_progress->setValue(0.0);
    }
    setErrorString(QString());
    Q_EMIT statusChanged();
}

bool ReleaseVariant::erase()
{
    if (QFile(m_iso).remove()) {
        mDebug() << this->metaObject()->className() << "Deleted" << m_iso;
        m_iso = QString();
        Q_EMIT isoChanged();
        return true;
    } else {
        mWarning() << this->metaObject()->className() << "An attempt to delete" << m_iso << "failed!";
        return false;
    }
}

void ReleaseVariant::setStatus(Status s)
{
    if (m_status != s) {
        m_status = s;
        Q_EMIT statusChanged();
    }
}

QString ReleaseVariant::errorString() const
{
    return m_error;
}

void ReleaseVariant::setErrorString(const QString &o)
{
    if (m_error != o) {
        m_error = o;
        Q_EMIT errorStringChanged();
    }
}

ReleaseArchitecture ReleaseArchitecture::m_all[] = {
    {{"x86_64"}, QT_TR_NOOP("Intel/AMD 64bit"), QT_TR_NOOP("ISO format image for Intel, AMD and other compatible PCs (64-bit)")},
    {{"x86", "i386", "i686"}, QT_TR_NOOP("Intel/AMD 32bit"), QT_TR_NOOP("ISO format image for Intel, AMD and other compatible PCs (32-bit)")},
    {{"armv7hl", "armhfp"}, QT_TR_NOOP("ARM v7"), QT_TR_NOOP("LZMA-compressed raw image for ARM v7-A machines like the Raspberry Pi 2 and 3")},
    {{"aarch64"}, QT_TR_NOOP("AArch64"), QT_TR_NOOP("LZMA-compressed raw image for AArch64 machines")},
};

ReleaseArchitecture::ReleaseArchitecture(const QStringList &abbreviation, const char *description, const char *details)
    : m_abbreviation(abbreviation)
    , m_description(description)
    , m_details(details)
{
}

ReleaseArchitecture *ReleaseArchitecture::fromId(ReleaseArchitecture::Id id)
{
    if (id >= 0 && id < _ARCHCOUNT)
        return &m_all[id];
    return nullptr;
}

ReleaseArchitecture *ReleaseArchitecture::fromAbbreviation(const QString &abbr)
{
    for (int i = 0; i < _ARCHCOUNT; i++) {
        if (m_all[i].abbreviation().contains(abbr, Qt::CaseInsensitive))
            return &m_all[i];
    }
    return nullptr;
}

bool ReleaseArchitecture::isKnown(const QString &abbr)
{
    for (int i = 0; i < _ARCHCOUNT; i++) {
        if (m_all[i].abbreviation().contains(abbr, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QList<ReleaseArchitecture *> ReleaseArchitecture::listAll()
{
    QList<ReleaseArchitecture *> ret;
    for (int i = 0; i < _ARCHCOUNT; i++) {
        ret.append(&m_all[i]);
    }
    return ret;
}

QStringList ReleaseArchitecture::listAllDescriptions()
{
    QStringList ret;
    for (int i = 0; i < _ARCHCOUNT; i++) {
        ret.append(m_all[i].description());
    }
    return ret;
}

QStringList ReleaseArchitecture::abbreviation() const
{
    return m_abbreviation;
}

QString ReleaseArchitecture::description() const
{
    return tr(m_description);
}

QString ReleaseArchitecture::details() const
{
    return tr(m_details);
}

int ReleaseArchitecture::index() const
{
    return this - m_all;
}

ReleaseArchitecture::Id ReleaseArchitecture::id() const
{
    return (Id)index();
}
