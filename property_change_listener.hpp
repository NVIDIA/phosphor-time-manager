#pragma once

#include "types.hpp"

namespace phosphor
{
namespace time
{

class PropertyChangeListener
{
  public:
    PropertyChangeListener() = default;
    virtual ~PropertyChangeListener() = default;

    PropertyChangeListener(const PropertyChangeListener&) = delete;
    PropertyChangeListener(PropertyChangeListener&&) = delete;
    PropertyChangeListener& operator=(const PropertyChangeListener&) = delete;
    PropertyChangeListener& operator=(PropertyChangeListener&&) = delete;

    /** @brief Notified on time mode is changed */
    virtual void onModeChanged(Mode mode) = 0;
};

} // namespace time
} // namespace phosphor
