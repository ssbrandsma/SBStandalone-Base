file(SHA256 "${BINARY}" hash)
file(SIZE "${BINARY}" bytes)
get_filename_component(output "${BINARY}" DIRECTORY)
file(WRITE "${output}/manifest.txt" "sbupnp=0.3.1\npupnp=${REVISION}\ncompiler=${COMPILER}\nsha256=${hash}\nbytes=${bytes}\n")
