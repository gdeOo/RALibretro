/*
Copyright (C) 2018 Andre Leiradella

This file is part of RALibretro.

RALibretro is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RALibretro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Input.h"

#include "jsonsax/jsonsax.h"

#define KEYBOARD_ID -1

#define TAG "[INP] "

static const char* s_gameControllerDB[] =
{
  // Updated on 2017-06-15
  #include "GameControllerDB.inl"
  // Some controllers I have around
  "63252305000000000000504944564944,USB Vibration Joystick (BM),platform:Windows,x:b3,a:b2,b:b1,y:b0,back:b8,start:b9,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,dpup:h0.1,leftshoulder:b4,lefttrigger:b6,rightshoulder:b5,righttrigger:b7,leftstick:b10,rightstick:b11,leftx:a0,lefty:a1,rightx:a2,righty:a3,",
  "351212ab000000000000504944564944,NES30 Joystick,platform:Windows,x:b3,a:b2,b:b1,y:b0,back:b6,start:b7,leftshoulder:b4,rightshoulder:b5,leftx:a0,lefty:a1,"
};

bool Input::init(libretro::LoggerComponent* logger)
{
  _logger = logger;

  reset();

  // Add controller mappings
  for (size_t i = 0; i < sizeof(s_gameControllerDB) / sizeof(s_gameControllerDB[0]); i++ )
  {
    SDL_GameControllerAddMapping(s_gameControllerDB[i]);
  }

  // Add the keyboard controller
  Pad keyb;

  keyb._id = KEYBOARD_ID;
  keyb._controller = NULL;
  keyb._controllerName = "Keyboard";
  keyb._joystick = NULL;
  keyb._joystickName = "Keyboard";
  keyb._ports = 0;
  keyb._sensitivity = 0.5f;

  _pads.insert(std::make_pair(keyb._id, keyb));
  _logger->info(TAG "Controller %s (%s) added", keyb._controllerName, keyb._joystickName);

  // Add controllers already connected
  int max = SDL_NumJoysticks();

  for(int i = 0; i < max; i++)
  {
    addController(i);
  }

  return true;
}

void Input::reset()
{
  _descriptors.clear();

  _ports = 0;
  _updated = false;

  ControllerInfo none;
  none._description = "None";
  none._id = RETRO_DEVICE_NONE;
  memset(none._state, 0, sizeof(none._state));

  ControllerInfo retropad;
  retropad._description = "RetroPad";
  retropad._id = RETRO_DEVICE_JOYPAD;
  memset(retropad._state, 0, sizeof(retropad._state));

  for (unsigned i = 0; i < kMaxPorts; i++)
  {
    _info[i].clear();
    _info[i].push_back(none);
    _info[i].push_back(retropad);

    _devices[i] = 0;
  }

  for (auto& pair: _pads)
  {
    Pad* pad = &pair.second;
    pad->_ports = 0;
  }

  memset(&_mouse, 0, sizeof(_mouse));
}

SDL_JoystickID Input::addController(int which)
{
  if (SDL_IsGameController(which))
  {
    Pad pad;

    pad._controller = SDL_GameControllerOpen(which);

    if (pad._controller == NULL)
    {
      _logger->error(TAG "Error opening the controller: %s", SDL_GetError());
      return -1;
    }

    pad._joystick = SDL_GameControllerGetJoystick(pad._controller);

    if (pad._joystick == NULL)
    {
      _logger->error(TAG "Error getting the joystick: %s", SDL_GetError());
      SDL_GameControllerClose(pad._controller);
      return -1;
    }

    pad._id = SDL_JoystickInstanceID(pad._joystick);

    if (_pads.find(pad._id) != _pads.end())
    {
      SDL_GameControllerClose(pad._controller);
      return pad._id;
    }

    pad._controllerName = SDL_GameControllerName(pad._controller);
    pad._joystickName = SDL_JoystickName(pad._joystick);
    pad._ports = 0;
    pad._sensitivity = 0.5f;

    _pads.insert(std::make_pair(pad._id, pad));
    _logger->info(TAG "Controller %s (%s) added", pad._controllerName, pad._joystickName);

    // make sure we're tracking the GUID
    SDL_JoystickGUID guid = SDL_JoystickGetGUID(pad._joystick);
    for (const auto& pair : _joystickGUIDs)
    {
      if (memcmp(pair.second.data, guid.data, sizeof(guid.data)) == 0)
        return pad._id;
    }

    SDL_JoystickID nextID = (SDL_JoystickID)_joystickGUIDs.size();
    _joystickGUIDs.insert(std::make_pair(nextID, guid));
    return pad._id;
  }

  return -1;
}

void Input::autoAssign()
{
  Pad* pad = NULL;

  for (auto& pair: _pads)
  {
    if (pair.second._id != KEYBOARD_ID)
    {
      pad = &pair.second;
      break;
    }
  }
  
  if (pad == NULL)
  {
    auto found = _pads.find(KEYBOARD_ID);

    if (found != _pads.end())
    {
      pad = &found->second;
    }
  }

  if (pad == NULL)
  {
    return;
  }

  uint64_t bit = 1;

  for (unsigned port = 0; port < kMaxPorts; port++, bit <<= 1)
  {
    if ((_ports & bit) != 0)
    {
      pad->_ports |= bit;

      if (_devices[port] == 0)
      {
        for (unsigned i = 1 /* Skip None */; i < _info[port].size(); i++)
        {
          const ControllerInfo* info = &_info[port][i];

          if ((info->_id & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD)
          {
            _devices[port] = i;
            break;
          }
        }
      }

      _updated = true;
    }
  }
}

void Input::buttonEvent(int port, Button button, bool pressed)
{
  int rbutton;

  switch (button)
  {
    case Button::kUp:     rbutton = RETRO_DEVICE_ID_JOYPAD_UP; break;
    case Button::kDown:   rbutton = RETRO_DEVICE_ID_JOYPAD_DOWN; break;
    case Button::kLeft:   rbutton = RETRO_DEVICE_ID_JOYPAD_LEFT; break;
    case Button::kRight:  rbutton = RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
    case Button::kX:      rbutton = RETRO_DEVICE_ID_JOYPAD_X; break;
    case Button::kY:      rbutton = RETRO_DEVICE_ID_JOYPAD_Y; break;
    case Button::kA:      rbutton = RETRO_DEVICE_ID_JOYPAD_A; break;
    case Button::kB:      rbutton = RETRO_DEVICE_ID_JOYPAD_B; break;
    case Button::kL:      rbutton = RETRO_DEVICE_ID_JOYPAD_L; break;
    case Button::kR:      rbutton = RETRO_DEVICE_ID_JOYPAD_R; break;
    case Button::kL2:     rbutton = RETRO_DEVICE_ID_JOYPAD_L2; break;
    case Button::kR2:     rbutton = RETRO_DEVICE_ID_JOYPAD_R2; break;
    case Button::kL3:     rbutton = RETRO_DEVICE_ID_JOYPAD_L3; break;
    case Button::kR3:     rbutton = RETRO_DEVICE_ID_JOYPAD_R3; break;
    case Button::kSelect: rbutton = RETRO_DEVICE_ID_JOYPAD_SELECT; break;
    case Button::kStart:  rbutton = RETRO_DEVICE_ID_JOYPAD_START; break;
    default:              return;
  }

  _info[port][_devices[port]]._state[rbutton] = pressed;
}

void Input::axisEvent(int port, Axis axis, int16_t value)
{
  int raxis;

  switch (axis)
  {
    case Axis::kLeftAxisX:  raxis = (RETRO_DEVICE_INDEX_ANALOG_LEFT  << 1) | RETRO_DEVICE_ID_ANALOG_X; break;
    case Axis::kLeftAxisY:  raxis = (RETRO_DEVICE_INDEX_ANALOG_LEFT  << 1) | RETRO_DEVICE_ID_ANALOG_Y; break;
    case Axis::kRightAxisX: raxis = (RETRO_DEVICE_INDEX_ANALOG_RIGHT << 1) | RETRO_DEVICE_ID_ANALOG_X; break;
    case Axis::kRightAxisY: raxis = (RETRO_DEVICE_INDEX_ANALOG_RIGHT << 1) | RETRO_DEVICE_ID_ANALOG_Y; break;
    default:                return;
  }

  _info[port][_devices[port]]._axis[raxis] = value;
}

void Input::processEvent(const SDL_Event* event, KeyBinds* keyBinds)
{
  switch (event->type)
  {
  case SDL_CONTROLLERDEVICEADDED:
    addController(event, keyBinds);
    break;

  case SDL_CONTROLLERDEVICEREMOVED:
    removeController(event);
    break;
  }
}

void Input::mouseButtonEvent(MouseButton button, bool pressed)
{
  int rbutton;

  switch (button)
  {
    case MouseButton::kLeft:   rbutton = RETRO_DEVICE_ID_MOUSE_LEFT; break;
    case MouseButton::kRight:  rbutton = RETRO_DEVICE_ID_MOUSE_RIGHT; break;
    case MouseButton::kMiddle: rbutton = RETRO_DEVICE_ID_MOUSE_MIDDLE; break;
    default:                   return;
  }

  _mouse._button[rbutton] = pressed;
}

void Input::mouseMoveEvent(int relative_x, int relative_y, int absolute_x, int absolute_y)
{
  _mouse._relative_x = (int16_t)relative_x;
  _mouse._relative_y = (int16_t)relative_y;
  _mouse._absolute_x = (int16_t)absolute_x;
  _mouse._absolute_y = (int16_t)absolute_y;
}

void Input::setInputDescriptors(const struct retro_input_descriptor* descs, unsigned count)
{
  for (unsigned i = 0; i < count; descs++, i++)
  {
    Descriptor desc;
    desc._port = descs->port;
    desc._device = descs->device;
    desc._index = descs->index;
    desc._button = descs->id;
    desc._description = descs->description;

    _descriptors.push_back(desc);

    if (desc._port < kMaxPorts)
    {
      _ports |= 1ULL << desc._port;
    }
    else
    {
      _logger->warn(TAG "Port %u above %d limit", desc._port + 1, kMaxPorts);
    }
  }
}

void Input::setControllerInfo(const struct retro_controller_info* rinfo, unsigned count)
{
  ControllerInfo none;
  none._description = "None";
  none._id = RETRO_DEVICE_NONE;
  memset(none._state, 0, sizeof(none._state));

  ControllerInfo retropad;
  retropad._description = "RetroPad";
  retropad._id = RETRO_DEVICE_JOYPAD;
  memset(retropad._state, 0, sizeof(retropad._state));

  _ports = 0;

  for (unsigned port = 0; port < kMaxPorts; rinfo++, port++)
  {
    _info[port].clear();
    _info[port].push_back(none);
    _info[port].push_back(retropad);

    if (port < count)
    {
      for (unsigned i = 0; i < rinfo->num_types; i++)
      {
        if (rinfo->types[i].id == RETRO_DEVICE_JOYPAD)
        {
          _info[port].at(1)._description = rinfo->types[i].desc;
          continue;
        }

        ControllerInfo info;
        info._description = rinfo->types[i].desc;
        info._id = rinfo->types[i].id;

        if ((info._id & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD ||
          (info._id & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG)
        {
          memset(info._state, 0, sizeof(info._state));
          memset(info._axis, 0, sizeof(info._axis));

          _info[port].push_back(info);
          _ports |= 1ULL << port;
        }
      }
    }

    if (_devices[port] > _info[port].size())
      _devices[port] = 0;
  }

  _updated = true;
}

void Input::getControllerNames(unsigned port, std::vector<std::string>& names, int& selectedIndex) const
{
  names.clear();

  if (port < kMaxPorts)
  {
    for (const ControllerInfo& info : _info[port])
      names.push_back(info._description);

    selectedIndex = _devices[port];
  }
  else
  {
    selectedIndex = 0;
  }
}

void Input::setSelectedControllerIndex(unsigned port, int selectedIndex)
{
  _devices[port] = selectedIndex;
  _updated = true;
}

bool Input::ctrlUpdated()
{
  bool updated = _updated;
  _updated = false;
  return updated;
}

unsigned Input::getController(unsigned port)
{
  if (port < kMaxPorts)
    return _info[port][_devices[port]]._id;

  return RETRO_DEVICE_NONE;
}

void Input::poll()
{
  // Events are polled in the main event loop, and arrive in this class via
  // the processEvent method
}

int16_t Input::read(unsigned port, unsigned device, unsigned index, unsigned id)
{
  if (port < kMaxPorts)
  {
    switch (device)
    {
      case RETRO_DEVICE_JOYPAD:
        return _info[port][_devices[port]]._state[id] ? 32767 : 0;

      case RETRO_DEVICE_ANALOG:
        return _info[port][_devices[port]]._axis[index << 1 | id];

      case RETRO_DEVICE_MOUSE:
        switch (id)
        {
          case RETRO_DEVICE_ID_MOUSE_X:
          {
            int delta = (_mouse._absolute_x - _mouse._previous_x);
            _mouse._previous_x = _mouse._absolute_x;
            return (int16_t)delta;
          }
          case RETRO_DEVICE_ID_MOUSE_Y:
          {
            int delta = (_mouse._absolute_y - _mouse._previous_y);
            _mouse._previous_y = _mouse._absolute_y;
            return (int16_t)delta;
          }
          default: return _mouse._button[id];
        }

      case RETRO_DEVICE_POINTER:
        switch (id)
        {
          case RETRO_DEVICE_ID_POINTER_X: return _mouse._relative_x;
          case RETRO_DEVICE_ID_POINTER_Y: return _mouse._relative_y;
          case RETRO_DEVICE_ID_POINTER_PRESSED: return _mouse._button[RETRO_DEVICE_ID_MOUSE_LEFT];
          default: break;
        }
    }
  }

  return 0;
}

KeyBinds::Binding Input::captureButtonPress()
{
  KeyBinds::Binding desc = { 0, 0, KeyBinds::Binding::Type::None, 0 };

  if (!_pads.empty())
  {
    SDL_GameControllerUpdate();
    for (const auto& pair : _pads)
    {
      if (!pair.second._controller)
        continue;

      for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
      {
        if (SDL_GameControllerGetButton(pair.second._controller, static_cast<SDL_GameControllerButton>(i)))
        {
          desc.joystick_id = pair.second._id;
          desc.type = KeyBinds::Binding::Type::Button;
          desc.button = i;
          return desc;
        }
      }

      int threshold = static_cast<int>(32767 * pair.second._sensitivity);

      for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
      {
        const auto value = SDL_GameControllerGetAxis(pair.second._controller, static_cast<SDL_GameControllerAxis>(i));
        if (value < -threshold)
        {
          desc.joystick_id = pair.second._id;
          desc.type = KeyBinds::Binding::Type::Axis;
          desc.button = i;
          desc.modifiers = 0xFF;
          return desc;
        }
        else if (value > threshold)
        {
          desc.joystick_id = pair.second._id;
          desc.type = KeyBinds::Binding::Type::Axis;
          desc.button = i;
          if (i != SDL_CONTROLLER_AXIS_TRIGGERLEFT && i != SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            desc.modifiers = 1;
          return desc;
        }
      }
    }
  }

  return desc;
}

void Input::addController(const SDL_Event* event, KeyBinds* keyBinds)
{
  SDL_JoystickID id = addController(event->cdevice.which);
  if (id >= 0)
  {
    // check to see if the GUID was previously assigned an ID. if so, inform the keybinder of the mapping
    auto pad = _pads.find(id);
    if (pad != _pads.end())
    {
      auto guid = SDL_JoystickGetGUID(pad->second._joystick);
      for (auto pair = _joystickGUIDs.begin(); pair != _joystickGUIDs.end(); ++pair)
      {
        if (memcmp(pair->second.data, guid.data, sizeof(guid.data)) == 0)
        {
          if (pair->first != pad->second._id)
            keyBinds->mapDevice(pair->first, pad->second._id);
          break;
        }
      }
    }
  }
}

void Input::removeController(const SDL_Event* event)
{
  auto it = _pads.find(event->cdevice.which);

  if (it != _pads.end())
  {
    Pad* pad = &it->second;
    _logger->info(TAG "Controller %s (%s) removed", pad->_controllerName, pad->_joystickName);

    // cleanup
    SDL_GameControllerClose(pad->_controller);
    _pads.erase(it);

    // Flag a pending update so the core will receive an event for this removal
    _updated = true;
  }
}

float Input::getJoystickSensitivity(int joystickId)
{
  auto found = _pads.find(joystickId);
  return (found != _pads.end()) ? found->second._sensitivity : 0.0f;
}

std::string Input::serialize()
{
  std::string json("[");
  const char* comma = "";

  for (unsigned port = 0; port < kMaxPorts; port++)
  {
    char temp[128];
    snprintf(temp, sizeof(temp), "%s{\"port\":%u,\"device\":%d}", comma, port + 1, _devices[port]);
    json.append(temp);
    comma = ",";
  }

  json.append("]");
  return json;
}

void Input::deserialize(const char* json)
{
  struct Deserialize
  {
    Input* self;
    std::string key;
    unsigned port;
  };

  Deserialize ud;
  ud.self = this;

  jsonsax_parse(json, &ud, [](void* udata, jsonsax_event_t event, const char* str, size_t num) {
    auto ud = (Deserialize*)udata;

    if (event == JSONSAX_KEY)
    {
      ud->key = std::string(str, num);
    }
    else if (event == JSONSAX_NUMBER)
    {
      if (ud->key == "port")
      {
        ud->port = strtoul(str, NULL, 10) - 1;
      }
      else if (ud->key == "device")
      {
        ud->self->_devices[ud->port] = strtol(str, NULL, 10);
      }
    }
    
    return 0;
  });

  _updated = true;
}

const char* Input::s_getType(int index, void* udata)
{
  auto info = (std::vector<ControllerInfo>*)udata;

  if ((size_t)index < info->size())
  {
    return info->at(index)._description.c_str();
  }

  return NULL;
}

const char* Input::s_getPad(int index, void* udata)
{
  if (index == 0)
  {
    return "Unassigned";
  }

  auto pads = (std::map<SDL_JoystickID, Pad>*)udata;

  for (const auto& pad: *pads)
  {
    if (--index == 0)
    {
      return pad.second._controllerName;
    }
  }

  return NULL;
}
