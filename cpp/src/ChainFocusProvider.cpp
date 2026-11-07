#include "ChainFocusProvider.h"

ChainFocusProvider::ChainFocusProvider(
    std::unique_ptr<FocusedWindowProvider> primary,
    std::unique_ptr<FocusedWindowProvider> fallback)
    : m_primary(std::move(primary))
    , m_fallback(std::move(fallback)) {
}

int ChainFocusProvider::focusedPid() const {
    if (m_primary != nullptr) {
        const int pid = m_primary->focusedPid();
        if (pid > 0) {
            m_activeName = m_primary->name();
            return pid;
        }
    }
    if (m_fallback != nullptr) {
        const int pid = m_fallback->focusedPid();
        if (pid > 0) {
            m_activeName = m_fallback->name();
            return pid;
        }
    }
    return -1;
}

QString ChainFocusProvider::name() const {
    if (!m_activeName.isEmpty()) {
        return m_activeName;
    }
    if (m_primary != nullptr) {
        return m_primary->name();
    }
    return m_fallback != nullptr ? m_fallback->name()
                                 : QStringLiteral("chain");
}
