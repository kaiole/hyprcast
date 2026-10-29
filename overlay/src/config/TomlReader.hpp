#pragma once

#include <toml++/toml.hpp>

#include <QString>
#include <QVariantMap>

#include <concepts>
#include <expected>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>

// Small helpers shared by the overlay config and theme manifest parsers.
namespace Hyprcast::Overlay::Toml {
    [[nodiscard]] QString toQString(std::string_view text);

    // The sub-table `key` of `parent`, nullptr when absent, or an error when it is not a table.
    [[nodiscard]] std::expected<const toml::table*, QString> optionalTable(const toml::table& parent, std::string_view key, const QString& parentPath = {});

    // Reads optional fields of one table into existing values; absent keys leave them
    // unchanged. The first failure is kept and later reads are skipped, so a table can be
    // read in one chain and checked once with result().
    class TableReader {
      public:
        // `path` is the dotted table name used in messages; `label` names the document
        // kind in unknown/missing key messages (for example "configuration").
        TableReader(const toml::table& table, QString path, QString label = QStringLiteral("configuration"));

        TableReader& onlyKeys(std::initializer_list<std::string_view> allowed);
        TableReader& required(std::string_view key);
        TableReader& string(std::string_view key, QString& out);
        TableReader& boolean(std::string_view key, bool& out);
        TableReader& number(std::string_view key, double& out); // TOML integers are accepted too.
        TableReader& stringMap(std::string_view key, QVariantMap& out);

        template <std::integral T>
        TableReader& integer(std::string_view key, T& out) {
            return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
                const auto parsed = value.value_exact<std::int64_t>();
                if (!parsed || *parsed < std::numeric_limits<T>::min() || *parsed > std::numeric_limits<T>::max()) {
                    return std::unexpected(QStringLiteral("'%1' must be an integer").arg(name(key)));
                }
                out = static_cast<T>(*parsed);
                return {};
            });
        }

        // Reads a string and converts it with `parse`, which returns the value or an error.
        template <typename T, typename Parse>
        TableReader& choice(std::string_view key, T& out, Parse&& parse) {
            return node(key, [&](const toml::node& value) -> std::expected<void, QString> {
                const auto text = value.value_exact<std::string>();
                if (!text) {
                    return std::unexpected(QStringLiteral("'%1' must be a string").arg(name(key)));
                }
                auto parsed = std::forward<Parse>(parse)(toQString(*text));
                if (!parsed) {
                    return std::unexpected(parsed.error());
                }
                out = *parsed;
                return {};
            });
        }

        // Runs `read(node)` when `key` is present; `read` returns std::expected<void, QString>.
        template <typename Read>
        TableReader& node(std::string_view key, Read&& read) {
            if (!ok()) {
                return *this;
            }
            if (const auto* value = m_table.get(key)) {
                if (auto outcome = std::forward<Read>(read)(*value); !outcome) {
                    m_error = outcome.error();
                }
            }
            return *this;
        }

        [[nodiscard]] bool ok() const noexcept {
            return m_error.isEmpty();
        }
        [[nodiscard]] std::expected<void, QString> result() const;
        // Dotted name of `key` within this table, for messages.
        [[nodiscard]] QString name(std::string_view key) const;

      private:
        const toml::table& m_table;
        QString            m_path;
        QString            m_label;
        QString            m_error;
    };
} // namespace Hyprcast::Overlay::Toml
