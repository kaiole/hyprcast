#include "TomlReader.hpp"

#include <algorithm>
#include <string>

namespace Hyprcast::Overlay::Toml {
    QString toQString(std::string_view text) {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    std::expected<const toml::table*, QString> optionalTable(const toml::table& parent, std::string_view key, const QString& parentPath) {
        const auto* node = parent.get(key);
        if (!node) {
            return nullptr;
        }
        if (const auto* table = node->as_table()) {
            return table;
        }
        const QString name = parentPath.isEmpty() ? toQString(key) : parentPath + QLatin1Char('.') + toQString(key);
        return std::unexpected(QStringLiteral("'%1' must be a TOML table").arg(name));
    }

    TableReader::TableReader(const toml::table& table, QString path, QString label) : m_table(table), m_path(std::move(path)), m_label(std::move(label)) {}

    TableReader& TableReader::onlyKeys(std::initializer_list<std::string_view> allowed) {
        if (!ok()) {
            return *this;
        }
        for (const auto& [key, value] : m_table) {
            if (!std::ranges::contains(allowed, key.str())) {
                m_error = QStringLiteral("unknown %1 key '%2'").arg(m_label, name(key.str()));
                break;
            }
        }
        return *this;
    }

    TableReader& TableReader::required(std::string_view key) {
        if (ok() && !m_table.get(key)) {
            m_error = QStringLiteral("missing required %1 key '%2'").arg(m_label, name(key));
        }
        return *this;
    }

    TableReader& TableReader::string(std::string_view key, QString& out) {
        return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
            const auto text = value.value_exact<std::string>();
            if (!text) {
                return std::unexpected(QStringLiteral("'%1' must be a string").arg(name(key)));
            }
            out = toQString(*text);
            return {};
        });
    }

    TableReader& TableReader::boolean(std::string_view key, bool& out) {
        return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
            const auto flag = value.value_exact<bool>();
            if (!flag) {
                return std::unexpected(QStringLiteral("'%1' must be a boolean").arg(name(key)));
            }
            out = *flag;
            return {};
        });
    }

    TableReader& TableReader::number(std::string_view key, double& out) {
        return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
            const auto parsed = value.value<double>();
            if (!parsed) {
                return std::unexpected(QStringLiteral("'%1' must be a number").arg(name(key)));
            }
            out = *parsed;
            return {};
        });
    }

    TableReader& TableReader::stringMap(std::string_view key, QVariantMap& out) {
        return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
            const auto* table = value.as_table();
            if (!table) {
                return std::unexpected(QStringLiteral("'%1' must be a TOML table").arg(name(key)));
            }
            for (const auto& [entryKey, entry] : *table) {
                const auto text = entry.value_exact<std::string>();
                if (!text) {
                    return std::unexpected(QStringLiteral("'%1.%2' must be a string").arg(name(key), toQString(entryKey.str())));
                }
                out.insert(toQString(entryKey.str()), toQString(*text));
            }
            return {};
        });
    }

    std::expected<void, QString> TableReader::result() const {
        if (!ok()) {
            return std::unexpected(m_error);
        }
        return {};
    }

    QString TableReader::name(std::string_view key) const {
        return m_path.isEmpty() ? toQString(key) : m_path + QLatin1Char('.') + toQString(key);
    }
} // namespace Hyprcast::Overlay::Toml
