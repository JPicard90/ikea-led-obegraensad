#include "plugins/ClaudeThinkingPlugin.h"
#include "storage.h"

#include <ctype.h>

// One loop tick every TICK_MS; all animation speeds are derived from ticks.
static constexpr unsigned long TICK_MS = 40;
static constexpr uint16_t TICKS_PER_TYPE = 4;
static constexpr uint16_t TICKS_PER_DELETE = 2;
static constexpr uint16_t TICKS_PAUSE = 75;
static constexpr uint16_t TICKS_PER_SPINNER_FRAME = 3;
static constexpr uint16_t CURSOR_BLINK_TICKS = 10;

// Compact layout: 5x5 spinner on rows 0-4, then the word in a 3x5 font on two
// 4-character lines (rows 5-9 and 11-15, one blank row between them) so it
// always fits the panel whole. The spinner only reaches row 4 with a few
// sparse pixels at its peak frames, so the text can sit right below it.
static constexpr int SPINNER_X = 5;
static constexpr int SPINNER_SIZE = 5;
static constexpr int CHAR_WIDTH = 3;
static constexpr int CHAR_PITCH = 4;
static constexpr int CHARS_PER_LINE = 4;
static constexpr int LINE_PITCH = 6;
static constexpr int TEXT_Y0 = 5;
static constexpr uint8_t MAX_WORD = CHARS_PER_LINE * 2;

// Big layout (the original look): 7x7 spinner on rows 0-6, word typed in the
// system font on row 9, shifting left terminal-style once it outgrows the
// panel.
static constexpr int BIG_CHAR_SPACING = 6;
static constexpr int BIG_CURSOR_WIDTH = 5;
static constexpr int BIG_CURSOR_HEIGHT = 7;
static constexpr int BIG_TEXT_Y = 9;
static constexpr int BIG_SPINNER_X = 4;
static constexpr int BIG_SPINNER_SIZE = 7;

// 5x5 spinner frames, growing from a dot to an 8-ray star; bit 4-x of each
// row byte is pixel x.
static const uint8_t SPINNER_FRAMES[4][SPINNER_SIZE] = {
    {0x00, 0x00, 0x04, 0x00, 0x00},
    {0x00, 0x04, 0x0E, 0x04, 0x00},
    {0x04, 0x04, 0x1F, 0x04, 0x04},
    {0x15, 0x0E, 0x1F, 0x0E, 0x15},
};
static const uint8_t SPINNER_BRIGHTNESS[4] = {90, 145, 200, 255};
static const uint8_t SPINNER_SEQUENCE[6] = {0, 1, 2, 3, 2, 1};

// 7x7 spinner frames for the big layout; bit 6-x of each row byte is pixel x.
static const uint8_t BIG_SPINNER_FRAMES[5][BIG_SPINNER_SIZE] = {
    {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x08, 0x1C, 0x08, 0x00, 0x00},
    {0x00, 0x08, 0x2A, 0x3E, 0x2A, 0x08, 0x00},
    {0x08, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x08},
    {0x49, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x49},
};
static const uint8_t BIG_SPINNER_BRIGHTNESS[5] = {90, 130, 170, 215, 255};
static const uint8_t BIG_SPINNER_SEQUENCE[8] = {0, 1, 2, 3, 4, 3, 2, 1};

// 3x5 uppercase font, A-Z; bit 2-x of each row byte is pixel x.
static const uint8_t FONT3X5[26][5] = {
    {0x2, 0x5, 0x7, 0x5, 0x5}, // A
    {0x6, 0x5, 0x6, 0x5, 0x6}, // B
    {0x3, 0x4, 0x4, 0x4, 0x3}, // C
    {0x6, 0x5, 0x5, 0x5, 0x6}, // D
    {0x7, 0x4, 0x6, 0x4, 0x7}, // E
    {0x7, 0x4, 0x6, 0x4, 0x4}, // F
    {0x3, 0x4, 0x5, 0x5, 0x3}, // G
    {0x5, 0x5, 0x7, 0x5, 0x5}, // H
    {0x7, 0x2, 0x2, 0x2, 0x7}, // I
    {0x1, 0x1, 0x1, 0x5, 0x2}, // J
    {0x5, 0x5, 0x6, 0x5, 0x5}, // K
    {0x4, 0x4, 0x4, 0x4, 0x7}, // L
    {0x5, 0x7, 0x7, 0x5, 0x5}, // M
    {0x7, 0x5, 0x5, 0x5, 0x5}, // N
    {0x2, 0x5, 0x5, 0x5, 0x2}, // O
    {0x6, 0x5, 0x6, 0x4, 0x4}, // P
    {0x2, 0x5, 0x5, 0x2, 0x1}, // Q
    {0x6, 0x5, 0x6, 0x5, 0x5}, // R
    {0x3, 0x4, 0x2, 0x1, 0x6}, // S
    {0x7, 0x2, 0x2, 0x2, 0x2}, // T
    {0x5, 0x5, 0x5, 0x5, 0x2}, // U
    {0x5, 0x5, 0x5, 0x2, 0x2}, // V
    {0x5, 0x5, 0x5, 0x7, 0x5}, // W
    {0x5, 0x5, 0x2, 0x5, 0x5}, // X
    {0x5, 0x5, 0x2, 0x2, 0x2}, // Y
    {0x7, 0x1, 0x2, 0x4, 0x7}, // Z
};

// All 8 letters or fewer so every word fits the compact two-line grid whole;
// the compact renderer uppercases them, the big one types them as written.
static const char *const WORDS[] = {
    "Thinking", "Brewing",  "Vibing",   "Noodling", "Mulling",  "Musing",
    "Scheming", "Churning", "Whirring", "Crafting", "Forging",  "Stewing",
    "Weaving",  "Herding",  "Booping",  "Doodling", "Dreaming", "Hatching",
    "Spinning", "Whisking", "Plotting", "Puzzling", "Humming",  "Cooking",
};
static constexpr uint8_t WORD_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);

void ClaudeThinkingPlugin::setup()
{
  Screen.clear();
  tick = 0;
  phaseTicks = 0;
  phase = Phase::Typing;

#ifdef ENABLE_STORAGE
  storage.begin("led-wall", true);
  bigFont = storage.getBool("claudeBig", false);
  storage.end();
#endif

  pickNextWord();
}

void ClaudeThinkingPlugin::websocketHook(JsonDocument &request)
{
  const char *event = request["event"];

  if (event != nullptr && !strcmp(event, "claudefont"))
  {
    bigFont = request["big"].as<bool>();

#ifdef ENABLE_STORAGE
    storage.begin("led-wall", false);
    storage.putBool("claudeBig", bigFont);
    storage.end();
#endif

    Screen.clear();
  }
}

void ClaudeThinkingPlugin::loop()
{
  if (!timer.isReady(TICK_MS))
    return;

  tick++;
  phaseTicks++;

  switch (phase)
  {
  case Phase::Typing:
    if (phaseTicks % TICKS_PER_TYPE == 0 && typedLength < wordLength)
    {
      typedLength++;
      if (typedLength == wordLength)
      {
        phase = Phase::Pausing;
        phaseTicks = 0;
      }
    }
    break;

  case Phase::Pausing:
    if (phaseTicks >= TICKS_PAUSE)
    {
      phase = Phase::Deleting;
      phaseTicks = 0;
    }
    break;

  case Phase::Deleting:
    if (phaseTicks % TICKS_PER_DELETE == 0 && typedLength > 0)
    {
      typedLength--;
      if (typedLength == 0)
      {
        pickNextWord();
        phase = Phase::Typing;
        phaseTicks = 0;
      }
    }
    break;
  }

  Screen.clear();
  if (bigFont)
  {
    drawSpinnerBig();
    drawWordBig();
  }
  else
  {
    drawSpinnerCompact();
    drawWordCompact();
  }
}

void ClaudeThinkingPlugin::pickNextWord()
{
  uint8_t index;
  do
  {
    index = random(WORD_COUNT);
  } while (WORD_COUNT > 1 && index == lastWordIndex);

  lastWordIndex = index;
  word = WORDS[index];
  wordLength = strlen(word);
  if (wordLength > MAX_WORD)
    wordLength = MAX_WORD;
  typedLength = 0;
}

void ClaudeThinkingPlugin::drawSpinnerCompact()
{
  uint8_t frame = SPINNER_SEQUENCE[(tick / TICKS_PER_SPINNER_FRAME) % 6];
  const uint8_t *rows = SPINNER_FRAMES[frame];

  for (int y = 0; y < SPINNER_SIZE; y++)
  {
    for (int x = 0; x < SPINNER_SIZE; x++)
    {
      if (rows[y] & (1 << (SPINNER_SIZE - 1 - x)))
      {
        Screen.setPixel(SPINNER_X + x, y, 1, SPINNER_BRIGHTNESS[frame]);
      }
    }
  }
}

void ClaudeThinkingPlugin::drawWordCompact()
{
  for (uint8_t i = 0; i < typedLength; i++)
  {
    char c = toupper(word[i]);
    if (c < 'A' || c > 'Z')
      continue;

    int x0 = (i % CHARS_PER_LINE) * CHAR_PITCH;
    int y0 = TEXT_Y0 + (i / CHARS_PER_LINE) * LINE_PITCH;
    const uint8_t *glyph = FONT3X5[c - 'A'];

    for (int y = 0; y < 5; y++)
    {
      for (int x = 0; x < CHAR_WIDTH; x++)
      {
        if (glyph[y] & (1 << (CHAR_WIDTH - 1 - x)))
        {
          Screen.setPixel(x0 + x, y0 + y, 1);
        }
      }
    }
  }

  // Blinking cursor in the next character cell, terminal-style; with the word
  // complete on a full 8-letter grid there is no free cell, so it just hides.
  bool cursorVisible =
      phase != Phase::Pausing || (phaseTicks / CURSOR_BLINK_TICKS) % 2 == 0;
  if (cursorVisible && typedLength < MAX_WORD)
  {
    int x0 = (typedLength % CHARS_PER_LINE) * CHAR_PITCH;
    int y0 = TEXT_Y0 + (typedLength / CHARS_PER_LINE) * LINE_PITCH;
    Screen.drawRectangle(x0, y0, CHAR_WIDTH, 5, true, 1);
  }
}

void ClaudeThinkingPlugin::drawSpinnerBig()
{
  uint8_t frame = BIG_SPINNER_SEQUENCE[(tick / TICKS_PER_SPINNER_FRAME) % 8];
  const uint8_t *rows = BIG_SPINNER_FRAMES[frame];

  for (int y = 0; y < BIG_SPINNER_SIZE; y++)
  {
    for (int x = 0; x < BIG_SPINNER_SIZE; x++)
    {
      if (rows[y] & (1 << (BIG_SPINNER_SIZE - 1 - x)))
      {
        Screen.setPixel(BIG_SPINNER_X + x, y, 1, BIG_SPINNER_BRIGHTNESS[frame]);
      }
    }
  }
}

void ClaudeThinkingPlugin::drawWordBig()
{
  const font &systemFont = fonts[0];

  // Keep the cursor on screen: once the text outgrows the panel, shift it
  // left terminal-style.
  int textWidth = typedLength * BIG_CHAR_SPACING;
  int scrollOffset = textWidth + BIG_CURSOR_WIDTH - COLS;
  if (scrollOffset < 0)
    scrollOffset = 0;

  for (uint8_t i = 0; i < typedLength; i++)
  {
    int x = i * BIG_CHAR_SPACING - scrollOffset;
    if (x <= -BIG_CHAR_SPACING || x >= COLS)
      continue;

    char c = word[i];
    if (c < systemFont.offset || (c - systemFont.offset) >= (int)systemFont.data.size())
    {
      c = ' ';
    }
    Screen.drawCharacter(x, BIG_TEXT_Y, Screen.readBytes(systemFont.data[c - systemFont.offset]), 8);
  }

  bool cursorVisible =
      phase != Phase::Pausing || (phaseTicks / CURSOR_BLINK_TICKS) % 2 == 0;
  if (cursorVisible)
  {
    Screen.drawRectangle(textWidth - scrollOffset, BIG_TEXT_Y, BIG_CURSOR_WIDTH,
                         BIG_CURSOR_HEIGHT, true, 1);
  }
}

const char *ClaudeThinkingPlugin::getName() const
{
  return "Claude Thinking";
}
