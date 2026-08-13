/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#pragma once

#include <cstdint>

namespace Dx12Input
{
  enum class Button : uint32_t
  {
    none = 0,
    left = 1,
    right = 2,
    middle = 4
  };

  enum class Modifier : uint32_t
  {
    none = 0,
    shift = 1,
    ctrl = 2,
    alt = 4
  };

  struct PointerEvent
  {
    float x = 0.f;
    float y = 0.f;
    Button button = Button::none;
    Modifier modifiers = Modifier::none;
  };

  struct WheelEvent
  {
    float x = 0.f;
    float y = 0.f;
    float delta = 0.f;
    Modifier modifiers = Modifier::none;
  };
}
