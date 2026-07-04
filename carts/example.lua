-- example.lua — a cart whose art comes entirely from an imported PNG.
-- sheet.png tiles: 0=red, 1=green, 2=blue, 3=transparent.

function _init()
  for y = 0, MAP_H - 1 do
    for x = 0, MAP_W - 1 do
      set_tile(x, y, 0)          -- fill BG0 with the red tile
    end
  end
end

function _draw()
  sprite(2, 156, 86)             -- a blue tile as a sprite
end
