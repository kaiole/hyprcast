#include "Theme.hpp"

#include <toml++/toml.hpp>

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace Hyprcast::Overlay {
    namespace {
        using Table = toml::table;

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

        QString keyString(std::string_view key) {
            return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
        }

        bool hasOnlyKeys(const Table& table, std::initializer_list<std::string_view> allowed, const QString& prefix, QString* error) {
            for (const auto& [key, value] : table) {
                Q_UNUSED(value);
                const std::string_view candidate = key.str();
                if (std::find(allowed.begin(), allowed.end(), candidate) == allowed.end()) {
                    const QString name = prefix.isEmpty() ? keyString(candidate) : prefix + QLatin1Char('.') + keyString(candidate);
                    return fail(error, QStringLiteral("unknown theme manifest key '%1'").arg(name));
                }
            }
            return true;
        }

        const Table* readTable(const Table& table, std::string_view key, QString* error) {
            const auto* node = table.get(key);
            if (!node) {
                return nullptr;
            }
            const auto* child = node->as_table();
            if (!child) {
                fail(error, QStringLiteral("'%1' must be a TOML table").arg(keyString(key)));
                return nullptr;
            }
            return child;
        }

        bool readString(const Table& table, std::string_view key, QString* value, QString* error, bool required = false) {
            const auto* node = table.get(key);
            if (!node) {
                return !required || fail(error, QStringLiteral("missing required theme manifest key '%1'").arg(keyString(key)));
            }
            const auto parsed = node->value_exact<std::string>();
            if (!parsed) {
                return fail(error, QStringLiteral("'%1' must be a string").arg(keyString(key)));
            }
            *value = QString::fromUtf8(parsed->data(), static_cast<qsizetype>(parsed->size()));
            return true;
        }

        bool readInteger(const Table& table, std::string_view key, int* value, QString* error) {
            const auto* node = table.get(key);
            if (!node) {
                return fail(error, QStringLiteral("missing required theme manifest key '%1'").arg(keyString(key)));
            }
            const auto parsed = node->value_exact<std::int64_t>();
            if (!parsed || *parsed < std::numeric_limits<int>::min() || *parsed > std::numeric_limits<int>::max()) {
                return fail(error, QStringLiteral("'%1' must be an integer").arg(keyString(key)));
            }
            *value = static_cast<int>(*parsed);
            return true;
        }

        bool validOptionName(const QString& name) {
            static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,63}$"));
            return pattern.match(name).hasMatch();
        }

        bool validThemeId(const QString& id) {
            static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
            return pattern.match(id).hasMatch() && !id.startsWith(QStringLiteral("builtin."));
        }

        bool readTomlOptionValue(const toml::node& node, const QString& type, QVariant* value, QString* error, const QString& path) {
            if (type == QStringLiteral("boolean")) {
                const auto parsed = node.value_exact<bool>();
                if (!parsed) {
                    return fail(error, QStringLiteral("'%1' must be a TOML boolean").arg(path));
                }
                *value = *parsed;
                return true;
            }
            if (type == QStringLiteral("integer")) {
                const auto parsed = node.value_exact<std::int64_t>();
                if (!parsed) {
                    return fail(error, QStringLiteral("'%1' must be a TOML integer").arg(path));
                }
                *value = QVariant::fromValue<qlonglong>(*parsed);
                return true;
            }
            if (type == QStringLiteral("number")) {
                if (const auto parsed = node.value_exact<double>()) {
                    *value = *parsed;
                    return true;
                }
                if (const auto parsed = node.value_exact<std::int64_t>()) {
                    *value = static_cast<double>(*parsed);
                    return true;
                }
                return fail(error, QStringLiteral("'%1' must be a TOML number").arg(path));
            }
            const auto parsed = node.value_exact<std::string>();
            if (!parsed) {
                return fail(error, QStringLiteral("'%1' must be a TOML string").arg(path));
            }
            *value = QString::fromUtf8(parsed->data(), static_cast<qsizetype>(parsed->size()));
            return true;
        }

        bool isIntegerVariant(const QVariant& value) {
            const int type = value.metaType().id();
            return type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt || type == QMetaType::ULongLong;
        }

        bool validateOptionValue(const ThemeOption& option, const QVariant& value, QString* error, const QString& path) {
            if (option.type == QStringLiteral("boolean")) {
                if (value.metaType().id() != QMetaType::Bool) {
                    return fail(error, QStringLiteral("'%1' must be a boolean").arg(path));
                }
            } else if (option.type == QStringLiteral("integer")) {
                if (!isIntegerVariant(value)) {
                    return fail(error, QStringLiteral("'%1' must be an integer").arg(path));
                }
            } else if (option.type == QStringLiteral("number")) {
                if (!isIntegerVariant(value) && value.metaType().id() != QMetaType::Double) {
                    return fail(error, QStringLiteral("'%1' must be a number").arg(path));
                }
                if (!std::isfinite(value.toDouble())) {
                    return fail(error, QStringLiteral("'%1' must be finite").arg(path));
                }
            } else if (option.type == QStringLiteral("string")) {
                if (value.metaType().id() != QMetaType::QString || value.toString().size() > 4096 || value.toString().contains(QChar::Null)) {
                    return fail(error, QStringLiteral("'%1' must be a string no longer than 4096 characters").arg(path));
                }
            } else if (option.type == QStringLiteral("enum")) {
                if (value.metaType().id() != QMetaType::QString || !option.choices.contains(value.toString())) {
                    return fail(error, QStringLiteral("'%1' must be one of: %2").arg(path, option.choices.join(QStringLiteral(", "))));
                }
            } else if (option.type == QStringLiteral("color")) {
                if (value.metaType().id() != QMetaType::QString || !QColor(value.toString()).isValid()) {
                    return fail(error, QStringLiteral("'%1' must be a valid Qt color, for example '#ffffff'").arg(path));
                }
            }

            if (option.minimum || option.maximum) {
                const double numeric = value.toDouble();
                if (option.minimum && numeric < option.minimum->toDouble()) {
                    return fail(error, QStringLiteral("'%1' must be at least %2").arg(path, option.minimum->toString()));
                }
                if (option.maximum && numeric > option.maximum->toDouble()) {
                    return fail(error, QStringLiteral("'%1' must be at most %2").arg(path, option.maximum->toString()));
                }
            }
            return true;
        }

        QString readFileName(const QUrl& url) {
            if (url.isLocalFile()) {
                return url.toLocalFile();
            }
            if (url.scheme() == QStringLiteral("qrc")) {
                return QLatin1Char(':') + url.path();
            }
            return {};
        }

        QStringList componentErrors(const QQmlComponent& component) {
            QStringList errors;
            for (const auto& error : component.errors()) {
                errors.push_back(error.toString());
            }
            return errors;
        }

        bool pathIsRelativeToPackage(const QString& relativePath, const QString& packageRoot, QString* absolutePath, QString* error) {
            if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath) || relativePath.contains(QChar::Null)) {
                return fail(error, QStringLiteral("theme entry must be a non-empty relative path"));
            }
            const QString clean = QDir::cleanPath(relativePath);
            if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
                return fail(error, QStringLiteral("theme entry must stay inside its theme directory"));
            }
            const QString rootCanonical = QFileInfo(packageRoot).canonicalFilePath();
            const QString fileCanonical = QFileInfo(QDir(packageRoot).filePath(clean)).canonicalFilePath();
            if (rootCanonical.isEmpty() || fileCanonical.isEmpty() || !QFileInfo(fileCanonical).isFile() ||
                !(fileCanonical == rootCanonical || fileCanonical.startsWith(rootCanonical + QDir::separator()))) {
                return fail(error, QStringLiteral("theme entry '%1' is missing or resolves outside the theme directory").arg(relativePath));
            }
            *absolutePath = fileCanonical;
            return true;
        }
    } // namespace

    ThemeCatalog::ThemeCatalog(QStringList themeRoots, QUrl bundledManifest) : m_themeRoots(std::move(themeRoots)), m_bundledManifest(std::move(bundledManifest)) {}

    bool ThemeCatalog::discover(QStringList* warnings, QString* error) {
        if (warnings) {
            warnings->clear();
        }
        if (error) {
            error->clear();
        }
        m_themes.clear();
        m_rejected.clear();
        m_bundledDefault.reset();

        ThemeDescriptor bundled;
        QString         bundledError;
        if (!loadDescriptor(m_bundledManifest, true, {}, &bundled, &bundledError)) {
            return fail(error, QStringLiteral("bundled default theme is invalid: %1").arg(bundledError));
        }
        if (bundled.id != QStringLiteral("default")) {
            return fail(error, QStringLiteral("bundled default manifest must use the reserved id 'default'"));
        }
        m_bundledDefault = bundled;

        QSet<QString> claimedIds;
        for (const QString& rootPath : m_themeRoots) {
            const QDir root(rootPath);
            if (!root.exists()) {
                continue;
            }
            const QFileInfoList directories = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo& directory : directories) {
                const QString id = directory.fileName();
                if (!validThemeId(id)) {
                    if (warnings) {
                        warnings->push_back(QStringLiteral("Ignoring theme directory '%1': invalid theme ID").arg(directory.absoluteFilePath()));
                    }
                    continue;
                }
                if (claimedIds.contains(id)) {
                    if (warnings) {
                        warnings->push_back(QStringLiteral("Theme '%1' from '%2' is shadowed by a higher-precedence installation").arg(id, directory.absoluteFilePath()));
                    }
                    continue;
                }
                claimedIds.insert(id);

                const QString   manifestPath = QDir(directory.absoluteFilePath()).filePath(QStringLiteral("theme.toml"));
                ThemeDescriptor descriptor;
                QString         themeError;
                if (!loadDescriptor(QUrl::fromLocalFile(manifestPath), false, id, &descriptor, &themeError)) {
                    m_rejected.insert(id, themeError);
                    if (warnings) {
                        warnings->push_back(QStringLiteral("Ignoring theme directory '%1': %2").arg(directory.absoluteFilePath(), themeError));
                    }
                    continue;
                }
                m_themes.insert(descriptor.id, std::move(descriptor));
            }
        }

        if (!claimedIds.contains(bundled.id)) {
            m_themes.insert(bundled.id, bundled);
        } else if (warnings) {
            warnings->push_back(QStringLiteral("Bundled theme '%1' is shadowed; select 'builtin:default' to use it explicitly").arg(bundled.id));
        }
        return true;
    }

    bool ThemeCatalog::resolve(const QString& id, ThemeDescriptor* descriptor, QString* error) const {
        if (!descriptor) {
            return fail(error, QStringLiteral("internal error: null theme descriptor destination"));
        }
        if (id == QStringLiteral("builtin:default")) {
            if (!m_bundledDefault) {
                return fail(error, QStringLiteral("bundled default theme has not been discovered"));
            }
            *descriptor = *m_bundledDefault;
            return true;
        }
        const auto found = m_themes.constFind(id);
        if (found == m_themes.cend()) {
            const auto rejected = m_rejected.constFind(id);
            if (rejected != m_rejected.cend()) {
                return fail(error, QStringLiteral("theme '%1' could not be loaded: %2").arg(id, rejected.value()));
            }
            return fail(error, QStringLiteral("theme '%1' was not found; available themes: %2").arg(id, availableThemeIds().join(QStringLiteral(", "))));
        }
        *descriptor = found.value();
        return true;
    }

    bool ThemeCatalog::validateOptions(const QString& id, const QVariantMap& supplied, QVariantMap* effective, QString* error) const {
        ThemeDescriptor descriptor;
        if (!resolve(id, &descriptor, error)) {
            return false;
        }
        if (!effective) {
            return fail(error, QStringLiteral("internal error: null theme option destination"));
        }

        for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) {
            if (!descriptor.options.contains(it.key())) {
                return fail(error, QStringLiteral("unknown option 'theme.options.%1' for theme '%2'").arg(it.key(), id));
            }
        }

        QVariantMap values;
        for (auto it = descriptor.options.cbegin(); it != descriptor.options.cend(); ++it) {
            const ThemeOption& option = it.value();
            const QVariant     value  = supplied.contains(it.key()) ? supplied.value(it.key()) : option.defaultValue;
            if (!validateOptionValue(option, value, error, QStringLiteral("theme.options.%1").arg(it.key()))) {
                return false;
            }
            values.insert(it.key(), value);
        }
        *effective = std::move(values);
        return true;
    }

    QStringList ThemeCatalog::availableThemeIds() const {
        QStringList ids = m_themes.keys();
        if (m_bundledDefault) {
            ids.push_back(QStringLiteral("builtin:default"));
        }
        ids.removeDuplicates();
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    bool ThemeCatalog::loadDescriptor(const QUrl& manifestUrl, bool bundled, const QString& expectedId, ThemeDescriptor* descriptor, QString* error) const {
        if (!descriptor) {
            return fail(error, QStringLiteral("internal error: null theme manifest destination"));
        }
        const QString manifestFile = readFileName(manifestUrl);
        QFile         file(manifestFile);
        if (manifestFile.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            return fail(error, QStringLiteral("cannot read theme manifest '%1'").arg(manifestUrl.toString()));
        }
        if (file.size() > 256 * 1024) {
            return fail(error, QStringLiteral("theme manifest exceeds 256 KiB"));
        }
        const QByteArray contents = file.readAll();

        try {
            const Table parsed = toml::parse(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())), manifestUrl.toString().toStdString());
            if (!hasOnlyKeys(parsed, {"theme", "options"}, {}, error)) {
                return false;
            }
            const Table* theme = readTable(parsed, "theme", error);
            if (!theme) {
                return fail(error, QStringLiteral("theme manifest must contain a [theme] table"));
            }
            if (!hasOnlyKeys(*theme, {"id", "name", "api_version", "entry"}, QStringLiteral("theme"), error)) {
                return false;
            }

            ThemeDescriptor result;
            result.bundled = bundled;
            if (!readString(*theme, "id", &result.id, error, true)) {
                return false;
            }
            QString entry;
            if (!readString(*theme, "entry", &entry, error, true)) {
                return false;
            }
            if (entry.size() > 1024 || entry.contains(QChar::Null)) {
                return fail(error, QStringLiteral("theme.entry must be at most 1024 characters and contain no NUL"));
            }
            if (const auto* name = theme->get("name")) {
                if (!readString(*theme, "name", &result.name, error)) {
                    return false;
                }
            }
            if (result.name.isEmpty()) {
                result.name = result.id;
            }
            if (!readInteger(*theme, "api_version", &result.apiVersion, error)) {
                return false;
            }
            if (!validThemeId(result.id)) {
                return fail(error, QStringLiteral("theme id '%1' must match [a-z0-9][a-z0-9._-]{0,63} and not use the reserved 'builtin.' prefix").arg(result.id));
            }
            if (!expectedId.isEmpty() && result.id != expectedId) {
                return fail(error, QStringLiteral("manifest id '%1' does not match directory name '%2'").arg(result.id, expectedId));
            }
            if (result.apiVersion != 1) {
                return fail(error, QStringLiteral("theme '%1' requires presentation API version %2; this overlay supports version 1").arg(result.id).arg(result.apiVersion));
            }
            if (result.name.size() > 128 || result.name.contains(QChar::Null)) {
                return fail(error, QStringLiteral("theme.name must be at most 128 characters and contain no NUL"));
            }

            if (bundled) {
                const QUrl entryUrl = manifestUrl.resolved(QUrl(entry));
                if (!QFile::exists(readFileName(entryUrl))) {
                    return fail(error, QStringLiteral("bundled theme entry '%1' does not exist").arg(entry));
                }
                result.entryUrl = entryUrl;
            } else {
                const QString packageRoot = QFileInfo(manifestUrl.toLocalFile()).absolutePath();
                QString       absoluteEntry;
                if (!pathIsRelativeToPackage(entry, packageRoot, &absoluteEntry, error)) {
                    return false;
                }
                result.entryUrl = QUrl::fromLocalFile(absoluteEntry);
            }

            const Table* options = readTable(parsed, "options", error);
            if (options) {
                for (const auto& [rawName, node] : *options) {
                    const QString name = keyString(rawName.str());
                    if (!validOptionName(name)) {
                        return fail(error, QStringLiteral("theme option name '%1' must match [a-z][a-z0-9_]{0,63}").arg(name));
                    }
                    const Table* declaration = node.as_table();
                    if (!declaration) {
                        return fail(error, QStringLiteral("'options.%1' must be a TOML table").arg(name));
                    }
                    const QString prefix = QStringLiteral("options.%1").arg(name);
                    if (!hasOnlyKeys(*declaration, {"type", "default", "minimum", "maximum", "values", "description"}, prefix, error)) {
                        return false;
                    }
                    ThemeOption option;
                    if (!readString(*declaration, "type", &option.type, error, true)) {
                        return false;
                    }
                    option.type = option.type.toLower();
                    const QStringList supportedTypes{QStringLiteral("boolean"), QStringLiteral("integer"), QStringLiteral("number"),
                                                     QStringLiteral("string"),  QStringLiteral("enum"),    QStringLiteral("color")};
                    if (!supportedTypes.contains(option.type)) {
                        return fail(error, QStringLiteral("'%1.type' must be boolean, integer, number, string, enum, or color").arg(prefix));
                    }
                    const auto* defaultNode = declaration->get("default");
                    if (!defaultNode) {
                        return fail(error, QStringLiteral("missing required theme option default '%1.default'").arg(prefix));
                    }
                    if (!readTomlOptionValue(*defaultNode, option.type, &option.defaultValue, error, prefix + QStringLiteral(".default"))) {
                        return false;
                    }
                    if (const auto* description = declaration->get("description")) {
                        Q_UNUSED(description);
                        if (!readString(*declaration, "description", &option.description, error)) {
                            return false;
                        }
                    }
                    if (const auto* minimum = declaration->get("minimum")) {
                        if (option.type != QStringLiteral("integer") && option.type != QStringLiteral("number")) {
                            return fail(error, QStringLiteral("'%1.minimum' is only valid for integer and number options").arg(prefix));
                        }
                        QVariant value;
                        if (!readTomlOptionValue(*minimum, option.type, &value, error, prefix + QStringLiteral(".minimum"))) {
                            return false;
                        }
                        if (option.type == QStringLiteral("number") && !std::isfinite(value.toDouble())) {
                            return fail(error, QStringLiteral("'%1.minimum' must be finite").arg(prefix));
                        }
                        option.minimum = value;
                    }
                    if (const auto* maximum = declaration->get("maximum")) {
                        if (option.type != QStringLiteral("integer") && option.type != QStringLiteral("number")) {
                            return fail(error, QStringLiteral("'%1.maximum' is only valid for integer and number options").arg(prefix));
                        }
                        QVariant value;
                        if (!readTomlOptionValue(*maximum, option.type, &value, error, prefix + QStringLiteral(".maximum"))) {
                            return false;
                        }
                        if (option.type == QStringLiteral("number") && !std::isfinite(value.toDouble())) {
                            return fail(error, QStringLiteral("'%1.maximum' must be finite").arg(prefix));
                        }
                        option.maximum = value;
                    }
                    if (const auto* values = declaration->get("values")) {
                        if (option.type != QStringLiteral("enum")) {
                            return fail(error, QStringLiteral("'%1.values' is only valid for enum options").arg(prefix));
                        }
                        const auto* array = values->as_array();
                        if (!array || array->empty()) {
                            return fail(error, QStringLiteral("'%1.values' must be a non-empty array of strings").arg(prefix));
                        }
                        for (const auto& item : *array) {
                            const auto value = item.value_exact<std::string>();
                            if (!value) {
                                return fail(error, QStringLiteral("'%1.values' must contain only strings").arg(prefix));
                            }
                            option.choices.push_back(QString::fromUtf8(value->data(), static_cast<qsizetype>(value->size())));
                        }
                        const qsizetype choiceCount = option.choices.size();
                        option.choices.removeDuplicates();
                        if (option.choices.size() != choiceCount) {
                            return fail(error, QStringLiteral("'%1.values' must not contain duplicates").arg(prefix));
                        }
                    }
                    if (option.type == QStringLiteral("enum") && option.choices.isEmpty()) {
                        return fail(error, QStringLiteral("enum option '%1' must declare values").arg(name));
                    }
                    if (option.minimum && option.maximum && option.minimum->toDouble() > option.maximum->toDouble()) {
                        return fail(error, QStringLiteral("'%1.minimum' must not exceed maximum").arg(prefix));
                    }
                    if (!validateOptionValue(option, option.defaultValue, error, prefix + QStringLiteral(".default"))) {
                        return false;
                    }
                    if (option.description.size() > 1024 || option.description.contains(QChar::Null)) {
                        return fail(error, QStringLiteral("'%1.description' must be at most 1024 characters and contain no NUL").arg(prefix));
                    }
                    result.options.insert(name, std::move(option));
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }
            *descriptor = std::move(result);
            return true;
        } catch (const toml::parse_error& exception) { return fail(error, QString::fromUtf8(exception.what())); } catch (const std::exception& exception) {
            return fail(error, QString::fromUtf8(exception.what()));
        }
    }

    ThemeApi::ThemeApi(KeyboardPresenter& presenter, const OverlayConfig& config, QString themeId, QVariantMap options, QObject* parent) :
        QObject(parent), m_presenter(presenter), m_themeId(std::move(themeId)), m_settings(overlayConfigToQmlValues(config)), m_options(std::move(options)) {
        connect(&m_presenter, &KeyboardPresenter::heldKeysChanged, this, &ThemeApi::heldKeysChanged);
        connect(&m_presenter, &KeyboardPresenter::fadingChanged, this, &ThemeApi::fadingChanged);
        connect(&m_presenter, &KeyboardPresenter::fadeDurationMsChanged, this, &ThemeApi::fadeDurationMsChanged);
    }

    HistoryListModel* ThemeApi::history() const {
        return &m_presenter.historyModel();
    }

    HistoryListModel* ThemeApi::expiredHistory() const {
        return &m_presenter.fadingHistoryModel();
    }

    QStringList ThemeApi::heldKeys() const {
        return m_presenter.heldKeys();
    }

    int ThemeApi::heldKeyCount() const {
        return m_presenter.heldKeyCount();
    }

    bool ThemeApi::fading() const {
        return m_presenter.fading();
    }

    int ThemeApi::fadeDurationMs() const {
        return m_presenter.fadeDurationMs();
    }

    void ThemeApi::updateConfiguration(const OverlayConfig& config, QVariantMap options) {
        const QVariantMap settings = overlayConfigToQmlValues(config);
        if (settings != m_settings) {
            m_settings = settings;
            emit settingsChanged();
        }
        if (options != m_options) {
            m_options = std::move(options);
            emit optionsChanged();
        }
    }

    ThemeRuntime::ThemeRuntime(const ThemeCatalog& catalog, QQuickView& view, KeyboardPresenter& presenter, QObject* parent) :
        QObject(parent), m_catalog(catalog), m_view(view), m_presenter(presenter), m_host(qobject_cast<QQuickItem*>(view.rootObject())) {
        if (m_host) {
            connect(m_host, &QQuickItem::widthChanged, this, &ThemeRuntime::resizeSlots);
            connect(m_host, &QQuickItem::heightChanged, this, &ThemeRuntime::resizeSlots);
        }
    }

    ThemeRuntime::~ThemeRuntime() {
        destroyInstance(std::move(m_staged));
        destroyInstance(std::move(m_active));
    }

    bool ThemeRuntime::loadInitial(const OverlayConfig& config, QString* error) {
        if (!m_host) {
            return fail(error, QStringLiteral("theme host QML root is not a QQuickItem"));
        }
        auto instance = createInstance(config, error);
        if (!instance) {
            return false;
        }
        instance->slot->setVisible(true);
        m_active = std::move(instance);
        return true;
    }

    bool ThemeRuntime::prepareSwitch(const OverlayConfig& config, QString* error) {
        if (!m_active) {
            return fail(error, QStringLiteral("no active theme is available to switch from"));
        }
        if (config.themeId == m_active->id) {
            return true;
        }
        // This only stages detectable component creation failures. QML is trusted code and may
        // already have performed side effects; staging is not a security or rollback boundary.
        auto candidate = createInstance(config, error);
        if (!candidate) {
            return false;
        }
        destroyInstance(std::move(m_staged));
        m_staged = std::move(candidate);
        return true;
    }

    void ThemeRuntime::applyAcceptedConfiguration(const OverlayConfig& config) {
        if (m_staged && m_staged->id == config.themeId) {
            m_staged->slot->setVisible(true);
            if (m_active) {
                m_active->slot->setVisible(false);
            }
            destroyInstance(std::move(m_active));
            m_active = std::move(m_staged);
        }
        if (!m_active || m_active->id != config.themeId) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: accepted config references theme '%1' but no matching theme instance is active").arg(config.themeId);
            return;
        }
        QVariantMap options;
        QString     error;
        if (!m_catalog.validateOptions(config.themeId, config.themeOptions, &options, &error)) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: accepted theme options became invalid: %1").arg(error);
            return;
        }
        m_active->api->updateConfiguration(config, std::move(options));
    }

    QString ThemeRuntime::activeThemeId() const {
        return m_active ? m_active->id : QString{};
    }

    std::unique_ptr<ThemeRuntime::Instance> ThemeRuntime::createInstance(const OverlayConfig& config, QString* error) {
        ThemeDescriptor descriptor;
        if (!m_catalog.resolve(config.themeId, &descriptor, error)) {
            return {};
        }
        QVariantMap effectiveOptions;
        if (!m_catalog.validateOptions(config.themeId, config.themeOptions, &effectiveOptions, error)) {
            return {};
        }
        auto instance  = std::make_unique<Instance>();
        instance->id   = config.themeId;
        instance->slot = new QQuickItem(m_host);
        instance->slot->setObjectName(QStringLiteral("hyprcastThemeSlot"));
        instance->slot->setParentItem(m_host);
        instance->slot->setSize(m_host->size());
        instance->slot->setVisible(false);
        instance->context = new QQmlContext(m_view.rootContext(), this);
        instance->api     = new ThemeApi(m_presenter, config, config.themeId, std::move(effectiveOptions), instance->context);
        instance->context->setContextProperty(QStringLiteral("hyprcast"), instance->api);

        QQmlComponent component(m_view.engine(), descriptor.entryUrl, QQmlComponent::PreferSynchronous);
        if (component.isError()) {
            const QString message = componentErrors(component).join(QLatin1Char('\n'));
            destroyInstance(std::move(instance));
            fail(error, QStringLiteral("theme '%1' failed to load '%2':\n%3").arg(config.themeId, descriptor.entryUrl.toString(), message));
            return {};
        }
        // Creation evaluates theme QML (including Component.onCompleted handlers).
        QObject*    object = component.create(instance->context);
        QQuickItem* root   = qobject_cast<QQuickItem*>(object);
        if (component.isError() || !root) {
            const QString message = componentErrors(component).join(QLatin1Char('\n'));
            if (object) {
                delete object;
            }
            destroyInstance(std::move(instance));
            fail(error,
                 QStringLiteral("theme '%1' must create a QQuickItem root; loading '%2' failed%3")
                     .arg(config.themeId, descriptor.entryUrl.toString(), message.isEmpty() ? QString{} : QStringLiteral(":\n") + message));
            return {};
        }
        root->setParent(instance->slot);
        root->setParentItem(instance->slot);
        instance->root = root;
        return instance;
    }

    void ThemeRuntime::destroyInstance(std::unique_ptr<Instance> instance) {
        if (!instance) {
            return;
        }
        if (instance->slot) {
            delete instance->slot;
            instance->slot = nullptr;
            instance->root = nullptr;
        }
        if (instance->context) {
            delete instance->context;
            instance->context = nullptr;
            instance->api     = nullptr;
        }
    }

    void ThemeRuntime::resizeSlots() {
        const QSizeF size = m_host ? m_host->size() : QSizeF{};
        if (m_active && m_active->slot) {
            m_active->slot->setSize(size);
        }
        if (m_staged && m_staged->slot) {
            m_staged->slot->setSize(size);
        }
    }
} // namespace Hyprcast::Overlay
