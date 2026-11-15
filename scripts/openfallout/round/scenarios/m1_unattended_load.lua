-- Mac Native, milestone one: a save whose content files are not all loaded is loaded with no box when the caller said
-- `--missing-content=accept` (rounds/m1-unattended-load.json). The round file checks the log lines; this only shows that
-- the game runs after the load: where the player is, a few seconds of play and one picture.
return function(r)
    r.eye('loaded')
    r.wait(3)
    r.eye('after 3 s')
    r.shot('loaded')
end
