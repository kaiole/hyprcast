#include "Theme.hpp"
#include "../config/TomlReader.hpp"

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
#include <string>
#include <string_view>

namespace Hyprcast::Overlay {
    namespace {
        using Toml::TableReader;
        using Toml::toQString;

        const QString BuiltinDefaultId = QStringLiteral("builtin:default");
        const QString ManifestLabel    = QStringLiteral("theme manifest");

        bool          validOptionName(const QString& name) {
            static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,63}$"));
            return pattern.match(name).hasMatch();
        }

        bool validThemeId(const QString& id) {
            static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
            return pattern.match(id).hasMatch() && !id.startsWith(QStringLiteral("builtin."));
        }

        std::expected<ThemeOptionType, QString> parseOptionType(QStringView text, const QString& path) {
            static const QHash<QString, ThemeOptionType> types{
                {QStringLiteral("boolean"), ThemeOptionType::Boolean}, {QStringLiteral("integer"), ThemeOptionType::Integer}, {QStringLiteral("number"), ThemeOptionType::Number},
                {QStringLiteral("string"), ThemeOptionType::String},   {QStringLiteral("enum"), ThemeOptionType::Enum},       {QStringLiteral("color"), ThemeOptionType::Color},
            };
            const auto found = types.constFind(text.toString().toLower());
            if (found == types.cend()) {
                return std::unexpected(QStringLiteral("'%1' must be boolean, integer, number, string, enum, or color").arg(path));
            }
            return found.value();
        }

        bool isNumeric(ThemeOptionType type) {
            return type == ThemeOptionType::Integer || type == ThemeOptionType::Number;
        }

        // Converts a TOML value to the QVariant representation used for `type`.
        std::expected<QVariant, QString> readOptionValue(const toml::node& node, ThemeOptionType type, const QString& path) {
            switch (type) {
                case ThemeOptionType::Boolean:
                    if (const auto parsed = node.value_exact<bool>()) {
                        return QVariant(*parsed);
                    }
                    return std::unexpected(QStringLiteral("'%1' must be a TOML boolean").arg(path));
                case ThemeOptionType::Integer:
                    if (const auto parsed = node.value_exact<std::int64_t>()) {
                        return QVariant::fromValue<qlonglong>(*parsed);
                    }
                    return std::unexpected(QStringLiteral("'%1' must be a TOML integer").arg(path));
                case ThemeOptionType::Number:
                    if (const auto parsed = node.value_exact<double>()) {
                        return QVariant(*parsed);
                    }
                    if (const auto parsed = node.value_exact<std::int64_t>()) {
                        return QVariant(static_cast<double>(*parsed));
                    }
                    return std::unexpected(QStringLiteral("'%1' must be a TOML number").arg(path));
                case ThemeOptionType::String:
                case ThemeOptionType::Enum:
                case ThemeOptionType::Color: break;
            }
            if (const auto parsed = node.value_exact<std::string>()) {
                return QVariant(toQString(*parsed));
            }
            return std::unexpected(QStringLiteral("'%1' must be a TOML string").arg(path));
        }

        bool isIntegerVariant(const QVariant& value) {
            const int type = value.metaType().id();
            return type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt || type == QMetaType::ULongLong;
        }

        std::expected<void, QString> validateOptionValue(const ThemeOption& option, const QVariant& value, const QString& path) {
            const int  typeId   = value.metaType().id();
            const bool isString = typeId == QMetaType::QString;
            switch (option.type) {
                case ThemeOptionType::Boolean:
                    if (typeId != QMetaType::Bool) {
                        return std::unexpected(QStringLiteral("'%1' must be a boolean").arg(path));
                    }
                    break;
                case ThemeOptionType::Integer:
                    if (!isIntegerVariant(value)) {
                        return std::unexpected(QStringLiteral("'%1' must be an integer").arg(path));
                    }
                    break;
                case ThemeOptionType::Number:
                    if (!isIntegerVariant(value) && typeId != QMetaType::Double) {
                        return std::unexpected(QStringLiteral("'%1' must be a number").arg(path));
                    }
                    if (!std::isfinite(value.toDouble())) {
                        return std::unexpected(QStringLiteral("'%1' must be finite").arg(path));
                    }
                    break;
                case ThemeOptionType::String:
                    if (!isString || value.toString().size() > 4096 || value.toString().contains(QChar::Null)) {
                        return std::unexpected(QStringLiteral("'%1' must be a string no longer than 4096 characters").arg(path));
                    }
                    break;
                case ThemeOptionType::Enum:
                    if (!isString || !option.choices.contains(value.toString())) {
                        return std::unexpected(QStringLiteral("'%1' must be one of: %2").arg(path, option.choices.join(QStringLiteral(", "))));
                    }
                    break;
                case ThemeOptionType::Color:
                    if (!isString || !QColor(value.toString()).isValid()) {
                        return std::unexpected(QStringLiteral("'%1' must be a valid Qt color, for example '#ffffff'").arg(path));
                    }
                    break;
            }

            const double numeric = value.toDouble();
            if (option.minimum && numeric < option.minimum->toDouble()) {
                return std::unexpected(QStringLiteral("'%1' must be at least %2").arg(path, option.minimum->toString()));
            }
            if (option.maximum && numeric > option.maximum->toDouble()) {
                return std::unexpected(QStringLiteral("'%1' must be at most %2").arg(path, option.maximum->toString()));
            }
            return {};
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

        std::expected<QString, QString> resolveInsidePackage(const QString& relativePath, const QString& packageRoot) {
            if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath) || relativePath.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("theme entry must be a non-empty relative path"));
            }
            const QString clean = QDir::cleanPath(relativePath);
            if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
                return std::unexpected(QStringLiteral("theme entry must stay inside its theme directory"));
            }
            const QString rootCanonical = QFileInfo(packageRoot).canonicalFilePath();
            const QString fileCanonical = QFileInfo(QDir(packageRoot).filePath(clean)).canonicalFilePath();
            if (rootCanonical.isEmpty() || fileCanonical.isEmpty() || !QFileInfo(fileCanonical).isFile() ||
                !(fileCanonical == rootCanonical || fileCanonical.startsWith(rootCanonical + QDir::separator()))) {
                return std::unexpected(QStringLiteral("theme entry '%1' is missing or resolves outside the theme directory").arg(relativePath));
            }
            return fileCanonical;
        }

        // Reads the numeric bound `key` ("minimum" or "maximum") of an integer or number option.
        std::expected<std::optional<QVariant>, QString> readBound(const toml::table& declaration, std::string_view key, const ThemeOption& option, const QString& prefix) {
            const auto* node = declaration.get(key);
            if (!node) {
                return std::nullopt;
            }
            const QString path = prefix + QLatin1Char('.') + toQString(key);
            if (!isNumeric(option.type)) {
                return std::unexpected(QStringLiteral("'%1' is only valid for integer and number options").arg(path));
            }
            auto value = readOptionValue(*node, option.type, path);
            if (!value) {
                return std::unexpected(value.error());
            }
            if (option.type == ThemeOptionType::Number && !std::isfinite(value->toDouble())) {
                return std::unexpected(QStringLiteral("'%1' must be finite").arg(path));
            }
            return std::optional<QVariant>{std::move(*value)};
        }

        std::expected<QStringList, QString> readChoices(const toml::node& node, const QString& prefix) {
            const auto* array = node.as_array();
            if (!array || array->empty()) {
                return std::unexpected(QStringLiteral("'%1.values' must be a non-empty array of strings").arg(prefix));
            }
            QStringList choices;
            for (const auto& item : *array) {
                const auto value = item.value_exact<std::string>();
                if (!value) {
                    return std::unexpected(QStringLiteral("'%1.values' must contain only strings").arg(prefix));
                }
                choices.push_back(toQString(*value));
            }
            if (choices.removeDuplicates() != 0) {
                return std::unexpected(QStringLiteral("'%1.values' must not contain duplicates").arg(prefix));
            }
            return choices;
        }

        std::expected<ThemeOption, QString> readOptionDeclaration(const QString& name, const toml::node& node) {
            const QString prefix      = QStringLiteral("options.%1").arg(name);
            const auto*   declaration = node.as_table();
            if (!declaration) {
                return std::unexpected(QStringLiteral("'%1' must be a TOML table").arg(prefix));
            }

            ThemeOption option;
            TableReader read(*declaration, prefix, ManifestLabel);
            read.onlyKeys({"type", "default", "minimum", "maximum", "values", "description"})
                .required("type")
                .choice("type", option.type, [&](QStringView text) { return parseOptionType(text, prefix + QStringLiteral(".type")); })
                .required("default")
                .node("default",
                      [&](const toml::node& value) {
                          return readOptionValue(value, option.type, prefix + QStringLiteral(".default")).transform([&](QVariant parsed) {
                              option.defaultValue = std::move(parsed);
                          });
                      })
                .string("description", option.description);
            if (auto ok = read.result(); !ok) {
                return std::unexpected(ok.error());
            }

            auto minimum = readBound(*declaration, "minimum", option, prefix);
            if (!minimum) {
                return std::unexpected(minimum.error());
            }
            auto maximum = readBound(*declaration, "maximum", option, prefix);
            if (!maximum) {
                return std::unexpected(maximum.error());
            }
            option.minimum = std::move(*minimum);
            option.maximum = std::move(*maximum);

            if (const auto* values = declaration->get("values")) {
                if (option.type != ThemeOptionType::Enum) {
                    return std::unexpected(QStringLiteral("'%1.values' is only valid for enum options").arg(prefix));
                }
                auto choices = readChoices(*values, prefix);
                if (!choices) {
                    return std::unexpected(choices.error());
                }
                option.choices = std::move(*choices);
            }
            if (option.type == ThemeOptionType::Enum && option.choices.isEmpty()) {
                return std::unexpected(QStringLiteral("enum option '%1' must declare values").arg(name));
            }
            if (option.minimum && option.maximum && option.minimum->toDouble() > option.maximum->toDouble()) {
                return std::unexpected(QStringLiteral("'%1.minimum' must not exceed maximum").arg(prefix));
            }
            if (auto ok = validateOptionValue(option, option.defaultValue, prefix + QStringLiteral(".default")); !ok) {
                return std::unexpected(ok.error());
            }
            if (option.description.size() > 1024 || option.description.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("'%1.description' must be at most 1024 characters and contain no NUL").arg(prefix));
            }
            return option;
        }

        std::expected<ThemeDescriptor, QString> loadDescriptor(const QUrl& manifestUrl, bool bundled, const QString& expectedId) {
            const QString manifestFile = readFileName(manifestUrl);
            QFile         file(manifestFile);
            if (manifestFile.isEmpty() || !file.open(QIODevice::ReadOnly)) {
                return std::unexpected(QStringLiteral("cannot read theme manifest '%1'").arg(manifestUrl.toString()));
            }
            if (file.size() > 256 * 1024) {
                return std::unexpected(QStringLiteral("theme manifest exceeds 256 KiB"));
            }
            const QByteArray contents = file.readAll();

            toml::table      parsed;
            try {
                parsed = toml::parse(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())), manifestUrl.toString().toStdString());
            } catch (const std::exception& exception) { // toml::parse_error derives from std::runtime_error.
                return std::unexpected(QString::fromUtf8(exception.what()));
            }

            if (auto ok = TableReader(parsed, {}, ManifestLabel).onlyKeys({"theme", "options"}).result(); !ok) {
                return std::unexpected(ok.error());
            }
            const auto theme = Toml::optionalTable(parsed, "theme");
            if (!theme || !*theme) {
                return std::unexpected(QStringLiteral("theme manifest must contain a [theme] table"));
            }

            ThemeDescriptor result;
            result.bundled = bundled;
            QString     entry;
            TableReader read(**theme, QStringLiteral("theme"), ManifestLabel);
            read.onlyKeys({"id", "name", "api_version", "entry"})
                .required("id")
                .string("id", result.id)
                .required("entry")
                .string("entry", entry)
                .string("name", result.name)
                .required("api_version")
                .integer("api_version", result.apiVersion);
            if (auto ok = read.result(); !ok) {
                return std::unexpected(ok.error());
            }
            if (entry.size() > 1024 || entry.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("theme.entry must be at most 1024 characters and contain no NUL"));
            }
            if (result.name.isEmpty()) {
                result.name = result.id;
            }
            if (!validThemeId(result.id)) {
                return std::unexpected(QStringLiteral("theme id '%1' must match [a-z0-9][a-z0-9._-]{0,63} and not use the reserved 'builtin.' prefix").arg(result.id));
            }
            if (!expectedId.isEmpty() && result.id != expectedId) {
                return std::unexpected(QStringLiteral("manifest id '%1' does not match directory name '%2'").arg(result.id, expectedId));
            }
            if (result.apiVersion != ThemeApiVersion) {
                return std::unexpected(
                    QStringLiteral("theme '%1' requires presentation API version %2; this overlay supports version %3").arg(result.id).arg(result.apiVersion).arg(ThemeApiVersion));
            }
            if (result.name.size() > 128 || result.name.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("theme.name must be at most 128 characters and contain no NUL"));
            }

            if (bundled) {
                const QUrl entryUrl = manifestUrl.resolved(QUrl(entry));
                if (!QFile::exists(readFileName(entryUrl))) {
                    return std::unexpected(QStringLiteral("bundled theme entry '%1' does not exist").arg(entry));
                }
                result.entryUrl = entryUrl;
            } else {
                const auto absoluteEntry = resolveInsidePackage(entry, QFileInfo(manifestUrl.toLocalFile()).absolutePath());
                if (!absoluteEntry) {
                    return std::unexpected(absoluteEntry.error());
                }
                result.entryUrl = QUrl::fromLocalFile(*absoluteEntry);
            }

            const auto options = Toml::optionalTable(parsed, "options");
            if (!options) {
                return std::unexpected(options.error());
            }
            if (*options) {
                for (const auto& [rawName, node] : **options) {
                    const QString name = toQString(rawName.str());
                    if (!validOptionName(name)) {
                        return std::unexpected(QStringLiteral("theme option name '%1' must match [a-z][a-z0-9_]{0,63}").arg(name));
                    }
                    auto option = readOptionDeclaration(name, node);
                    if (!option) {
                        return std::unexpected(option.error());
                    }
                    result.options.insert(name, std::move(*option));
                }
            }
            return result;
        }
    } // namespace

    ThemeCatalog::ThemeCatalog(QStringList themeRoots, QUrl bundledManifest) : m_themeRoots(std::move(themeRoots)), m_bundledManifest(std::move(bundledManifest)) {}

    std::expected<QStringList, QString> ThemeCatalog::discover() {
        m_themes.clear();
        m_rejected.clear();
        m_bundledDefault.reset();

        auto bundled = loadDescriptor(m_bundledManifest, true, {});
        if (!bundled) {
            return std::unexpected(QStringLiteral("bundled default theme is invalid: %1").arg(bundled.error()));
        }
        if (bundled->id != QStringLiteral("default")) {
            return std::unexpected(QStringLiteral("bundled default manifest must use the reserved id 'default'"));
        }
        m_bundledDefault = *bundled;

        QStringList   warnings;
        QSet<QString> claimedIds;
        for (const QString& rootPath : m_themeRoots) {
            const QDir root(rootPath);
            if (!root.exists()) {
                continue;
            }
            for (const QFileInfo& directory : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const QString id = directory.fileName();
                if (!validThemeId(id)) {
                    warnings.push_back(QStringLiteral("Ignoring theme directory '%1': invalid theme ID").arg(directory.absoluteFilePath()));
                    continue;
                }
                if (claimedIds.contains(id)) {
                    warnings.push_back(QStringLiteral("Theme '%1' from '%2' is shadowed by a higher-precedence installation").arg(id, directory.absoluteFilePath()));
                    continue;
                }
                claimedIds.insert(id);

                const QString manifestPath = QDir(directory.absoluteFilePath()).filePath(QStringLiteral("theme.toml"));
                auto          descriptor   = loadDescriptor(QUrl::fromLocalFile(manifestPath), false, id);
                if (!descriptor) {
                    m_rejected.insert(id, descriptor.error());
                    warnings.push_back(QStringLiteral("Ignoring theme directory '%1': %2").arg(directory.absoluteFilePath(), descriptor.error()));
                    continue;
                }
                m_themes.insert(descriptor->id, std::move(*descriptor));
            }
        }

        if (!claimedIds.contains(bundled->id)) {
            m_themes.insert(bundled->id, *bundled);
        } else {
            warnings.push_back(QStringLiteral("Bundled theme '%1' is shadowed; select '%2' to use it explicitly").arg(bundled->id, BuiltinDefaultId));
        }
        return warnings;
    }

    std::expected<const ThemeDescriptor*, QString> ThemeCatalog::resolve(const QString& id) const {
        if (id == BuiltinDefaultId) {
            if (!m_bundledDefault) {
                return std::unexpected(QStringLiteral("bundled default theme has not been discovered"));
            }
            return &*m_bundledDefault;
        }
        const auto found = m_themes.constFind(id);
        if (found != m_themes.cend()) {
            return &found.value();
        }
        const auto rejected = m_rejected.constFind(id);
        if (rejected != m_rejected.cend()) {
            return std::unexpected(QStringLiteral("theme '%1' could not be loaded: %2").arg(id, rejected.value()));
        }
        return std::unexpected(QStringLiteral("theme '%1' was not found; available themes: %2").arg(id, availableThemeIds().join(QStringLiteral(", "))));
    }

    std::expected<QVariantMap, QString> ThemeCatalog::validateOptions(const QString& id, const QVariantMap& supplied) const {
        const auto descriptor = resolve(id);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        const auto& declared = (*descriptor)->options;
        for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) {
            if (!declared.contains(it.key())) {
                return std::unexpected(QStringLiteral("unknown option 'theme.options.%1' for theme '%2'").arg(it.key(), id));
            }
        }

        QVariantMap values;
        for (auto it = declared.cbegin(); it != declared.cend(); ++it) {
            const QVariant value = supplied.contains(it.key()) ? supplied.value(it.key()) : it.value().defaultValue;
            if (auto ok = validateOptionValue(it.value(), value, QStringLiteral("theme.options.%1").arg(it.key())); !ok) {
                return std::unexpected(ok.error());
            }
            values.insert(it.key(), value);
        }
        return values;
    }

    QStringList ThemeCatalog::availableThemeIds() const {
        QStringList ids = m_themes.keys();
        if (m_bundledDefault) {
            ids.push_back(BuiltinDefaultId);
        }
        ids.removeDuplicates();
        std::ranges::sort(ids);
        return ids;
    }

    ThemeApi::ThemeApi(KeyboardPresenter& presenter, const OverlayConfig& config, QString themeId, QVariantMap options, QObject* parent) :
        QObject(parent), m_presenter(presenter), m_themeId(std::move(themeId)), m_settings(overlayConfigToQmlValues(config)), m_options(std::move(options)) {
        connect(&m_presenter, &KeyboardPresenter::heldKeysChanged, this, &ThemeApi::heldKeysChanged);
        auto notifyHistoryCount = [this] { emit historyCountChanged(); };
        auto notifyExpiredCount = [this] { emit expiredHistoryCountChanged(); };
        connect(&m_presenter.historyModel(), &QAbstractItemModel::rowsInserted, this, notifyHistoryCount);
        connect(&m_presenter.historyModel(), &QAbstractItemModel::rowsRemoved, this, notifyHistoryCount);
        connect(&m_presenter.historyModel(), &QAbstractItemModel::modelReset, this, notifyHistoryCount);
        connect(&m_presenter.fadingHistoryModel(), &QAbstractItemModel::rowsInserted, this, notifyExpiredCount);
        connect(&m_presenter.fadingHistoryModel(), &QAbstractItemModel::rowsRemoved, this, notifyExpiredCount);
        connect(&m_presenter.fadingHistoryModel(), &QAbstractItemModel::modelReset, this, notifyExpiredCount);
        connect(&m_presenter, &KeyboardPresenter::fadingChanged, this, &ThemeApi::fadingChanged);
        connect(&m_presenter, &KeyboardPresenter::fadeDurationMsChanged, this, &ThemeApi::fadeDurationMsChanged);
    }

    HistoryListModel* ThemeApi::history() const {
        return &m_presenter.historyModel();
    }

    HistoryListModel* ThemeApi::expiredHistory() const {
        return &m_presenter.fadingHistoryModel();
    }

    HistoryProjectionModel* ThemeApi::displayHistory() const {
        return &m_presenter.historyModel().presentationModel();
    }

    HistoryProjectionModel* ThemeApi::expiredDisplayHistory() const {
        return &m_presenter.fadingHistoryModel().presentationModel();
    }

    int ThemeApi::historyCount() const {
        return m_presenter.historyModel().rowCount();
    }

    int ThemeApi::expiredHistoryCount() const {
        return m_presenter.fadingHistoryModel().rowCount();
    }

    QStringList ThemeApi::heldKeys() const {
        return m_presenter.heldKeys();
    }

    QVariantList ThemeApi::heldKeyItems() const {
        return m_presenter.heldKeyItems();
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

    std::expected<void, QString> ThemeRuntime::loadInitial(const OverlayConfig& config) {
        if (!m_host) {
            return std::unexpected(QStringLiteral("theme host QML root is not a QQuickItem"));
        }
        auto instance = createInstance(config);
        if (!instance) {
            return std::unexpected(instance.error());
        }
        (*instance)->slot->setVisible(true);
        m_active = std::move(*instance);
        return {};
    }

    std::expected<void, QString> ThemeRuntime::prepareSwitch(const OverlayConfig& config) {
        if (!m_active) {
            return std::unexpected(QStringLiteral("no active theme is available to switch from"));
        }
        if (config.themeId == m_active->id) {
            return {};
        }
        // This only stages detectable component creation failures. QML is trusted code and may
        // already have performed side effects; staging is not a security or rollback boundary.
        auto candidate = createInstance(config);
        if (!candidate) {
            return std::unexpected(candidate.error());
        }
        m_staged = std::move(*candidate);
        return {};
    }

    void ThemeRuntime::applyAcceptedConfiguration(const OverlayConfig& config) {
        if (m_staged && m_staged->id == config.themeId) {
            m_staged->slot->setVisible(true);
            if (m_active) {
                m_active->slot->setVisible(false);
            }
            m_active = std::move(m_staged); // Destroys the previous theme.
        }
        if (!m_active || m_active->id != config.themeId) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: accepted config references theme '%1' but no matching theme instance is active").arg(config.themeId);
            return;
        }
        auto options = m_catalog.validateOptions(config.themeId, config.themeOptions);
        if (!options) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: accepted theme options became invalid: %1").arg(options.error());
            return;
        }
        m_active->api->updateConfiguration(config, std::move(*options));
    }

    QString ThemeRuntime::activeThemeId() const {
        return m_active ? m_active->id : QString{};
    }

    std::expected<std::unique_ptr<ThemeRuntime::Instance>, QString> ThemeRuntime::createInstance(const OverlayConfig& config) {
        const auto descriptor = m_catalog.resolve(config.themeId);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        auto effectiveOptions = m_catalog.validateOptions(config.themeId, config.themeOptions);
        if (!effectiveOptions) {
            return std::unexpected(effectiveOptions.error());
        }
        const QUrl entryUrl = (*descriptor)->entryUrl;
        auto       instance = std::make_unique<Instance>();
        instance->id        = config.themeId;
        instance->context   = std::make_unique<QQmlContext>(m_view.rootContext());
        instance->api       = new ThemeApi(m_presenter, config, config.themeId, std::move(*effectiveOptions), instance->context.get());
        instance->context->setContextProperty(QStringLiteral("hyprcast"), instance->api);
        instance->slot = QObjectOwner<QQuickItem>(new QQuickItem(m_host));
        instance->slot->setObjectName(QStringLiteral("hyprcastThemeSlot"));
        instance->slot->setParentItem(m_host);
        instance->slot->setSize(m_host->size());
        instance->slot->setVisible(false);

        QQmlComponent component(m_view.engine(), entryUrl, QQmlComponent::PreferSynchronous);
        if (component.isError()) {
            return std::unexpected(
                QStringLiteral("theme '%1' failed to load '%2':\n%3").arg(config.themeId, entryUrl.toString(), componentErrors(component).join(QLatin1Char('\n'))));
        }
        // Creation evaluates theme QML (including Component.onCompleted handlers).
        std::unique_ptr<QObject> object{component.create(instance->context.get())};
        auto*                    root = qobject_cast<QQuickItem*>(object.get());
        if (component.isError() || !root) {
            const QString message = componentErrors(component).join(QLatin1Char('\n'));
            return std::unexpected(QStringLiteral("theme '%1' must create a QQuickItem root; loading '%2' failed%3")
                                       .arg(config.themeId, entryUrl.toString(), message.isEmpty() ? QString{} : QStringLiteral(":\n") + message));
        }
        root->setParent(instance->slot.get());
        root->setParentItem(instance->slot.get());
        instance->root = root;
        object.release(); // Now owned by the slot.
        return instance;
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
