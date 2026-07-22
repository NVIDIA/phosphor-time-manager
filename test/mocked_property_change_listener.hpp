#pragma once
#include "property_change_listener.hpp"

#include <gmock/gmock.h>

namespace phosphor
{
namespace time
{

class MockPropertyChangeListener : public PropertyChangeListener
{
  public:
    MOCK_METHOD(void, onModeChanged, (Mode mode), (override));
};

} // namespace time
} // namespace phosphor
