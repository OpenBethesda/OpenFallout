-- The game of hacking a terminal as a player works it out, for autoplay.lua: each guess is told its likeness (how many letters
-- are the same in the same places as the password), and only the words that agree with every likeness so far can be the
-- password. Of those, the guess is the one that leaves the fewest in the worst case. A board is never played to its last
-- attempt with more than one word left: the player leaves it instead, and the terminal lays out a new board (with its attempts
-- back) when it is opened again, so it is never locked out by luck.
local M = {}

function M.likeness(a, b)
    local same = 0
    for i = 1, math.min(#a, #b) do
        if a:sub(i, i) == b:sub(i, i) then same = same + 1 end
    end
    return same
end

-- The places, in `words`, of the words that agree with every guess so far ({ word, likeness } each) and were not guessed
function M.candidates(words, guesses)
    local found = {}
    for index, word in ipairs(words) do
        local fits = true
        for _, guess in ipairs(guesses) do
            if guess.word == word or M.likeness(word, guess.word) ~= guess.likeness then
                fits = false
                break
            end
        end
        if fits then found[#found + 1] = index end
    end
    return found
end

-- What to do with a board: { guess = place } (a word to try), { leave = true } (one attempt left and more than one word could
-- be the password) or { close = true } (no word agrees: the board is not one this reasoning knows); `left` is how many words
-- could still be it
function M.choose(words, guesses, attempts)
    local candidates = M.candidates(words, guesses)
    if #candidates == 0 then return { close = true, left = 0 } end
    if #candidates == 1 then return { guess = candidates[1], left = 1 } end
    if attempts <= 1 then return { leave = true, left = #candidates } end
    local best, bestWorst
    for _, index in ipairs(candidates) do
        local groups, worst = {}, 0
        for _, other in ipairs(candidates) do
            if other ~= index then
                local same = M.likeness(words[index], words[other])
                groups[same] = (groups[same] or 0) + 1
                if groups[same] > worst then worst = groups[same] end
            end
        end
        if bestWorst == nil or worst < bestWorst then best, bestWorst = index, worst end
    end
    return { guess = best, left = #candidates }
end

return M
