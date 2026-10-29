#pragma once

#include <QPointer>

#include <utility>

namespace Hyprcast::Overlay {
    // Unique owner for a QObject that also has a Qt parent (for example so it shows up
    // in findChild()). Deletes the object on destruction unless Qt already did because
    // the parent died first; the QPointer turns null in that case, avoiding a double delete.
    template <typename T>
    class QObjectOwner {
      public:
        QObjectOwner() = default;
        explicit QObjectOwner(T* object) noexcept : m_object(object) {}
        ~QObjectOwner() {
            delete m_object.data();
        }

        QObjectOwner(const QObjectOwner&)            = delete;
        QObjectOwner& operator=(const QObjectOwner&) = delete;
        QObjectOwner(QObjectOwner&& other) noexcept : m_object(std::exchange(other.m_object, nullptr)) {}
        QObjectOwner& operator=(QObjectOwner&& other) noexcept {
            if (this != &other) {
                delete m_object.data();
                m_object = std::exchange(other.m_object, nullptr);
            }
            return *this;
        }

        [[nodiscard]] T* get() const noexcept {
            return m_object.data();
        }
        T* operator->() const noexcept {
            return get();
        }
        explicit operator bool() const noexcept {
            return !m_object.isNull();
        }

      private:
        QPointer<T> m_object;
    };
}
