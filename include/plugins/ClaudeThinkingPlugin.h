#pragma once

#include "PluginManager.h"
#include "timing.h"

class ClaudeThinkingPlugin : public Plugin
{
private:
  enum class Phase
  {
    Typing,
    Pausing,
    Deleting,
  };

  NonBlockingDelay timer;
  Phase phase = Phase::Typing;
  const char *word = "";
  uint8_t wordLength = 0;
  uint8_t typedLength = 0;
  uint8_t lastWordIndex = 255;
  uint16_t tick = 0;
  uint16_t phaseTicks = 0;
  // false = compact 3x5 caps, whole word on the panel; true = the original
  // large look (7x7 star + system font, terminal-style scroll).
  bool bigFont = false;

  void pickNextWord();
  void drawSpinnerCompact();
  void drawWordCompact();
  void drawSpinnerBig();
  void drawWordBig();

public:
  void setup() override;
  void loop() override;
  void websocketHook(JsonDocument &request) override;
  const char *getName() const override;
};
