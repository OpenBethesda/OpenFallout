function (get_whole_archive_options OUT_VAR)
    # We use --whole-archive because OSG plugins use registration.
    # The macOS linker takes -all_load whichever compiler drives it.
    set(${OUT_VAR} -Wl,-all_load ${ARGN} -Wl,-noall_load PARENT_SCOPE)
endfunction ()
