#include "InputCapture.h"

CInputCapture::CInputCapture(Observer observer) {
    m_keyEventListener = Event::bus()->m_events.input.keyboard.key.listen(observer);
}
