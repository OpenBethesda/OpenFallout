-- A book of Fallout that the player activates in the world goes into the inventory, and no screen opens: the book window of
-- Morrowind and the Lua path that opened it for Fallout books are gone. The book is the Wasteland Survival Guide of the quest
-- item (Fallout3.esm 0x0C5634), which teaches no skill and has no script. It logs numbers and ids only.
local BOOK = { 'Fallout3.esm', 0x0C5634 }

return { limit = 120, delay = 4, run = function(r)
    r.god(true)
    r.wait(6)
    local before = r.inventory('before', nil, BOOK)
    local carried = before and before.carried or 0

    local book = r.spawn{ record = BOOK, dist = 80, bearing = 0, label = 'book' }
    if book == nil then return end
    r.wait(2)

    local ok, err = pcall(function() book:activateBy(r.player()) end)
    if not ok then
        r.error('book', 'activate', tostring(err))
        return
    end
    r.log('book', 'activated')

    -- The screen of the book opened on the frame after the activation (an event to the player); watch for three seconds
    for i = 1, 6 do
        local mode = r.mode('after ' .. i)
        if mode == nil or mode.ui ~= 'nil' then
            r.error('book', 'a screen opened', tostring(mode and mode.ui))
            return
        end
        r.wait(0.5)
    end

    local after = r.inventory('after', nil, BOOK)
    if after == nil or after.carried ~= carried + 1 then
        r.error('book', 'not in the inventory', string.format('before=%d after=%s', carried, tostring(after and after.carried)))
        return
    end
    if book:isValid() and book.count > 0 then
        r.error('book', 'still in the world', 'count=' .. tostring(book.count))
        return
    end
    r.log('book', 'picked up with no screen')
end }
