-- carts/hello.lua — move the diamond (arrows/d-pad), hold A to scroll faster.
-- Two-layer parallax: BG0 (grid) scrolls at 1x, BG1 (orange pillars) at 2x.
-- The pillars are priority tiles, so the diamond passes BEHIND them.

local px, py = 156, 86
local scroll = 0
local frames = 0

function _init()
  px, py, scroll, frames = 156, 86, 0, 0
end

function _update()
  local step = 2
  if button(LEFT)  then px = px - step end
  if button(RIGHT) then px = px + step end
  if button(UP)    then py = py - step end
  if button(DOWN)  then py = py + step end
  if px < 0 then px = 0 elseif px > 312 then px = 312 end
  if py < 0 then py = 0 elseif py > 172 then py = 172 end
  scroll = scroll + (button(A) and 3 or 1)
  frames = frames + 1

  if button_pressed(X) then sound(0, 440, NOISE, 12) end
  if not button(X)     then sound_off(0) end
end

function _draw()
  layer(0); camera(scroll, 0)          -- background
  layer(1); camera(scroll * 2, 0)      -- foreground pillars, 2x parallax

  use_palette(1); sprite(2, px, py)    -- red diamond

  overlay_palette(1)                   -- HUD in red (sub-palette 1, color 1)
  text("SECTOR8", 2, 1, 1)
  text("X" .. px .. " F" .. frames, 2, 170, 1)
end
